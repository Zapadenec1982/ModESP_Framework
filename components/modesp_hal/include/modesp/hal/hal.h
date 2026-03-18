/**
 * @file hal.h
 * @brief Hardware Abstraction Layer — GPIO initialization and resource lookup
 *
 * HAL initializes physical hardware (GPIO pins, buses) from BoardConfig
 * and provides resource lookup by hardware ID. Drivers use HAL resources
 * to access their hardware without knowing pin numbers.
 *
 * All resources are statically allocated — zero heap.
 */

#pragma once

#include "modesp/hal/hal_types.h"
#include "etl/string_view.h"

namespace modesp {

class HAL {
public:
    /// Initialize all hardware resources from board configuration.
    /// Sets GPIOs to safe state (relays OFF).
    bool init(const BoardConfig& config);

    /// Find a GPIO output resource by hardware ID (e.g. "relay_1")
    GpioOutputResource*  find_gpio_output(etl::string_view id);

    /// Find a OneWire bus resource by hardware ID (e.g. "ow_1")
    OneWireBusResource*  find_onewire_bus(etl::string_view id);

    /// Find a GPIO input resource by hardware ID (e.g. "din_1")
    GpioInputResource*   find_gpio_input(etl::string_view id);

    /// Find an ADC channel resource by hardware ID (e.g. "adc_1")
    AdcChannelResource*  find_adc_channel(etl::string_view id);

    /// Find an I2C expander resource by ID (e.g. "relay_exp")
    I2CExpanderResource*       find_i2c_expander(etl::string_view id);

    /// Find an expander output config by hardware ID (e.g. "relay_1")
    I2CExpanderOutputConfig*   find_expander_output(etl::string_view id);

    /// Find an expander input config by hardware ID (e.g. "din_1")
    I2CExpanderInputConfig*    find_expander_input(etl::string_view id);

    size_t gpio_output_count()  const { return gpio_output_count_; }
    size_t onewire_count()      const { return onewire_count_; }
    size_t gpio_input_count()   const { return gpio_input_count_; }
    size_t adc_count()          const { return adc_count_; }
    size_t i2c_expander_count() const { return i2c_expander_count_; }

private:
    etl::array<GpioOutputResource, MAX_RELAYS>         gpio_outputs_;
    etl::array<OneWireBusResource, MAX_ONEWIRE_BUSES>  onewire_buses_;
    etl::array<GpioInputResource, MAX_ADC_CHANNELS>    gpio_inputs_;
    etl::array<AdcChannelResource, MAX_ADC_CHANNELS>   adc_channels_;
    etl::array<I2CBusResource, MAX_I2C_BUSES>          i2c_buses_;
    etl::array<I2CExpanderResource, MAX_I2C_EXPANDERS> i2c_expanders_;
    etl::vector<I2CExpanderOutputConfig, MAX_EXPANDER_IOS> expander_outputs_;
    etl::vector<I2CExpanderInputConfig, MAX_EXPANDER_IOS>  expander_inputs_;

    size_t gpio_output_count_  = 0;
    size_t onewire_count_      = 0;
    size_t gpio_input_count_   = 0;
    size_t adc_count_          = 0;
    size_t i2c_bus_count_      = 0;
    size_t i2c_expander_count_ = 0;

    bool init_gpio_outputs(const BoardConfig& config);
    bool init_onewire(const BoardConfig& config);
    bool init_gpio_inputs(const BoardConfig& config);
    bool init_adc(const BoardConfig& config);
    bool init_i2c(const BoardConfig& config);
    bool init_i2c_expanders(const BoardConfig& config);
};

} // namespace modesp
