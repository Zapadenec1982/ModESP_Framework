/**
 * @file ble_nrf_tilt_driver.cpp
 * @brief nRF52832 tilt beacon — manufacturer-data decoder + ISensorDriver view.
 *
 * This driver owns BOTH the wire format and the channel view, and — unlike the shared
 * temp/hum/battery cache used by BTHome sensors — its OWN per-MAC reading cache, because
 * its fields are device-specific (tilt angle / tilted / battery / raw axes). The transport
 * (modesp_ble) only routes manufacturer-data frames to us; it knows no nRF format.
 *
 * The decoder also calls ble::report_seen("ble_nrf_tilt", angle, "°") so the transport's
 * MAC→type map auto-detects this device in the unified BLE scan.
 */
#include "ble_nrf_tilt_driver.h"
#include "modesp/hal/hal_types.h"        // complete modesp::Binding for apply_settings()
#include "modesp/hal/hal.h"
#include "modesp/hal/driver_registry.h"
#include "modesp/ble/adv_decoder.h"      // transport mfg-decoder registry + report_seen
#include "etl/string_view.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "ble_nrf_tilt";

BleNrfTiltDriver::Channel
BleNrfTiltDriver::channel_from_address(const char* a) {
    if (a) {
        if (strcmp(a, "tilted")  == 0) return Channel::TILTED;
        if (strcmp(a, "battery") == 0) return Channel::BATTERY;
        if (strcmp(a, "ax")      == 0) return Channel::AX;
        if (strcmp(a, "ay")      == 0) return Channel::AY;
        if (strcmp(a, "az")      == 0) return Channel::AZ;
    }
    return Channel::ANGLE;   // default + "angle"
}

void BleNrfTiltDriver::configure(const char* role, const NrfTiltReading* cache,
                                 Channel channel) {
    role_    = role;
    cache_   = cache;
    channel_ = channel;
}

void BleNrfTiltDriver::apply_settings(const modesp::Binding& b) {
    stale_ms_ = (uint32_t)b.setting_or("stale_ms", (float)stale_ms_);
}

bool BleNrfTiltDriver::init() {
    if (!cache_) {
        ESP_LOGE(TAG, "[%s] no cache slot bound", role_.c_str());
        return false;
    }
    ESP_LOGI(TAG, "[%s] Initialized (channel=%d, stale=%lu ms)",
             role_.c_str(), (int)channel_, (unsigned long)stale_ms_);
    return true;
}

void BleNrfTiltDriver::update(uint32_t) {}   // push model — decoder feeds the cache

bool BleNrfTiltDriver::read(float& value) {
    if (!cache_ || !is_healthy()) return false;
    switch (channel_) {
        case Channel::ANGLE:   if (cache_->angle_deg < 0) return false; value = (float)cache_->angle_deg; return true;
        case Channel::TILTED:  value = cache_->tilted ? 1.0f : 0.0f;    return true;
        case Channel::BATTERY: value = (float)cache_->batt_mv;          return true;
        case Channel::AX:      value = (float)cache_->ax;               return true;
        case Channel::AY:      value = (float)cache_->ay;               return true;
        case Channel::AZ:      value = (float)cache_->az;               return true;
    }
    return false;
}

bool BleNrfTiltDriver::is_healthy() const {
    if (!cache_ || cache_->last_us == 0) return false;
    return (esp_timer_get_time() - cache_->last_us) < (int64_t)stale_ms_ * 1000;
}

// ═══════════════════════════════════════════════════════════════
// Manufacturer-data decoder + own cache + factory + registration.
// Optional via CONFIG_MODESP_DRIVER_BLE_NRF_TILT (depends on MODESP_BLE_CENTRAL).
// ═══════════════════════════════════════════════════════════════

namespace {
// Two independently-sized pools: one instance PER BINDING (a device with 6 channels bound
// takes 6 slots), one cache entry PER DEVICE (collapsed by MAC).
constexpr size_t MAX_NRF_DEVICES  = 6;                    // distinct MACs (own reading cache)
constexpr size_t MAX_NRF_BINDINGS = MAX_NRF_DEVICES * 6;  // driver instances = devices * channels
BleNrfTiltDriver s_pool[MAX_NRF_BINDINGS];
size_t           s_n = 0;

// The driver's OWN per-MAC reading cache (device-specific fields — not the shared
// BleReading temp/hum). Keyed by MAC in NimBLE addr.val (little-endian) order.
NrfTiltReading s_cache[MAX_NRF_DEVICES];
size_t         s_cache_n = 0;

bool parse_mac(const char* s, uint8_t out[6]) {
    if (!s) return false;
    unsigned v[6];
    if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for (int i = 0; i < 6; i++) out[i] = (uint8_t)v[i];
    return true;
}

// Find-or-add a cache slot for a MAC (little-endian order). Both the decoder (writes) and
// the factory (binds the driver's view) resolve the SAME slot for a MAC.
NrfTiltReading* slot_for_mac(const uint8_t mac_le[6]) {
    for (size_t i = 0; i < s_cache_n; i++)
        if (s_cache[i].used && memcmp(s_cache[i].mac, mac_le, 6) == 0) return &s_cache[i];
    if (s_cache_n >= MAX_NRF_DEVICES) return nullptr;
    NrfTiltReading& s = s_cache[s_cache_n++];
    memset(&s, 0, sizeof(s));
    memcpy(s.mac, mac_le, 6);
    s.used = true; s.rssi = 127; s.angle_deg = -1;
    return &s;
}

// The nRF firmware writes a fixed protocol marker at md[8] (FRAME_MAGIC in its main.c) —
// company 0xFFFF is a shared test id, so this makes the match deterministic. MUST equal the
// nRF firmware's constant.
static constexpr uint8_t NRF_FRAME_MAGIC = 0xA7;

// Manufacturer-data decoder registered with the transport. Claims the HolyIOT/nRF tilt
// beacon: company 0xFFFF, ver=1, exactly 15 bytes, magic marker at md[8].
//   md incl. the 2-byte company prefix: [0..1]=FFFF, [2]=ver, [3]=flags, [4]=tilt_deg,
//   [5..6]=vbat LE, [7]=seq, [8]=MAGIC, [9..14]=ax,ay,az int16 LE.
bool nrf_tilt_decode(const uint8_t* mac, int8_t rssi,
                     uint16_t company, const uint8_t* md, uint16_t len) {
    if (company != 0xFFFF) return false;
    // Deterministic gate: exact length + ver + the firmware's magic marker → no unrelated
    // 0xFFFF beacon is ever claimed.
    if (len != 15 || md[2] != 0x01 || md[8] != NRF_FRAME_MAGIC) return false;   // not ours
    NrfTiltReading* s = slot_for_mac(mac);
    if (!s) return true;                              // ours, but cache pool full
    int angle    = (md[4] == 0xFF) ? -1 : (int)md[4];
    s->angle_deg = angle;
    s->tilted    = (md[3] & 0x01) != 0;
    s->batt_mv   = md[5] | (md[6] << 8);
    s->seq       = md[7];
    s->ax = (int16_t)(md[9]  | (md[10] << 8));
    s->ay = (int16_t)(md[11] | (md[12] << 8));
    s->az = (int16_t)(md[13] | (md[14] << 8));
    s->rssi    = rssi;
    s->last_us = esp_timer_get_time();
    // Identify + summarise for the manual scan: type + a short current-readings string so
    // the operator can tell two nRF sensors apart ("45° 2900mV" vs "12° 3010mV"; tilt one
    // and watch which row's angle changes live).
    char sum[24];
    if (angle >= 0) snprintf(sum, sizeof(sum), "%d\xC2\xB0%s %dmV", angle, s->tilted ? "!" : "", s->batt_mv);
    else            snprintf(sum, sizeof(sum), "--%s %dmV", s->tilted ? "!" : "", s->batt_mv);
    modesp::ble::report_seen(mac, rssi, "ble_nrf_tilt", sum);
    return true;
}

modesp::ISensorDriver* ble_nrf_tilt_factory(const modesp::Binding& b, modesp::HAL& hal) {
    // Decoder is registered at BOOT (see the register hook below), not here — so an unbound
    // nRF device is already visible in the unified scan before any binding exists.
    if (s_n >= MAX_NRF_BINDINGS) {
        ESP_LOGE(TAG, "instance pool exhausted");
        return nullptr;
    }
    // Device identity comes from the merged registry (devices.json ∪ board.json) — never
    // from the role binding. Resolve id → MAC, then reverse display order → NimBLE addr.val
    // (little-endian) so the factory and the decoder key the SAME cache slot.
    auto* dev = hal.find_ble_device(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!dev) {
        ESP_LOGE(TAG, "role '%s': hw '%s' not in board.json nor devices.json",
                 b.role.c_str(), b.hardware_id.c_str());
        return nullptr;
    }
    uint8_t disp[6], le[6];
    if (!parse_mac(dev->mac.c_str(), disp)) {
        ESP_LOGE(TAG, "bad MAC '%s' for '%s'", dev->mac.c_str(), b.hardware_id.c_str());
        return nullptr;
    }
    for (int i = 0; i < 6; i++) le[i] = disp[5 - i];   // display → addr.val (LE)

    NrfTiltReading* slot = slot_for_mac(le);
    if (!slot) {
        ESP_LOGE(TAG, "cache pool full");
        return nullptr;
    }
    auto& drv = s_pool[s_n++];
    drv.configure(b.role.c_str(), slot,
                  BleNrfTiltDriver::channel_from_address(
                      b.address.empty() ? "" : b.address.c_str()));
    drv.apply_settings(b);
    return &drv;
}

} // namespace

// Register hook (called at boot by the generated register-all). Registers the factory AND
// the manufacturer-data decoder — the latter at BOOT so an unbound nRF beacon is visible in
// the unified GET /api/ble/scan before any binding exists (you subscribe it, then bind).
// Nearby nRF devices are listed by the unified scan, not a per-driver discovery function.
extern "C" void modesp_register_driver_ble_nrf_tilt(void) {
    modesp::DriverRegistry::register_sensor("ble_nrf_tilt", &ble_nrf_tilt_factory);
    modesp::ble::register_adv_mfg_decoder(&nrf_tilt_decode);
}
