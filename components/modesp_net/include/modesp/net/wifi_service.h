/**
 * @file wifi_service.h
 * @brief WiFi station/AP management service
 *
 * Connects to a configured AP (STA mode) or creates a fallback AP
 * if no credentials are stored in NVS. Handles reconnection with
 * exponential backoff.
 */

#pragma once

#include "modesp/base_module.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

namespace modesp {

class WiFiService : public BaseModule {
public:
    WiFiService() : BaseModule("wifi", ModulePriority::HIGH) {}

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_stop() override;

    // Current state
    bool is_connected() const { return connected_; }
    const char* ip_address() const { return ip_str_; }
    bool is_ap_mode() const { return ap_mode_; }

    // Control
    bool save_credentials(const char* ssid, const char* password);
    void request_reconnect();

    // Scan
    static constexpr size_t MAX_SCAN_RESULTS = 20;
    struct ScanResult {
        char ssid[33];
        int8_t rssi;
        uint8_t authmode;  // wifi_auth_mode_t
    };
    bool start_scan();
    int  get_scan_results(ScanResult* out, size_t max_results);
    bool is_scan_done() const { return scan_done_; }

private:
    bool connected_ = false;
    bool ap_mode_ = false;
    bool wifi_started_ = false;
    char ip_str_[16] = {};
    char ssid_[33] = {};
    char password_[65] = {};

    // Netif handles (створюються один раз)
    esp_netif_t* sta_netif_ = nullptr;
    esp_netif_t* ap_netif_  = nullptr;

    // mDNS
    bool mdns_started_ = false;

    // Scan state
    bool scan_done_ = false;
    bool restore_ap_after_scan_ = false;  // AP mode restore deferred until results read

    // Deferred reconnect (give HTTP response time to reach client)
    bool deferred_reconnect_ = false;
    uint32_t deferred_reconnect_timer_ = 0;
    static constexpr uint32_t DEFERRED_RECONNECT_DELAY_MS = 1500;

    // Reconnect logic
    uint32_t reconnect_timer_ = 0;
    uint32_t reconnect_interval_ = 2000;
    uint8_t  retry_count_ = 0;
    bool     reconnect_pending_ = false;
    static constexpr uint8_t MAX_RETRIES = 3;
    static constexpr uint32_t MAX_BACKOFF_MS = 32000;

    // RSSI update throttle
    uint32_t rssi_timer_ = 0;
    static constexpr uint32_t RSSI_INTERVAL_MS = 10000;

    // STA watchdog: restart якщо disconnect сумарно > 10 хв
    uint32_t disconnect_accum_ms_ = 0;
    uint32_t stable_timer_ms_ = 0;
    static constexpr uint32_t WIFI_MAX_DISCONNECT_MS = 600000;   // 10 min
    static constexpr uint32_t WIFI_STABLE_RESET_MS = 3600000;    // 1 hour

    // AP→STA periodic probe: спроби STA підключення з AP mode
    bool     ap_sta_probing_        = false;
    uint32_t ap_sta_probe_timer_    = 0;
    uint32_t ap_sta_probe_interval_ = 30000;
    uint32_t ap_sta_probe_timeout_  = 0;
    uint8_t  ap_sta_probe_count_    = 0;
    static constexpr uint32_t AP_STA_PROBE_INITIAL_MS  = 30000;   // 30s
    static constexpr uint32_t AP_STA_PROBE_MAX_MS      = 300000;  // 5 min
    static constexpr uint32_t AP_STA_PROBE_TIMEOUT_MS  = 15000;   // 15s
    static constexpr uint32_t AP_STA_PROBE_HEAP_MIN    = 51200;   // 50KB

    bool load_credentials();
    bool start_sta();
    bool start_ap();
    void ensure_sta_netif();
    void ensure_ap_netif();
    void start_mdns();
    void stop_mdns();
    void attempt_ap_sta_probe();
    void cancel_ap_sta_probe();

    // ESP event handler (static because ESP-IDF API requires function pointer)
    static void event_handler(void* arg, esp_event_base_t base,
                              int32_t event_id, void* event_data);
    void handle_wifi_event(int32_t event_id, void* event_data);
    void handle_ip_event(int32_t event_id, void* event_data);
};

} // namespace modesp
