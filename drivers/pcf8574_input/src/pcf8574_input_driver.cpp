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

// ═══════════════════════════════════════════════════════════════
// Driver factory + registration (optional via CONFIG_MODESP_DRIVER_PCF8574_INPUT)
// ═══════════════════════════════════════════════════════════════

#include "modesp/hal/driver_registry.h"
#include "modesp/hal/hal.h"
#include "etl/string_view.h"

namespace {
PCF8574InputDriver s_pcf_input_pool[modesp::MAX_SENSORS];
size_t             s_pcf_input_n = 0;

modesp::ISensorDriver* pcf8574_input_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_pcf_input_n >= modesp::MAX_SENSORS) {
        ESP_LOGE(TAG, "PCF input pool exhausted");
        return nullptr;
    }
    auto* in_cfg = hal.find_expander_input(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!in_cfg) {
        ESP_LOGE(TAG, "Expander input '%s' not found", b.hardware_id.c_str());
        return nullptr;
    }
    auto* expander = hal.find_i2c_expander(
        etl::string_view(in_cfg->expander_id.c_str(), in_cfg->expander_id.size()));
    if (!expander) {
        ESP_LOGE(TAG, "Expander '%s' not found", in_cfg->expander_id.c_str());
        return nullptr;
    }
    auto& drv = s_pcf_input_pool[s_pcf_input_n++];
    drv.configure(b.role.c_str(), expander, in_cfg->pin, in_cfg->invert);
    return &drv;
}
} // namespace

MODESP_REGISTER_SENSOR(pcf8574_input, &pcf8574_input_factory)
