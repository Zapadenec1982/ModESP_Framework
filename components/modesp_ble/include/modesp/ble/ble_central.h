/**
 * @file ble_central.h
 * @brief BleCentral — registry of broadcast-BLE sensors keyed by MAC.
 *
 * The modesp_ble passive scanner decodes advertisement frames (pvvx/ATC Xiaomi
 * 0x181A, BTHome 0xFCD2) and pushes them here via dispatch() from the NimBLE host
 * task. BLE sensor drivers (hardware_type "ble", e.g. drivers/ble_xiaomi_th)
 * register their MAC at factory time and read the cached reading in
 * ISensorDriver::read(). Fixed pool, zero-heap.
 *
 * Layering: this is the bridge between modesp_ble (owns the radio) and the driver
 * layer. The driver depends on modesp_ble; modesp_ble does NOT depend on drivers.
 */
#pragma once

#include "sdkconfig.h"   // CONFIG_MODESP_BLE_* must be defined regardless of include order
                         // (this header is pulled in first by drivers, before any IDF header)

#if defined(CONFIG_MODESP_BLE_ENABLE) && defined(CONFIG_MODESP_BLE_CENTRAL)

#include <stdint.h>
#include <stddef.h>

namespace modesp {

/// Decoded broadcast reading, merged across alternating frames (BTHome temp/hum
/// frame vs voltage/flags frame — docs/ble/sensors_observer_spec.md §3c).
struct BleReading {
    float   temp_c   = 0.0f;  bool has_temp = false;
    float   hum_pct  = 0.0f;  bool has_hum  = false;
    int     batt_pct = -1;    // -1 = unknown
    int     batt_mv  = -1;    // -1 = unknown
    int8_t  rssi     = 127;   // 127 = unavailable
    int64_t last_us  = 0;     // esp_timer_get_time() of last update; 0 = never seen
};

class BleCentral {
public:
    static constexpr size_t MAX_DEVICES = 8;
    static BleCentral& instance();

    /// Driver-side (factory thread): claim/find a slot for this MAC (DISPLAY order,
    /// e.g. a4:c1:38:b4:dc:11). Returns the cache to read each cycle, or nullptr if
    /// the pool is full. Idempotent for the same MAC.
    const BleReading* register_mac(const uint8_t mac6_display[6]);

    /// Host-task side (GAP cb): merge a decoded frame for `mac6_le` (NimBLE
    /// addr.val, little-endian) into a REGISTERED slot. No-op for unregistered
    /// MACs. Zero-heap (only memcmp + field copies into the fixed pool).
    void dispatch(const uint8_t mac6_le[6], int8_t rssi,
                  bool has_temp, float temp_c, bool has_hum, float hum_pct,
                  int batt_pct, int batt_mv);

    size_t device_count() const { return count_; }

private:
    struct Slot { uint8_t mac_le[6]; bool used; BleReading r; };
    Slot   slots_[MAX_DEVICES] = {};
    size_t count_ = 0;
};

} // namespace modesp

#endif // CONFIG_MODESP_BLE_ENABLE && CONFIG_MODESP_BLE_CENTRAL
