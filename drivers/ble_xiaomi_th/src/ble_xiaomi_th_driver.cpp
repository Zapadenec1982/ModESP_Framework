/**
 * @file ble_xiaomi_th_driver.cpp
 * @brief Xiaomi LYWSD03MMC BLE-observer driver — adv decoders + ISensorDriver view.
 *
 * This driver owns BOTH the wire format and the channel view:
 *   1. Advertisement decoders (pvvx/ATC 0x181A, BTHome v2 0xFCD2) registered with
 *      the modesp_ble transport (adv_decoder.h). modesp_ble owns the radio + passive
 *      scan but knows no device format; it hands each 16-bit service-data frame to
 *      every registered decoder. Our decoders parse this sensor's bytes and publish
 *      readings via ble::report_sensor() → BleCentral cache, keyed by MAC.
 *   2. ISensorDriver view: resolve the binding's MAC (board.json ble_devices via
 *      HAL::find_ble_device), register it with BleCentral, expose one cached channel.
 *
 * Byte layouts: docs/ble/sensors_observer_spec.md.
 */
#include "ble_xiaomi_th_driver.h"
#include "modesp/hal/hal_types.h"        // complete modesp::Binding for apply_settings()
#include "modesp/hal/hal.h"
#include "modesp/hal/driver_registry.h"
#include "modesp/ble/adv_decoder.h"      // transport decoder-registry + report/log API
#include "etl/string_view.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "ble_xiaomi_th";

BleXiaomiThDriver::Channel
BleXiaomiThDriver::channel_from_address(const char* a) {
    if (a && strcmp(a, "humidity") == 0) return Channel::HUMIDITY;
    if (a && strcmp(a, "battery")  == 0) return Channel::BATTERY;
    return Channel::TEMPERATURE;   // default + "temperature"
}

void BleXiaomiThDriver::configure(const char* role, const modesp::BleReading* cache,
                                  Channel channel) {
    role_    = role;
    cache_   = cache;
    channel_ = channel;
}

void BleXiaomiThDriver::apply_settings(const modesp::Binding& b) {
    stale_ms_ = (uint32_t)b.setting_or("stale_ms", (float)stale_ms_);
}

bool BleXiaomiThDriver::init() {
    if (!cache_) {
        ESP_LOGE(TAG, "[%s] no BLE cache bound", role_.c_str());
        return false;
    }
    ESP_LOGI(TAG, "[%s] Initialized (channel=%d, stale=%lu ms)",
             role_.c_str(), (int)channel_, (unsigned long)stale_ms_);
    return true;
}

void BleXiaomiThDriver::update(uint32_t) {}   // push model — BleCentral fed by scanner

bool BleXiaomiThDriver::read(float& value) {
    if (!cache_ || !is_healthy()) return false;
    switch (channel_) {
        case Channel::TEMPERATURE: if (!cache_->has_temp) return false; value = cache_->temp_c;  return true;
        case Channel::HUMIDITY:    if (!cache_->has_hum)  return false; value = cache_->hum_pct; return true;
        case Channel::BATTERY:     if (cache_->batt_pct < 0) return false; value = (float)cache_->batt_pct; return true;
    }
    return false;
}

bool BleXiaomiThDriver::is_healthy() const {
    if (!cache_ || cache_->last_us == 0) return false;
    return (esp_timer_get_time() - cache_->last_us) < (int64_t)stale_ms_ * 1000;
}

// ═══════════════════════════════════════════════════════════════
// Driver factory + registration (optional via CONFIG_MODESP_DRIVER_BLE_XIAOMI_TH)
//
// The MAC comes from board.json ble_devices (HAL::find_ble_device by hardware_id);
// several roles may bind the same device (multiple_per_bus) — each registers the
// same MAC with BleCentral (idempotent) and views a different channel via `address`.
// ═══════════════════════════════════════════════════════════════

namespace {
constexpr size_t MAX_BLE_TH = 8;
BleXiaomiThDriver s_pool[MAX_BLE_TH];
size_t            s_n = 0;

bool parse_mac(const char* s, uint8_t out[6]) {
    if (!s) return false;
    unsigned v[6];
    if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for (int i = 0; i < 6; i++) out[i] = (uint8_t)v[i];
    return true;
}

// ── Advertisement decoders (registered with the modesp_ble transport) ──────────
// The transport calls each per 16-bit service-data frame; a decoder returns true
// only for the UUID/format it owns. Values are published via ble::report_sensor;
// a recognized UUID with an unexpected length is raw-logged (ble::log_raw) for
// format discovery, still counting as "claimed" (return true) so the transport
// treats the frame as ours.

// pvvx / ATC custom firmware for Xiaomi LYWSD03MMC — service UUID 0x181A.
bool xiaomi_181a_decode(const uint8_t* mac, int8_t rssi,
                        uint16_t uuid, const uint8_t* sd, uint16_t len) {
    if (uuid != 0x181A) return false;
    if (len == 17) {                       // pvvx-custom, LITTLE-ENDIAN, all fields
        int16_t  t  = (int16_t)(sd[8]  | (sd[9]  << 8));
        uint16_t h  = (uint16_t)(sd[10] | (sd[11] << 8));
        uint16_t mv = (uint16_t)(sd[12] | (sd[13] << 8));
        modesp::ble::report_sensor(mac, rssi, "pvvx", true, t / 100.0f, true, h / 100.0f, sd[14], mv);
    } else if (len == 15) {                // ATC1441, BIG-ENDIAN numerics
        int16_t  t  = (int16_t)((sd[8] << 8) | sd[9]);
        uint16_t mv = (uint16_t)((sd[12] << 8) | sd[13]);
        modesp::ble::report_sensor(mac, rssi, "atc", true, t / 10.0f, true, (float)sd[10], sd[11], mv);
    } else {
        modesp::ble::log_raw(mac, rssi, uuid, sd, len);   // our UUID, unknown length
    }
    return true;
}

// BTHome v2 — service UUID 0xFCD2. LE TLV from offset 3; frames alternate fields.
bool bthome_fcd2_decode(const uint8_t* mac, int8_t rssi,
                        uint16_t uuid, const uint8_t* sd, uint16_t len) {
    if (uuid != 0xFCD2) return false;
    bool ht = false, hh = false; float t = 0, h = 0; int bp = -1, mv = -1;
    bool ok = true, any = false;
    uint16_t i = 3;                        // skip uuid(2) + device-info(1)
    while (i < len) {
        uint8_t id = sd[i++];
        if      (id == 0x02 && i + 2 <= len) { t = (int16_t)(sd[i] | (sd[i+1] << 8)) / 100.0f; ht = true; any = true; i += 2; }
        else if (id == 0x03 && i + 2 <= len) { h = (uint16_t)(sd[i] | (sd[i+1] << 8)) / 100.0f; hh = true; any = true; i += 2; }
        else if (id == 0x01 && i + 1 <= len) { bp = sd[i]; any = true; i += 1; }
        else if (id == 0x0C && i + 2 <= len) { mv = (uint16_t)(sd[i] | (sd[i+1] << 8)); any = true; i += 2; }
        else if (id == 0x00 && i + 1 <= len) { i += 1; }                                // packet id (1B)
        else if ((id == 0x0F || id == 0x10 || id == 0x11) && i + 1 <= len) { i += 1; }   // binary sensors (1B) — skip
        else { ok = false; break; }                                                     // unknown id — size unknown, stop
    }
    if (any) modesp::ble::report_sensor(mac, rssi, "bthome", ht, t, hh, h, bp, mv);
    else if (!ok) modesp::ble::log_raw(mac, rssi, uuid, sd, len);   // malformed — reveal format
    return any || ok;   // claimed unless nothing parsed AND the frame was malformed
}

modesp::ISensorDriver* ble_xiaomi_th_factory(const modesp::Binding& b, modesp::HAL& hal) {
    // Register this sensor's adv decoders with the transport (idempotent). Done at
    // factory time so decoders are live before the scanner starts; harmless if
    // several bindings share the driver.
    modesp::ble::register_adv_decoder(&xiaomi_181a_decode);
    modesp::ble::register_adv_decoder(&bthome_fcd2_decode);

    if (s_n >= MAX_BLE_TH) {
        ESP_LOGE(TAG, "pool exhausted");
        return nullptr;
    }
    // Resolve the MAC via the device id (binding.hardware). find_ble_device merges the
    // runtime registry (/data/devices.json — user-subscribed devices) with board.json
    // ble_devices, so a subscribed device resolves identically to a factory one — the
    // driver never learns the identity's origin. Device identity lives on the DEVICE,
    // never on the role binding.
    uint8_t mac[6];
    auto* dev = hal.find_ble_device(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!dev) {
        ESP_LOGE(TAG, "role '%s': hw '%s' not in board.json nor devices.json",
                 b.role.c_str(), b.hardware_id.c_str());
        return nullptr;
    }
    if (!parse_mac(dev->mac.c_str(), mac)) {
        ESP_LOGE(TAG, "bad MAC '%s' for '%s'", dev->mac.c_str(), b.hardware_id.c_str());
        return nullptr;
    }
    const modesp::BleReading* cache = modesp::BleCentral::instance().register_mac(mac);
    if (!cache) {
        ESP_LOGE(TAG, "BleCentral pool full");
        return nullptr;
    }
    auto& drv = s_pool[s_n++];
    drv.configure(b.role.c_str(), cache,
                  BleXiaomiThDriver::channel_from_address(
                      b.address.empty() ? "" : b.address.c_str()));
    drv.apply_settings(b);
    return &drv;
}

// Discovery: snapshot the transport's recently-seen advertisers so the UI can list
// nearby BLE sensors (MAC + live temperature + signal) to subscribe to. hw_id is
// ignored — a BLE scan is radio-wide (the passive observer runs continuously), not
// per-bus. out[].address = display MAC; value = temperature; rssi = signal.
int ble_xiaomi_th_discovery(modesp::HAL& /*hal*/, const char* /*hw_id*/,
                            modesp::DiscoveredDevice* out, size_t max) {
    if (!out || max == 0) return 0;
    modesp::ble::BleSeenDevice seen[16];
    size_t n = modesp::ble::list_seen(seen, sizeof(seen) / sizeof(seen[0]));
    if (n > max) n = max;
    for (size_t i = 0; i < n; i++) {
        const auto& s = seen[i];
        snprintf(out[i].address, sizeof(out[i].address),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 s.mac[0], s.mac[1], s.mac[2], s.mac[3], s.mac[4], s.mac[5]);
        out[i].has_value = s.has_temp;
        out[i].value     = s.has_temp ? s.temp_c : 0.0f;
        out[i].rssi      = s.rssi;
    }
    return static_cast<int>(n);
}
} // namespace

MODESP_REGISTER_SENSOR_WITH_DISCOVERY(ble_xiaomi_th, &ble_xiaomi_th_factory, &ble_xiaomi_th_discovery)
