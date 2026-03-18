/**
 * @file ntc_driver.h
 * @brief NTC thermistor driver via ADC implementing ISensorDriver
 *
 * Uses B-parameter equation:
 *   T = 1 / (1/T0 + (1/B)*ln(R_ntc/R_nominal)) - 273.15
 *
 * Voltage divider: VCC — R_series — ADC_PIN — NTC — GND
 *   R_ntc = R_series * adc_raw / (ADC_MAX - adc_raw)
 *
 * ESP32 ADC1: 12-bit (0-4095), atten=11 for 0-3.3V range
 *
 * Lifecycle:
 *   1. DriverManager calls configure() with role, GPIO, atten
 *   2. DriverManager calls init() → configures ADC oneshot
 *   3. Main loop calls update(dt_ms) every cycle
 *   4. Business module calls read(value) → temperature in °C
 */

#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "etl/string.h"

class NtcDriver : public modesp::ISensorDriver {
public:
    NtcDriver() = default;

    /// Configure before init
    void configure(const char* role, gpio_num_t gpio, uint8_t atten);

    // ── ISensorDriver interface ──
    bool init() override;
    void update(uint32_t dt_ms) override;
    bool read(float& value) override;
    bool is_healthy() const override;
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "ntc"; }
    uint32_t error_count() const override { return consecutive_errors_; }

private:
    float adc_to_temperature(int raw);
    static bool gpio_to_adc1_channel(gpio_num_t gpio, adc_channel_t& out);

    etl::string<16> role_;
    gpio_num_t gpio_       = GPIO_NUM_NC;
    uint8_t atten_         = 11;
    bool configured_       = false;

    // B-parameter defaults (10K NTC, 10K series)
    int beta_      = 3950;
    int r_series_  = 10000;
    int r_nominal_ = 10000;
    float offset_  = 0.0f;

    // ADC handle
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    adc_channel_t adc_channel_ = ADC_CHANNEL_0;

    // State
    float    current_temp_       = 0.0f;
    bool     has_valid_          = false;
    uint32_t ms_since_read_      = 0;
    uint32_t read_interval_ms_   = 1000;
    uint8_t  consecutive_errors_ = 0;

    static constexpr float MIN_VALID_TEMP  = -40.0f;
    static constexpr float MAX_VALID_TEMP  = 125.0f;
    static constexpr int   ADC_MAX_RAW     = 4095;
    static constexpr int   ADC_MIN_VALID   = 50;    // Короткозамкнений NTC
    static constexpr int   ADC_MAX_VALID   = 4045;  // Обірваний NTC
    static constexpr uint8_t MAX_CONSECUTIVE_ERRORS = 5;

    // T0 = 25°C в Кельвінах
    static constexpr float T0_KELVIN = 298.15f;
};
