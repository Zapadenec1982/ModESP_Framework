/**
 * @file relay_driver.cpp
 * @brief Relay driver implementation with switch protection
 *
 * Algorithm:
 *   1. Business module calls set(true/false) to request state change
 *   2. Driver checks min_switch_ms before allowing switch
 *   3. emergency_stop() forces relay OFF immediately (ignores interval)
 *   4. GPIO level depends on active_high configuration
 */

#include "relay_driver.h"
#include "esp_log.h"

static const char* TAG = "Relay";

// ═══════════════════════════════════════════════════════════════
// Configure (called by DriverManager before init)
// ═══════════════════════════════════════════════════════════════

void RelayDriver::configure(const char* role, gpio_num_t gpio, bool active_high, uint32_t min_switch_ms) {
    role_ = role;
    gpio_ = gpio;
    active_high_ = active_high;
    min_switch_ms_ = min_switch_ms;
    configured_ = true;
}

// ═══════════════════════════════════════════════════════════════
// IActuatorDriver lifecycle
// ═══════════════════════════════════════════════════════════════

bool RelayDriver::init() {
    if (!configured_) {
        ESP_LOGE(TAG, "Driver not configured — call configure() first");
        return false;
    }

    // GPIO already configured by HAL — just set safe state
    apply_gpio(false);
    relay_on_ = false;
    initialized_ = true;

    ESP_LOGI(TAG, "[%s] Relay initialized (GPIO=%d, active_%s)",
             role_.c_str(), gpio_,
             active_high_ ? "HIGH" : "LOW");
    return true;
}

void RelayDriver::update(uint32_t dt_ms) {
    since_last_switch_ms_ += dt_ms;
}

bool RelayDriver::set(bool state) {
    // No change needed
    if (state == relay_on_) return true;

    // Check minimum switch interval
    if (min_switch_ms_ > 0 && since_last_switch_ms_ < min_switch_ms_) {
        ESP_LOGD(TAG, "[%s] Switch rejected — min interval (%lu/%lu ms)",
                 role_.c_str(), since_last_switch_ms_, min_switch_ms_);
        return false;
    }

    // Apply state change
    relay_on_ = state;
    apply_gpio(state);
    since_last_switch_ms_ = 0;

    // Count ON transitions
    if (state) {
        cycles_++;
    }

    ESP_LOGI(TAG, "[%s] Relay %s (cycle #%lu)",
             role_.c_str(),
             relay_on_ ? "ON" : "OFF",
             cycles_);
    return true;
}

void RelayDriver::emergency_stop() {
    if (relay_on_) {
        ESP_LOGW(TAG, "[%s] EMERGENCY STOP — forcing relay OFF", role_.c_str());
        relay_on_ = false;
        apply_gpio(false);
        since_last_switch_ms_ = 0;
    }
}

// ═══════════════════════════════════════════════════════════════
// GPIO control
// ═══════════════════════════════════════════════════════════════

void RelayDriver::apply_gpio(bool on) {
    int level = on ? (active_high_ ? 1 : 0) : (active_high_ ? 0 : 1);
    gpio_set_level(gpio_, level);
}

// ═══════════════════════════════════════════════════════════════
// Driver factory + registration (optional via CONFIG_MODESP_DRIVER_RELAY)
// ═══════════════════════════════════════════════════════════════

#include "modesp/hal/driver_registry.h"
#include "modesp/hal/hal.h"
#include "etl/string_view.h"

namespace {
RelayDriver s_relay_pool[modesp::MAX_ACTUATORS];
size_t      s_relay_n = 0;

modesp::IActuatorDriver* relay_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_relay_n >= modesp::MAX_ACTUATORS) {
        ESP_LOGE(TAG, "Relay pool exhausted");
        return nullptr;
    }
    auto* gpio_res = hal.find_gpio_output(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!gpio_res) {
        ESP_LOGE(TAG, "GPIO output '%s' not found in HAL", b.hardware_id.c_str());
        return nullptr;
    }
    auto& drv = s_relay_pool[s_relay_n++];
    // min_switch_ms = 0; compressor anti-short-cycle is enforced in EquipmentBase.
    drv.configure(b.role.c_str(), gpio_res->gpio, gpio_res->active_high, 0);
    return &drv;
}
} // namespace

MODESP_REGISTER_ACTUATOR(relay, &relay_factory)
