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
