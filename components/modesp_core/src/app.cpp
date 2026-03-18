/**
 * @file app.cpp
 * @brief Application lifecycle — init та main loop
 */

#include "modesp/app.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "App";

namespace modesp {

App& App::instance() {
    static App app;
    return app;
}

bool App::init() {
    ESP_LOGI(TAG, "Initializing ModESP v%s (%s)", FIRMWARE_VERSION, BUILD_DATE);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    boot_time_us_ = (uint32_t)esp_timer_get_time();
    return true;
}

void App::run() {
    // Ініціалізація всіх зареєстрованих модулів
    if (!modules_.init_all(state_)) {
        ESP_LOGE(TAG, "Module init failed!");
        return;
    }

    running_ = true;

    // ── HW Watchdog: підписуємо поточну задачу ──
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms  = MODESP_WDT_TIMEOUT_MS,
        .idle_core_mask = 0,              // Не моніторимо idle задачі
        .trigger_panic = true,            // Panic при зависанні
    };
    esp_err_t wdt_err = esp_task_wdt_reconfigure(&wdt_cfg);
    if (wdt_err == ESP_OK) {
        esp_task_wdt_add(nullptr);  // nullptr = поточна задача
        ESP_LOGI(TAG, "HW watchdog: %d ms timeout", MODESP_WDT_TIMEOUT_MS);
    } else {
        ESP_LOGW(TAG, "HW watchdog reconfigure failed: %s", esp_err_to_name(wdt_err));
    }

    ESP_LOGI(TAG, "Entering main loop (%d Hz, %d modules)",
             MODESP_MAIN_LOOP_HZ, (int)modules_.module_count());
    ESP_LOGI(TAG, "Free heap after init: %lu bytes", esp_get_free_heap_size());

    const TickType_t period = pdMS_TO_TICKS(1000 / MODESP_MAIN_LOOP_HZ);
    const uint32_t dt_ms = 1000 / MODESP_MAIN_LOOP_HZ;  // 10ms при 100Hz

    TickType_t last_wake = xTaskGetTickCount();

    while (running_) {
        // Оновити всі модулі
        modules_.update_all(dt_ms);

        // Оновити uptime в SharedState (раз на секунду)
        // Примітка: system.heap_free оновлюється SystemMonitor
        static uint32_t sec_counter = 0;
        sec_counter += dt_ms;
        if (sec_counter >= 1000) {
            sec_counter = 0;
            state_.set("system.uptime", static_cast<int32_t>(uptime_sec()));
        }

        // Скидаємо HW watchdog
        esp_task_wdt_reset();

        // Точний період циклу
        vTaskDelayUntil(&last_wake, period);
    }

    // Graceful shutdown
    stop();
}

void App::stop() {
    ESP_LOGI(TAG, "Shutting down...");
    running_ = false;
    modules_.stop_all();
    ESP_LOGI(TAG, "Shutdown complete");
}

uint32_t App::uptime_sec() const {
    return (uint32_t)((esp_timer_get_time() - boot_time_us_) / 1000000ULL);
}

} // namespace modesp
