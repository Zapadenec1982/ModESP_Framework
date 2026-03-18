/**
 * @file pcf8574_relay_driver.cpp
 * @brief PCF8574 relay driver implementation
 */

#include "pcf8574_relay_driver.h"
#include "esp_log.h"

static const char* TAG = "PCF8574Relay";

void PCF8574RelayDriver::configure(const char* role,
                                    modesp::I2CExpanderResource* expander,
                                    uint8_t pin, bool active_high) {
    role_ = role;
    expander_ = expander;
    pin_ = pin;
    active_high_ = active_high;
    configured_ = true;
}

bool PCF8574RelayDriver::init() {
    if (!configured_ || !expander_) return false;

    // Початковий стан OFF: встановити біт у безпечне значення
    if (active_high_) {
        expander_->output_state &= ~(1 << pin_);  // active-HIGH: OFF = bit LOW
    } else {
        expander_->output_state |= (1 << pin_);   // active-LOW: OFF = bit HIGH
    }
    expander_->write_state();

    relay_on_ = false;
    initialized_ = true;

    ESP_LOGI(TAG, "[%s] pin %d on '%s' (active_%s)",
             role_.c_str(), pin_, expander_->id.c_str(),
             active_high_ ? "HIGH" : "LOW");
    return true;
}

void PCF8574RelayDriver::update(uint32_t) {
    // PCF8574 relay не потребує periodic update.
    // Захист компресора від коротких циклів — на рівні EquipmentModule.
}

bool PCF8574RelayDriver::set(bool state) {
    if (state == relay_on_) return true;

    // Встановити/очистити біт у спільному output_state expander'а
    // state=true (ON): якщо active_high → set bit; якщо active_low → clear bit
    // state=false (OFF): якщо active_high → clear bit; якщо active_low → set bit
    if (state == active_high_) {
        expander_->output_state |= (1 << pin_);
    } else {
        expander_->output_state &= ~(1 << pin_);
    }

    if (!expander_->write_state()) {
        ESP_LOGE(TAG, "[%s] I2C write failed!", role_.c_str());
        return false;
    }

    relay_on_ = state;
    if (state) cycles_++;

    ESP_LOGI(TAG, "[%s] %s (byte=0x%02X)",
             role_.c_str(), state ? "ON" : "OFF", expander_->output_state);
    return true;
}

void PCF8574RelayDriver::emergency_stop() {
    if (relay_on_) {
        ESP_LOGW(TAG, "[%s] EMERGENCY STOP", role_.c_str());
        set(false);
    }
}
