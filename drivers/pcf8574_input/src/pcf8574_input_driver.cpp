/**
 * @file pcf8574_input_driver.cpp
 * @brief PCF8574 digital input driver implementation
 */

#include "pcf8574_input_driver.h"
#include "esp_log.h"

static const char* TAG = "PCF8574Input";

void PCF8574InputDriver::configure(const char* role,
                                    modesp::I2CExpanderResource* expander,
                                    uint8_t pin, bool invert) {
    role_ = role;
    expander_ = expander;
    pin_ = pin;
    invert_ = invert;
    configured_ = true;
}

bool PCF8574InputDriver::init() {
    if (!configured_ || !expander_) return false;
    initialized_ = true;
    ESP_LOGI(TAG, "[%s] pin %d on '%s' (invert=%s)",
             role_.c_str(), pin_, expander_->id.c_str(),
             invert_ ? "yes" : "no");
    return true;
}

void PCF8574InputDriver::update(uint32_t dt_ms) {
    poll_ms_ += dt_ms;
    if (poll_ms_ < POLL_INTERVAL_MS) return;
    poll_ms_ = 0;

    uint8_t input_byte = 0xFF;
    if (expander_->read_state(input_byte)) {
        // Читаємо raw біт з PCF8574
        bool raw_bit = (input_byte & (1 << pin_)) != 0;
        // Застосовуємо інверсію: KC868-A6 opto-inputs active-LOW → invert=true
        state_ = invert_ ? !raw_bit : raw_bit;
    }
}

bool PCF8574InputDriver::read(float& value) {
    if (!initialized_) return false;
    value = state_ ? 1.0f : 0.0f;
    return true;
}
