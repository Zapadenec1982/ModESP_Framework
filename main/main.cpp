/**
 * @file main.cpp
 * @brief ModESP v4 — Phase 9.1: Equipment Layer
 *
 * Staged boot sequence:
 *   1. NVS init (required for WiFi credentials)
 *   2. Register system services -> init_all (phase 1)
 *   3. ConfigService reads board.json + bindings.json
 *   4. Register WiFi service
 *   5. HAL initializes GPIO from BoardConfig
 *   6. DriverManager creates drivers from bindings
 *   7. Register EquipmentModule (binds drivers) + business modules -> init_all (phase 2)
 *   8. Inject dependencies, register HTTP + WS -> init_all (phase 3)
 *   9. Connect WS handler to HTTP server
 *  10. Main loop: drivers update -> modules update -> WDT reset
 *
 * Three-phase init works because module_manager.init_all()
 * skips modules with state != CREATED.
 */

#include "modesp/app.h"
#include "modesp/base_module.h"

// Phase 2: System services
#include "modesp/error_service.h"
#include "modesp/watchdog_service.h"
#include "modesp/system_monitor.h"
#include "modesp/logger_service.h"

// Phase 4: Configuration + HAL + Persist
#include "modesp/services/config_service.h"
#include "modesp/services/persist_service.h"
#include "modesp/hal/hal.h"
#include "modesp/hal/driver_manager.h"

// Phase 5a: Network
#include "modesp/net/wifi_service.h"
#include "modesp/net/http_service.h"
#include "modesp/net/ws_service.h"
#include "modesp/services/nvs_helper.h"

// Phase 5b: Scenario engine — temporarily removed during rebuild (Phase 0).
// Restored у Phase 3 with namespace modesp::scenario.

// Cloud backend (compile-time Kconfig choice)
#if defined(CONFIG_MODESP_CLOUD_AWS)
  #include "modesp/net/aws_iot_service.h"
#else
  #include "modesp/net/mqtt_service.h"
#endif

// Module includes + instances + registration (auto-generated from project.json)
#include "module_includes.h"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_ota_ops.h"
#include "esp_sntp.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "main";

// ═══════════════════════════════════════════════════════════════
// Static instances (zero heap allocation)
// ═══════════════════════════════════════════════════════════════

// System services (CRITICAL priority — start first)
static modesp::ErrorService    error_service;
static modesp::LoggerService   logger_service;
static modesp::ConfigService   config_service;
static modesp::PersistService  persist_service;

// Services with dependencies
static modesp::SystemMonitor   system_monitor(error_service);

// HAL + DriverManager (not BaseModule — managed directly)
static modesp::HAL             hal;
static modesp::DriverManager   driver_manager;

// Network services
static modesp::WiFiService     wifi_service;
static modesp::HttpService     http_service;
static modesp::WsService       ws_service;

// Scenario engine — temporarily removed during rebuild (Phase 0).
// Restored у Phase 3 with namespace modesp::scenario.

// Cloud backend (compile-time Kconfig choice)
#if defined(CONFIG_MODESP_CLOUD_AWS)
static modesp::AwsIotService   cloud_service;
#else
static modesp::MqttService     cloud_service;
#endif

// Module instances (auto-generated from project.json)
#include "module_instances.h"
#include "module_register.h"

// ═══════════════════════════════════════════════════════════════
// Entry point
// ═══════════════════════════════════════════════════════════════

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "  ModESP Framework v4.0.0");
    ESP_LOGI(TAG, "  WiFi + HTTP API + WebSocket");
    ESP_LOGI(TAG, "======================================");

    // ── Step 0: NVS init (before everything) ──
    modesp::nvs_helper::init();

    // ── Step 0b: OTA validity — deferred validation (60s timeout) ──
    const esp_partition_t* running = esp_ota_get_running_partition();
    bool ota_pending_verify = false;
    {
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
            if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
                ota_pending_verify = true;
                ESP_LOGW(TAG, "OTA: New firmware — validating (60s timeout)...");
            }
        }
    }
    ESP_LOGI(TAG, "Running from: %s (0x%lx)",
             running->label, (unsigned long)running->address);

    auto& app = modesp::App::instance();

    // ── Step 1: Core init ──
    if (!app.init()) {
        ESP_LOGE(TAG, "App init failed!");
        return;
    }

    // Публікуємо інформацію про прошивку в SharedState
    {
        const esp_app_desc_t* desc = esp_app_get_description();
        app.state().set("_ota.version", desc->version);
        app.state().set("_ota.partition", running->label);
        app.state().set("_ota.idf", desc->idf_ver);

        // Дата + час збірки
        char build_dt[32];
        snprintf(build_dt, sizeof(build_dt), "%s %s", desc->date, desc->time);
        app.state().set("_ota.date", build_dt);
        app.state().set("_ota.board", desc->project_name);

        // OTA статус для cloud feedback (оновлюється з ota_handler.cpp)
        app.state().set("_ota.status", "idle");
        app.state().set("_ota.progress", static_cast<int32_t>(0));
        app.state().set("_ota.error", "");
    }

    // WatchdogService needs ModuleManager reference
    static modesp::WatchdogService watchdog_service(error_service, app.modules());

    // ── Step 2: Register system services (CRITICAL) ──
    app.modules().register_module(error_service);
    app.modules().register_module(watchdog_service);
    app.modules().register_module(logger_service);
    app.modules().register_module(config_service);

    // PersistService: відновлює persisted settings з NVS → SharedState
    // Повинен ініціалізуватися ДО бізнес-модулів (Phase 2)
    persist_service.set_state(&app.state());
    app.modules().register_module(persist_service);

    app.modules().register_module(system_monitor);

    ESP_LOGI(TAG, "Phase 1: Initializing system services...");
    app.modules().init_all(app.state());

    // ── Step 3: Read config ──
    const auto& board_cfg = config_service.board_config();
    const auto& bindings  = config_service.binding_table();

    ESP_LOGI(TAG, "Board: %s v%s (%d gpio_out, %d onewire buses)",
             board_cfg.board_name.c_str(),
             board_cfg.board_version.c_str(),
             (int)board_cfg.gpio_outputs.size(),
             (int)board_cfg.onewire_buses.size());

    // ── Step 4: Register WiFi + MQTT (HIGH priority) ──
    app.modules().register_module(wifi_service);
    cloud_service.set_state(&app.state());
    app.modules().register_module(cloud_service);

    // ── Step 5: Initialize HAL (GPIO setup) ──
    if (!hal.init(board_cfg)) {
        ESP_LOGE(TAG, "HAL init failed!");
        return;
    }

    // ── Step 6: Create and init all drivers ──
    if (!driver_manager.init(bindings, hal)) {
        ESP_LOGE(TAG, "DriverManager init failed!");
        return;
    }
    ESP_LOGI(TAG, "Drivers: %d sensors, %d actuators",
             (int)driver_manager.sensor_count(),
             (int)driver_manager.actuator_count());

    // ── Step 7: Register all modules (auto-generated from project.json) ──
    equipment.bind_drivers(driver_manager);

    // Scenario engine wiring temporarily removed during rebuild (Phase 0).
    // Restored у Phase 3 with namespace modesp::scenario + Engine ctor injection
    // of ActionRegistry, ContinuousRegistry, NvsObserver.

    modesp_register_modules(app);

    ESP_LOGI(TAG, "Phase 2: Initializing WiFi + business modules...");
    app.modules().init_all(app.state());

    // ── Step 7b: NTP time sync ──
    {
        bool ntp_enabled = true;
        modesp::nvs_helper::read_bool("time", "ntp_enabled", ntp_enabled);

        char tz[48] = "EET-2EEST,M3.5.0/3,M10.5.0/4";  // Київ
        modesp::nvs_helper::read_str("time", "timezone", tz, sizeof(tz));
        setenv("TZ", tz, 1);
        tzset();

        if (ntp_enabled) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
            ESP_LOGI(TAG, "SNTP started (TZ=%s)", tz);
        } else {
            ESP_LOGI(TAG, "NTP disabled, manual time mode (TZ=%s)", tz);
        }
        app.state().set("system.time", "--:--:--");
        app.state().set("system.date", "--.--.----");
    }

    // ── Step 8: Inject dependencies + register HTTP + WS ──
    http_service.set_state(&app.state());
    http_service.set_config(&config_service);
    http_service.set_modules(&app.modules());
    http_service.set_wifi(&wifi_service);
    http_service.set_persist(&persist_service);
    http_service.set_hal(&hal);
    http_service.set_datalogger(&datalogger);
    // http_service.set_scenario_engine — wired у Phase 3.

    ws_service.set_state(&app.state());

    app.modules().register_module(http_service);
    app.modules().register_module(ws_service);

    ESP_LOGI(TAG, "Phase 3: Initializing HTTP + WebSocket...");
    app.modules().init_all(app.state());

    // ── Step 9: Connect WS handler + finalize HTTP routes ──
    // Order matters: WS handler must be registered BEFORE the
    // wildcard static handler (/*), otherwise httpd_uri_match_wildcard
    // considers /* as already matching /ws for HTTP_GET.
    if (http_service.server()) {
        ws_service.set_http_server(http_service.server());
        cloud_service.set_http_server(http_service.server());
        http_service.register_static_handler();  // Must be last (wildcard catch-all)
    } else {
        ESP_LOGW(TAG, "HTTP server not started, WebSocket unavailable");
    }

    // ── Step 10: Main loop ──
    ESP_LOGI(TAG, "Registered %d modules total", (int)app.modules().module_count());
    ESP_LOGI(TAG, "Free heap after init: %lu bytes", esp_get_free_heap_size());

    // HW Watchdog — ESP-IDF v5.x auto-ініціалізує TWDT, тому reconfigure
    bool wdt_subscribed = false;
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms  = MODESP_WDT_TIMEOUT_MS,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_err_t wdt_err = esp_task_wdt_reconfigure(&wdt_cfg);
    if (wdt_err != ESP_OK) {
        // TWDT не ініціалізований — створюємо
        wdt_err = esp_task_wdt_init(&wdt_cfg);
    }
    if (wdt_err == ESP_OK) {
        esp_err_t add_err = esp_task_wdt_add(nullptr);
        if (add_err == ESP_OK) {
            wdt_subscribed = true;
            ESP_LOGI(TAG, "HW watchdog: %d ms timeout", MODESP_WDT_TIMEOUT_MS);
        } else if (add_err == ESP_ERR_INVALID_STATE) {
            // Задача вже підписана — ОК
            wdt_subscribed = true;
            ESP_LOGI(TAG, "HW watchdog: task already subscribed (%d ms)", MODESP_WDT_TIMEOUT_MS);
        } else {
            ESP_LOGW(TAG, "HW watchdog: add task failed: %s", esp_err_to_name(add_err));
        }
    } else {
        ESP_LOGW(TAG, "HW watchdog init failed: %s", esp_err_to_name(wdt_err));
    }

    constexpr int LOOP_HZ = MODESP_MAIN_LOOP_HZ;
    const TickType_t period = pdMS_TO_TICKS(1000 / LOOP_HZ);
    const uint32_t dt_ms = 1000 / LOOP_HZ;

    ESP_LOGI(TAG, "Entering main loop (%d Hz)...", LOOP_HZ);

    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        // 1. Update all drivers (sensors read, actuators apply)
        driver_manager.update_all(dt_ms);

        // 2. Update all modules (business logic reads from drivers)
        app.modules().update_all(dt_ms);

        // 3. OTA rollback validation (60s timeout)
        if (ota_pending_verify) {
            bool net_ok = wifi_service.is_connected() && http_service.server();
            if (net_ok) {
                esp_ota_mark_app_valid_cancel_rollback();
                ota_pending_verify = false;
                ESP_LOGI(TAG, "OTA: Firmware validated (WiFi + HTTP OK after %lus)",
                         (unsigned long)app.uptime_sec());
            } else if (app.uptime_sec() > 60) {
                ESP_LOGE(TAG, "OTA: Validation timeout (60s) — rollback on next reboot");
                esp_restart();
            }
        }

        // 4. Uptime + heap diagnostics in SharedState (once per second)
        static uint32_t sec_counter = 0;
        sec_counter += dt_ms;
        if (sec_counter >= 1000) {
            sec_counter = 0;
            // track_change=false: діагностичні ключі не тригерять WS delta
            app.state().set("system.uptime",
                            static_cast<int32_t>(app.uptime_sec()), false);
            size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            app.state().set("system.heap_largest",
                            static_cast<int32_t>(largest), false);
        }

        // 5. HW watchdog reset
        if (wdt_subscribed) esp_task_wdt_reset();

        // 6. Precise loop timing
        vTaskDelayUntil(&last_wake, period);
    }
}
