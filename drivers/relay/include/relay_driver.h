/**
 * @file relay_driver.h
 * @brief Relay actuator driver implementing IActuatorDriver
 *
 * Implementation:
 *   - GPIO output, active HIGH (configurable)
 *   - Minimum switch interval protection (compressor safety)
 *   - Tracks switch count
 *
 * Lifecycle:
 *   1. DriverManager calls configure() with role, GPIO, active_high, min_switch_ms
 *   2. DriverManager calls init()
 *   3. Main loop calls update(dt_ms) every cycle
 *   4. Business module calls set(true/false) to control relay
 */

#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "modesp/types.h"
#include "driver/gpio.h"
#include "etl/string.h"

class RelayDriver : public modesp::IActuatorDriver {
public:
    RelayDriver() = default;

    /// Configure before init (called by DriverManager)
    void configure(const char* role, gpio_num_t gpio, bool active_high, uint32_t min_switch_ms = 0);

    // ── IActuatorDriver interface ──
    bool init() override;
    void update(uint32_t dt_ms) override;
    bool set(bool state) override;
    bool get_state() const override { return relay_on_; }
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "relay"; }
    bool is_healthy() const override { return initialized_; }
    void emergency_stop() override;
    uint32_t switch_count() const override { return cycles_; }

private:
    void apply_gpio(bool on);

    etl::string<16> role_;
    gpio_num_t gpio_           = GPIO_NUM_NC;
    bool active_high_          = true;
    uint32_t min_switch_ms_    = 0;
    bool relay_on_             = false;
    bool initialized_          = false;
    bool configured_           = false;
    uint32_t cycles_           = 0;
    uint32_t since_last_switch_ms_ = modesp::TIMER_SATISFIED;
};
