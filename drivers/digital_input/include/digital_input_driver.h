/**
 * @file digital_input_driver.h
 * @brief GPIO digital input driver implementing ISensorDriver
 *
 * Reads a GPIO pin state with debounce filtering.
 * Returns 0.0 (LOW/inactive) or 1.0 (HIGH/active) via read().
 * Supports pull-up/pull-down and invert logic (NO/NC contacts).
 *
 * Lifecycle:
 *   1. DriverManager calls configure() with role, GPIO, pull_up
 *   2. DriverManager calls init()
 *   3. Main loop calls update(dt_ms) every cycle
 *   4. Business module calls read(value) → 0.0 or 1.0
 */

#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "driver/gpio.h"
#include "etl/string.h"

class DigitalInputDriver : public modesp::ISensorDriver {
public:
    DigitalInputDriver() = default;

    /// Configure before init (called by DriverManager)
    void configure(const char* role, gpio_num_t gpio, bool pull_up = true, bool invert = false);

    // ── ISensorDriver interface ──
    bool init() override;
    void update(uint32_t dt_ms) override;
    bool read(float& value) override;
    bool is_healthy() const override { return initialized_; }
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "digital_input"; }

private:
    etl::string<16> role_;
    gpio_num_t gpio_       = GPIO_NUM_NC;
    bool pull_up_          = true;
    bool invert_           = false;
    bool initialized_      = false;
    bool configured_       = false;

    // Debounce
    static constexpr uint32_t DEBOUNCE_MS = 50;
    bool     raw_state_       = false;
    bool     debounced_state_ = false;
    uint32_t stable_ms_       = 0;
};
