/**
 * @file pcf8574_input_driver.h
 * @brief Digital input driver via I2C expander PCF8574
 *
 * Reads one bit from PCF8574 input byte. Multiple drivers on the same
 * expander each trigger a separate I2C read (6 reads per 100ms cycle
 * at 100kHz ~ 1.8ms total — acceptable overhead).
 */

#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "modesp/hal/hal_types.h"
#include "etl/string.h"

class PCF8574InputDriver : public modesp::ISensorDriver {
public:
    void configure(const char* role, modesp::I2CExpanderResource* expander,
                   uint8_t pin, bool invert = false);

    bool init() override;
    void update(uint32_t dt_ms) override;
    bool read(float& value) override;   // 1.0f = active, 0.0f = inactive
    bool is_healthy() const override {
        return initialized_ && expander_ != nullptr
            && consecutive_errors_ < MAX_CONSECUTIVE_ERRORS;
    }
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "pcf8574_input"; }
    uint32_t error_count() const override { return consecutive_errors_; }

private:
    etl::string<16> role_;
    modesp::I2CExpanderResource* expander_ = nullptr;
    uint8_t pin_       = 0;
    bool invert_       = false;
    bool state_        = false;
    bool initialized_  = false;
    bool configured_   = false;
    uint8_t consecutive_errors_ = 0;   // saturating; I2C read failures
    uint32_t poll_ms_  = 0;
    static constexpr uint32_t POLL_INTERVAL_MS = 100;
    static constexpr uint8_t MAX_CONSECUTIVE_ERRORS = 5;
};
