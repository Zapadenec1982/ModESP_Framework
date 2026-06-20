/**
 * @file ble_xiaomi_th_driver.cpp
 * @brief Xiaomi LYWSD03MMC BLE-observer driver — factory + ISensorDriver view.
 *
 * The decode lives in modesp_ble (BleCentral, fed by the passive scanner). Here we
 * just resolve the binding's MAC (board.json ble_devices via HAL::find_ble_device),
 * register it with BleCentral, and expose one cached channel per binding.
 */
#include "ble_xiaomi_th_driver.h"
#include "modesp/hal/hal_types.h"        // complete modesp::Binding for apply_settings()
#include "modesp/hal/hal.h"
#include "modesp/hal/driver_registry.h"
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

modesp::ISensorDriver* ble_xiaomi_th_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_n >= MAX_BLE_TH) {
        ESP_LOGE(TAG, "pool exhausted");
        return nullptr;
    }
    auto* dev = hal.find_ble_device(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!dev) {
        ESP_LOGE(TAG, "BLE device '%s' not in board.json ble_devices", b.hardware_id.c_str());
        return nullptr;
    }
    uint8_t mac[6];
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
} // namespace

MODESP_REGISTER_SENSOR(ble_xiaomi_th, &ble_xiaomi_th_factory)
