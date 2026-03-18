/**
 * @file wifi_service.cpp
 * @brief WiFi STA/AP management implementation
 *
 * WiFi стек ініціалізується ОДИН раз в on_init().
 * start_sta() / start_ap() тільки змінюють режим і конфіг.
 */

#include "modesp/net/wifi_service.h"
#include "modesp/services/nvs_helper.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "mdns.h"

#include <cstring>
#include <cstdio>
#include <algorithm>

static const char* TAG = "WiFi";

namespace modesp {

// ── Event handler (static, dispatches to instance) ──────────────

void WiFiService::event_handler(void* arg, esp_event_base_t base,
                                 int32_t event_id, void* event_data) {
    auto* self = static_cast<WiFiService*>(arg);
    if (base == WIFI_EVENT) {
        self->handle_wifi_event(event_id, event_data);
    } else if (base == IP_EVENT) {
        self->handle_ip_event(event_id, event_data);
    }
}

void WiFiService::handle_wifi_event(int32_t event_id, void* event_data) {
    switch (event_id) {
        case WIFI_EVENT_STA_START:
            // При AP→STA probe connect викликається явно з attempt_ap_sta_probe()
            if (ap_sta_probing_) {
                ESP_LOGD(TAG, "STA started (probe mode, connect deferred)");
            } else {
                ESP_LOGI(TAG, "STA started, connecting...");
                esp_wifi_connect();
            }
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            auto* dis = static_cast<wifi_event_sta_disconnected_t*>(event_data);

            // AP→STA probe fast-fail: повертаємо чистий AP mode
            if (ap_sta_probing_) {
                ESP_LOGD(TAG, "AP→STA probe failed (reason %d)", dis->reason);
                cancel_ap_sta_probe();
                break;
            }

            connected_ = false;
            ip_str_[0] = '\0';
            state_set("wifi.connected", false);
            state_set("wifi.ip", "");
            ESP_LOGW(TAG, "Disconnected from '%.*s', reason=%d",
                     dis->ssid_len, dis->ssid, dis->reason);
            if (!ap_mode_) {
                reconnect_pending_ = true;
            }
            break;
        }

        case WIFI_EVENT_AP_STACONNECTED: {
            auto* event = static_cast<wifi_event_ap_staconnected_t*>(event_data);
            ESP_LOGI(TAG, "AP: station connected (AID=%d)", event->aid);
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            auto* event = static_cast<wifi_event_ap_stadisconnected_t*>(event_data);
            ESP_LOGI(TAG, "AP: station disconnected (AID=%d)", event->aid);
            break;
        }

        default:
            break;
    }
}

void WiFiService::handle_ip_event(int32_t event_id, void* event_data) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        snprintf(ip_str_, sizeof(ip_str_), IPSTR, IP2STR(&event->ip_info.ip));

        // AP→STA probe успіх: переходимо в чистий STA mode
        if (ap_sta_probing_) {
            ESP_LOGI(TAG, "AP→STA probe success! Switching to STA, IP: %s", ip_str_);
            ap_sta_probing_ = false;
            ap_mode_ = false;
            connected_ = true;
            reconnect_pending_ = false;
            retry_count_ = 0;
            reconnect_interval_ = 2000;

            // APSTA → чистий STA (вивільнити AP ресурси)
            esp_wifi_set_mode(WIFI_MODE_STA);

            // Перезапустити mDNS на STA інтерфейсі
            stop_mdns();
            start_mdns();

            // Скинути STA watchdog
            disconnect_accum_ms_ = 0;
            stable_timer_ms_ = 0;

            state_set("wifi.connected", true);
            state_set("wifi.mode", "sta");
            state_set("wifi.ssid", ssid_);
            state_set("wifi.ip", ip_str_);
            return;
        }

        connected_ = true;
        reconnect_pending_ = false;
        retry_count_ = 0;
        reconnect_interval_ = 2000;

        state_set("wifi.connected", true);
        state_set("wifi.ssid", ssid_);
        state_set("wifi.ip", ip_str_);

        ESP_LOGI(TAG, "Connected! IP: %s", ip_str_);

        // Запускаємо mDNS після отримання IP в STA mode
        start_mdns();
    }
}

// ── Lifecycle ───────────────────────────────────────────────────

bool WiFiService::on_init() {
    ESP_LOGI(TAG, "Initializing WiFi...");

    // TCP/IP та event loop — один раз
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Ініціалізуємо WiFi драйвер один раз (netif створюється лениво)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Реєструємо event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, this, nullptr));

    // Стартуємо в потрібному режимі
    if (load_credentials()) {
        ESP_LOGI(TAG, "Credentials found, starting STA mode (SSID: '%s', pass_len: %d)",
                 ssid_, (int)strlen(password_));
        state_set("wifi.ssid", ssid_);
        return start_sta();
    } else {
        ESP_LOGW(TAG, "No credentials found in NVS, starting AP mode");
        return start_ap();
    }
}

void WiFiService::on_stop() {
    stop_mdns();
}

void WiFiService::on_update(uint32_t dt_ms) {
    // Deferred reconnect (after save_credentials HTTP response was sent)
    if (deferred_reconnect_) {
        deferred_reconnect_timer_ += dt_ms;
        if (deferred_reconnect_timer_ >= DEFERRED_RECONNECT_DELAY_MS) {
            deferred_reconnect_ = false;
            deferred_reconnect_timer_ = 0;

            ESP_LOGI(TAG, "Executing deferred reconnect (current: %s)", ap_mode_ ? "AP" : "STA");
            esp_wifi_disconnect();
            esp_wifi_stop();
            wifi_started_ = false;
            ap_mode_ = false;
            connected_ = false;
            start_sta();
            return;
        }
    }

    // Reconnect логіка (тільки STA mode)
    if (reconnect_pending_ && !ap_mode_) {
        reconnect_timer_ += dt_ms;
        if (reconnect_timer_ >= reconnect_interval_) {
            reconnect_timer_ = 0;

            if (retry_count_ >= MAX_RETRIES) {
                ESP_LOGW(TAG, "Max retries reached, switching to AP mode");
                reconnect_pending_ = false;
                start_ap();
                return;
            }

            retry_count_++;
            ESP_LOGI(TAG, "Reconnect attempt %d/%d (backoff %lu ms)",
                     retry_count_, MAX_RETRIES, (unsigned long)reconnect_interval_);
            esp_wifi_connect();

            // Exponential backoff
            reconnect_interval_ *= 2;
            if (reconnect_interval_ > MAX_BACKOFF_MS) {
                reconnect_interval_ = MAX_BACKOFF_MS;
            }
        }
    }

    // RSSI update (STA mode, connected)
    if (connected_ && !ap_mode_) {
        rssi_timer_ += dt_ms;
        if (rssi_timer_ >= RSSI_INTERVAL_MS) {
            rssi_timer_ = 0;
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                state_set("wifi.rssi", static_cast<int32_t>(ap_info.rssi));
            }
        }
    }

    // ── AP→STA periodic probe ──
    if (ap_mode_ && !ap_sta_probing_ && !deferred_reconnect_ && ssid_[0] != '\0') {
        if (ap_sta_probe_timer_ > dt_ms) {
            ap_sta_probe_timer_ -= dt_ms;
        } else {
            ap_sta_probe_timer_ = 0;
        }
        if (ap_sta_probe_timer_ == 0) {
            if (esp_get_free_heap_size() >= AP_STA_PROBE_HEAP_MIN) {
                attempt_ap_sta_probe();
            } else {
                ESP_LOGW(TAG, "AP→STA probe skipped: low heap %lu",
                         (unsigned long)esp_get_free_heap_size());
                ap_sta_probe_timer_ = ap_sta_probe_interval_;
            }
        }
    }

    // ── Probe timeout ──
    if (ap_sta_probing_) {
        if (ap_sta_probe_timeout_ > dt_ms) {
            ap_sta_probe_timeout_ -= dt_ms;
        } else {
            ap_sta_probe_timeout_ = 0;
        }
        if (ap_sta_probe_timeout_ == 0) {
            ESP_LOGW(TAG, "AP→STA probe #%u timeout", ap_sta_probe_count_);
            cancel_ap_sta_probe();
        }
    }

    // STA Watchdog: рестарт при тривалому disconnect
    if (!ap_mode_) {
        if (connected_) {
            stable_timer_ms_ += dt_ms;
            if (stable_timer_ms_ >= WIFI_STABLE_RESET_MS) {
                // Стабільне з'єднання 1 годину — скидаємо лічильник
                disconnect_accum_ms_ = 0;
                stable_timer_ms_ = 0;
            }
        } else {
            stable_timer_ms_ = 0;
            disconnect_accum_ms_ += dt_ms;
            if (disconnect_accum_ms_ >= WIFI_MAX_DISCONNECT_MS) {
                ESP_LOGE(TAG, "WiFi unstable (%lums disconnect), restarting...",
                         (unsigned long)disconnect_accum_ms_);
                esp_restart();
            }
        }
    }
}

// ── Credentials ─────────────────────────────────────────────────

bool WiFiService::load_credentials() {
    bool ok = nvs_helper::read_str("wifi", "ssid", ssid_, sizeof(ssid_));
    if (!ok || ssid_[0] == '\0') return false;

    nvs_helper::read_str("wifi", "pass", password_, sizeof(password_));
    return true;
}

bool WiFiService::save_credentials(const char* ssid, const char* password) {
    if (!ssid || ssid[0] == '\0') return false;

    bool ok = nvs_helper::write_str("wifi", "ssid", ssid);
    if (ok && password) {
        ok = nvs_helper::write_str("wifi", "pass", password);
    }
    if (ok) {
        strncpy(ssid_, ssid, sizeof(ssid_) - 1);
        ssid_[sizeof(ssid_) - 1] = '\0';
        if (password) {
            strncpy(password_, password, sizeof(password_) - 1);
            password_[sizeof(password_) - 1] = '\0';
        }
        ESP_LOGI(TAG, "Credentials saved for SSID: %s", ssid_);
    }
    return ok;
}

void WiFiService::request_reconnect() {
    ESP_LOGI(TAG, "Reconnect requested — deferred by %d ms to let HTTP response reach client",
             (int)DEFERRED_RECONNECT_DELAY_MS);
    if (ap_sta_probing_) {
        cancel_ap_sta_probe();
    }
    deferred_reconnect_ = true;
    deferred_reconnect_timer_ = 0;
}

// ── WiFi Scan ───────────────────────────────────────────────────

bool WiFiService::start_scan() {
    scan_done_ = false;

    // Скасувати AP→STA probe перед scan (APSTA вже активний при probe)
    if (ap_sta_probing_) {
        cancel_ap_sta_probe();
    }

    if (!wifi_started_) {
        ESP_LOGW(TAG, "Cannot scan: WiFi not started");
        return false;
    }

    // Перевірка heap — APSTA + scan потребує ~30-40KB додатково
    size_t free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Scan requested (heap: %u)", (unsigned)free_heap);
    if (free_heap < 50000) {
        ESP_LOGW(TAG, "Cannot scan: low memory (%u bytes free, need 50000)", (unsigned)free_heap);
        return false;
    }

    // В AP mode потрібно переключитись в APSTA для сканування.
    // Порожній STA config щоб STA не почала connecting.
    bool was_ap_only = false;
    if (ap_mode_) {
        wifi_mode_t current_mode;
        esp_wifi_get_mode(&current_mode);
        if (current_mode == WIFI_MODE_AP) {
            ensure_sta_netif();
            ESP_LOGI(TAG, "Switching AP -> APSTA for scan");
            esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_APSTA);
            if (mode_err != ESP_OK) {
                ESP_LOGW(TAG, "Cannot switch to APSTA: %s", esp_err_to_name(mode_err));
                return false;
            }
            wifi_config_t sta_cfg = {};
            esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
            was_ap_only = true;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_config.scan_time.active.min = 120;
    scan_config.scan_time.active.max = 500;  // 500ms per channel — більше шансів знайти всі AP

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);  // blocking scan

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan failed: %s (heap: %u)", esp_err_to_name(err),
                 (unsigned)esp_get_free_heap_size());
        esp_wifi_scan_stop();
        if (was_ap_only) esp_wifi_set_mode(WIFI_MODE_AP);
        return false;
    }

    scan_done_ = true;
    restore_ap_after_scan_ = was_ap_only;
    ESP_LOGI(TAG, "WiFi scan completed (heap: %u)", (unsigned)esp_get_free_heap_size());
    return true;
}

int WiFiService::get_scan_results(ScanResult* out, size_t max_results) {
    if (!scan_done_) return 0;

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    // Обмежуємо до 10 щоб не жерти пам'ять — stack allocation замість heap
    if (ap_count > 10) ap_count = 10;
    if (ap_count > max_results) ap_count = static_cast<uint16_t>(max_results);

    // Stack allocation — ~800 bytes для 10 записів, без heap fragmentation
    wifi_ap_record_t ap_records[10];
    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    for (uint16_t i = 0; i < ap_count; i++) {
        strncpy(out[i].ssid, reinterpret_cast<const char*>(ap_records[i].ssid), 32);
        out[i].ssid[32] = '\0';
        out[i].rssi = ap_records[i].rssi;
        out[i].authmode = static_cast<uint8_t>(ap_records[i].authmode);
    }

    scan_done_ = false;

    // Повертаємо AP mode ПІСЛЯ зчитування результатів —
    // esp_wifi_scan_get_ap_records() звільняє внутрішні буфери ESP-IDF
    if (restore_ap_after_scan_) {
        ESP_LOGI(TAG, "Restoring AP mode after scan results read (heap: %u)",
                 (unsigned)esp_get_free_heap_size());
        esp_wifi_set_mode(WIFI_MODE_AP);
        restore_ap_after_scan_ = false;
    }

    return static_cast<int>(ap_count);
}

// ── Lazy netif creation ──────────────────────────────────────────

void WiFiService::ensure_sta_netif() {
    if (!sta_netif_) {
        sta_netif_ = esp_netif_create_default_wifi_sta();
        ESP_LOGI(TAG, "STA netif created (heap: %u)", (unsigned)esp_get_free_heap_size());
    }
}

void WiFiService::ensure_ap_netif() {
    if (!ap_netif_) {
        ap_netif_ = esp_netif_create_default_wifi_ap();
        ESP_LOGI(TAG, "AP netif created (heap: %u)", (unsigned)esp_get_free_heap_size());
    }
}

// ── STA / AP ────────────────────────────────────────────────────

bool WiFiService::start_sta() {
    ensure_sta_netif();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid),
            ssid_, sizeof(wifi_config.sta.ssid) - 1);
    strncpy(reinterpret_cast<char*>(wifi_config.sta.password),
            password_, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "Starting STA: SSID='%s' pass_len=%d",
             ssid_, (int)strlen(password_));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Country code UA (канали 1-13) — потрібно викликати ПІСЛЯ esp_wifi_start(),
    // інакше налаштування не застосовуються у деяких версіях ESP-IDF v5.x
    {
        wifi_country_t country = {};
        country.cc[0] = 'U'; country.cc[1] = 'A'; country.cc[2] = '\0';
        country.schan = 1;
        country.nchan = 13;
        country.max_tx_power = 80;  // 20 dBm (значення в 0.25 dBm)
        country.policy = WIFI_COUNTRY_POLICY_MANUAL;
        esp_wifi_set_country(&country);
    }

    ap_mode_ = false;
    wifi_started_ = true;
    retry_count_ = 0;
    reconnect_interval_ = 2000;
    reconnect_timer_ = 0;
    reconnect_pending_ = false;
    state_set("wifi.mode", "sta");
    return true;
}

bool WiFiService::start_ap() {
    ensure_ap_netif();

    // Зупиняємо якщо працює (переключення STA→AP)
    if (wifi_started_) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        wifi_started_ = false;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    // Спочатку генеруємо дефолтний SSID з MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ap_ssid[33];
    snprintf(ap_ssid, sizeof(ap_ssid), "ModESP-%02X%02X", mac[4], mac[5]);

    char ap_pass[65] = {};
    int32_t ap_channel = 1;

    // Читаємо кастомну конфігурацію AP з NVS (якщо є)
    char custom_ssid[33] = {};
    if (nvs_helper::read_str("wifi", "ap_ssid", custom_ssid, sizeof(custom_ssid))
        && custom_ssid[0] != '\0') {
        strncpy(ap_ssid, custom_ssid, sizeof(ap_ssid) - 1);
        ap_ssid[sizeof(ap_ssid) - 1] = '\0';
    }
    nvs_helper::read_str("wifi", "ap_pass", ap_pass, sizeof(ap_pass));
    nvs_helper::read_i32("wifi", "ap_chan", ap_channel);
    if (ap_channel < 1 || ap_channel > 13) ap_channel = 1;

    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char*>(wifi_config.ap.ssid),
            ap_ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = static_cast<uint8_t>(strlen(ap_ssid));
    wifi_config.ap.channel = static_cast<uint8_t>(ap_channel);
    wifi_config.ap.max_connection = 2;

    // Пароль >= 8 символів → WPA2, інакше відкрита мережа
    if (strlen(ap_pass) >= 8) {
        strncpy(reinterpret_cast<char*>(wifi_config.ap.password),
                ap_pass, sizeof(wifi_config.ap.password) - 1);
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Country code UA — після esp_wifi_start()
    {
        wifi_country_t country = {};
        country.cc[0] = 'U'; country.cc[1] = 'A'; country.cc[2] = '\0';
        country.schan = 1;
        country.nchan = 13;
        country.max_tx_power = 80;
        country.policy = WIFI_COUNTRY_POLICY_MANUAL;
        esp_wifi_set_country(&country);
    }

    ap_mode_ = true;
    wifi_started_ = true;
    connected_ = false;
    reconnect_pending_ = false;

    // Ініціалізація AP→STA probe (якщо є збережені credentials)
    ap_sta_probing_        = false;
    ap_sta_probe_timer_    = AP_STA_PROBE_INITIAL_MS;
    ap_sta_probe_interval_ = AP_STA_PROBE_INITIAL_MS;
    ap_sta_probe_count_    = 0;

    // AP IP завжди 192.168.4.1
    strncpy(ip_str_, "192.168.4.1", sizeof(ip_str_));
    state_set("wifi.mode", "ap");
    state_set("wifi.ssid", ap_ssid);
    state_set("wifi.ip", ip_str_);
    state_set("wifi.connected", false);

    ESP_LOGI(TAG, "AP mode started: %s ch=%ld %s", ap_ssid, (long)ap_channel,
             wifi_config.ap.authmode == WIFI_AUTH_WPA2_PSK ? "WPA2" : "open");

    // Запускаємо mDNS в AP mode (клієнти AP можуть використовувати .local)
    start_mdns();
    return true;
}

// ── AP→STA Probe ─────────────────────────────────────────────────

void WiFiService::attempt_ap_sta_probe() {
    ap_sta_probe_count_++;
    ESP_LOGI(TAG, "AP→STA probe #%u (interval %lus)",
             ap_sta_probe_count_,
             (unsigned long)(ap_sta_probe_interval_ / 1000));

    // Встановити ДО mode switch — STA_START handler перевіряє цей прапорець
    ap_sta_probing_ = true;
    ap_sta_probe_timeout_ = AP_STA_PROBE_TIMEOUT_MS;

    ensure_sta_netif();
    esp_wifi_set_mode(WIFI_MODE_APSTA);

    // Config + connect ПІСЛЯ mode switch (STA_START не викликає auto-connect)
    wifi_config_t sta_cfg = {};
    strncpy(reinterpret_cast<char*>(sta_cfg.sta.ssid),
            ssid_, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy(reinterpret_cast<char*>(sta_cfg.sta.password),
            password_, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_connect();
}

void WiFiService::cancel_ap_sta_probe() {
    // Встановити прапорець ДО disconnect, щоб STA_DISCONNECTED event не викликав рекурсію
    ap_sta_probing_ = false;

    esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_AP);

    // Backoff: 30s → 60s → 120s → 240s → 300s (cap)
    ap_sta_probe_interval_ = std::min(ap_sta_probe_interval_ * 2, AP_STA_PROBE_MAX_MS);
    ap_sta_probe_timer_    = ap_sta_probe_interval_;

    ESP_LOGD(TAG, "AP→STA probe cancelled, next in %lus",
             (unsigned long)(ap_sta_probe_interval_ / 1000));
}

// ── mDNS ────────────────────────────────────────────────────────

void WiFiService::start_mdns() {
    if (mdns_started_) return;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char hostname[24];
    snprintf(hostname, sizeof(hostname), "modesp-%02x%02x", mac[4], mac[5]);

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return;
    }

    mdns_hostname_set(hostname);
    mdns_instance_name_set("ModESP Controller");

    // Реєструємо HTTP сервіс (для виявлення WebUI в мережі)
    mdns_txt_item_t txt_items[] = {
        {"path", "/"},
    };
    mdns_service_add(nullptr, "_http", "_tcp", 80, txt_items, 1);

    mdns_started_ = true;
    state_set("system.mdns_hostname", hostname);
    ESP_LOGI(TAG, "mDNS started: %s.local", hostname);
}

void WiFiService::stop_mdns() {
    if (!mdns_started_) return;
    mdns_free();
    mdns_started_ = false;
    ESP_LOGI(TAG, "mDNS stopped");
}

} // namespace modesp
