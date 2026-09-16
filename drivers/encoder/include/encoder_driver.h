/**
 * @file encoder_driver.h
 * @brief Incremental rotary encoder (quadrature A/B) implementing ISensorDriver
 *
 * Decodes a 2-pin quadrature signal (CLK=A, DT=B) into a signed detent
 * counter. read() returns the accumulated position (in detents) as a float;
 * consumers track the delta between reads to detect rotation direction and
 * speed. The counter never resets, so no steps are lost between reads.
 *
 * The push-button of an "encoder with button" module is intentionally NOT
 * handled here — bind its SW pin to the digital_input driver as a separate
 * role. This driver stays a single-signal sensor, like every other.
 *
 * Lifecycle:
 *   1. DriverManager calls configure() with role, CLK/DT, steps_per_detent
 *   2. DriverManager calls init()
 *   3. Main loop calls update(dt_ms) every cycle (100 Hz) → quadrature decode
 *   4. Business module calls read(value) → accumulated detent count
 */

#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "driver/gpio.h"
#include "etl/string.h"

class EncoderDriver : public modesp::ISensorDriver {
public:
    EncoderDriver() = default;

    /// Configure before init (called by DriverManager factory)
    void configure(const char* role, gpio_num_t clk, gpio_num_t dt,
                   uint8_t steps_per_detent = 4, bool invert = false);

    // ── ISensorDriver interface ──
    bool init() override;
    void update(uint32_t dt_ms) override;
    bool read(float& value) override;
    bool is_healthy() const override { return initialized_; }
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "encoder"; }

private:
    /// Прочитати поточний 2-бітний стан квадратури: (CLK << 1) | DT
    uint8_t read_state() const;

    etl::string<16> role_;
    gpio_num_t clk_ = GPIO_NUM_NC;
    gpio_num_t dt_  = GPIO_NUM_NC;
    uint8_t steps_per_detent_ = 4;
    bool    invert_           = false;
    bool    initialized_      = false;
    bool    configured_       = false;

    uint8_t prev_state_ = 0;   // попередній стан квадратури
    int8_t  sub_count_  = 0;   // накопичені чвертькроки в межах одного детента
    int32_t position_   = 0;   // знаковий лічильник детентів (віддається у read)
};
