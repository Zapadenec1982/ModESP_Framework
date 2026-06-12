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
    uint8_t saved = expander_->output_state;
    if (active_high_) {
        expander_->output_state &= ~(1 << pin_);  // active-HIGH: OFF = bit LOW
    } else {
        expander_->output_state |= (1 << pin_);   // active-LOW: OFF = bit HIGH
    }
    if (!expander_->write_state()) {
        expander_->output_state = saved;  // відкат, не лишати неузгоджений біт
        ESP_LOGE(TAG, "[%s] init I2C write failed", role_.c_str());
        return false;
    }

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

    // Встановити/очистити біт у спільному output_state expander'а.
    // КРИТИЧНО: output_state — спільний байт усіх реле на цьому експандері.
    // При невдалому I2C-записі біт ОБОВ'ЯЗКОВО відкотити, інакше наступний
    // успішний write_state() будь-якого іншого реле виштовхне залиплий біт
    // і фізично перемкне це реле без відома софту.
    uint8_t saved = expander_->output_state;

    // state=true (ON): якщо active_high → set bit; якщо active_low → clear bit
    // state=false (OFF): якщо active_high → clear bit; якщо active_low → set bit
    if (state == active_high_) {
        expander_->output_state |= (1 << pin_);
    } else {
        expander_->output_state &= ~(1 << pin_);
    }

    if (!expander_->write_state()) {
        expander_->output_state = saved;  // ВІДКАТ спільного байта
        if (consecutive_errors_ < 255) consecutive_errors_++;
        ESP_LOGE(TAG, "[%s] I2C write failed! (errors=%u)",
                 role_.c_str(), consecutive_errors_);
        return false;
    }

    consecutive_errors_ = 0;
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

// ═══════════════════════════════════════════════════════════════
// Driver factory + registration (optional via CONFIG_MODESP_DRIVER_PCF8574_RELAY)
// ═══════════════════════════════════════════════════════════════

#include "modesp/hal/driver_registry.h"
#include "modesp/hal/hal.h"
#include "etl/string_view.h"

namespace {
PCF8574RelayDriver s_pcf_relay_pool[modesp::MAX_ACTUATORS];
size_t             s_pcf_relay_n = 0;

modesp::IActuatorDriver* pcf8574_relay_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_pcf_relay_n >= modesp::MAX_ACTUATORS) {
        ESP_LOGE(TAG, "PCF relay pool exhausted");
        return nullptr;
    }
    auto* out_cfg = hal.find_expander_output(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!out_cfg) {
        ESP_LOGE(TAG, "Expander output '%s' not found", b.hardware_id.c_str());
        return nullptr;
    }
    auto* expander = hal.find_i2c_expander(
        etl::string_view(out_cfg->expander_id.c_str(), out_cfg->expander_id.size()));
    if (!expander) {
        ESP_LOGE(TAG, "Expander '%s' not found", out_cfg->expander_id.c_str());
        return nullptr;
    }
    auto& drv = s_pcf_relay_pool[s_pcf_relay_n++];
    drv.configure(b.role.c_str(), expander, out_cfg->pin, out_cfg->active_high);
    return &drv;
}
} // namespace

MODESP_REGISTER_ACTUATOR(pcf8574_relay, &pcf8574_relay_factory)
