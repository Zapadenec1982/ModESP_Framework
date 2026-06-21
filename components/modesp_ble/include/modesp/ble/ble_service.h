/**
 * @file ble_service.h
 * @brief modesp_ble — single owner of the NimBLE host (BaseModule service).
 *
 * Phase 0: NimBLE host up + connectable advertising, coexisting with Wi-Fi STA.
 * Phase 1 (CONFIG_MODESP_BLE_GATT_SERVER): telemetry/control GATT server that
 *   mirrors SharedState — read+notify the generated MQTT publish keys, accept
 *   writes to the MQTT subscribe keys (validated via state_meta).
 * Phase 2 (CONFIG_MODESP_BLE_PROVISIONING): write-only GATT char to set Wi-Fi
 *   credentials into NVS + trigger wifi_service reconnect (headless setup).
 *
 * Layered design: this component is the ONLY place that touches esp_nimble_*.
 * BLE devices are drivers (hardware_type "ble"); app logic lives in modules.
 * See docs/ble/ipixel_nimble_spec.md and docs/ble/sensors_observer_spec.md.
 */
#pragma once

#include "modesp/base_module.h"

#if defined(CONFIG_MODESP_BLE_ENABLE)

// Forward-declare the NimBLE event struct to keep this header light.
struct ble_gap_event;

namespace modesp {

class WiFiService;  // injected for BLE provisioning (Phase 2)

class BleService : public BaseModule {
public:
    BleService();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_stop() override;

    // Invoked from the NimBLE host-sync C callback (stack ready).
    void handle_sync();

#if defined(CONFIG_MODESP_BLE_PROVISIONING)
    // Dependency injection: wifi_service for credential save + reconnect.
    void set_wifi(WiFiService* wifi) { wifi_ = wifi; }
#endif

#if defined(CONFIG_MODESP_BLE_GATT_SERVER)
    int  serialize_telemetry(char* buf, size_t size);   // JSON snapshot of publish keys
    void apply_command(const char* json, int len);      // parse+validate+state_set
    void on_gatt_subscribe(uint16_t conn_handle, uint16_t attr_handle, bool subscribed);
#endif

#if defined(CONFIG_MODESP_BLE_PROVISIONING)
    void apply_provisioning(const char* json, int len); // {"ssid","pass"} -> NVS + reconnect
#endif

#if defined(CONFIG_MODESP_BLE_GATT_SERVER) || defined(CONFIG_MODESP_BLE_PROVISIONING)
    void on_gatt_connect(uint16_t conn_handle);
    void on_gatt_disconnect();
#endif

#if defined(CONFIG_MODESP_BLE_CENTRAL)
    // NimBLE GAP scan callback (C-ABI). PUBLIC so the file-static observer-scan
    // starter (start_observer_scan) and BlePanel can pass it to ble_gap_disc.
    static int gap_scan_event(struct ble_gap_event* event, void* arg);
#endif

private:
    void start_advertising();
    static int gap_event(struct ble_gap_event* event, void* arg);

    bool synced_ = false;

#if defined(CONFIG_MODESP_BLE_GATT_SERVER) || defined(CONFIG_MODESP_BLE_PROVISIONING)
    void gatt_server_init();
    uint16_t conn_handle_ = 0xFFFF;  // BLE_HS_CONN_HANDLE_NONE
#endif

#if defined(CONFIG_MODESP_BLE_GATT_SERVER)
    bool     telemetry_subscribed_ = false;
    uint32_t notify_accum_ms_      = 0;
    char     last_telemetry_[512]  = {0};
#endif

#if defined(CONFIG_MODESP_BLE_PROVISIONING)
    WiFiService* wifi_ = nullptr;
#endif

#if defined(CONFIG_MODESP_BLE_CENTRAL)
    void start_scan();                                       // passive observer scan (member; calls start_observer_scan)
#endif
};

} // namespace modesp

#endif // CONFIG_MODESP_BLE_ENABLE
