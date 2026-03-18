/**
 * @file pcf8574_relay_driver.h
 * @brief Relay driver via I2C expander PCF8574
 *
 * Each driver instance controls one bit of the shared PCF8574 output byte.
 * Multiple drivers on the same expander share output_state through
 * I2CExpanderResource — safe because update runs in single-threaded main loop.
 */

#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "modesp/hal/hal_types.h"
#include "etl/string.h"

class PCF8574RelayDriver : public modesp::IActuatorDriver {
public:
    void configure(const char* role, modesp::I2CExpanderResource* expander,
                   uint8_t pin, bool active_high);

    bool init() override;
    void update(uint32_t dt_ms) override;
    bool set(bool state) override;
    bool get_state() const override { return relay_on_; }
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "pcf8574_relay"; }
    bool is_healthy() const override { return initialized_ && expander_ != nullptr; }
    void emergency_stop() override;
    uint32_t switch_count() const override { return cycles_; }

private:
    etl::string<16> role_;
    modesp::I2CExpanderResource* expander_ = nullptr;
    uint8_t pin_       = 0;
    bool active_high_  = false;
    bool relay_on_     = false;
    bool initialized_  = false;
    bool configured_   = false;
    uint32_t cycles_   = 0;
};
