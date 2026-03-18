/**
 * @file hal_types.h
 * @brief HAL system limits, config structs, and resource types
 *
 * All data structures used by the HAL layer:
 *   - BoardConfig:  parsed from board.json at boot
 *   - BindingTable: parsed from bindings.json at boot
 *   - Resource structs: initialized hardware handles
 *
 * Zero heap allocation — all containers are fixed-size ETL.
 */

#pragma once

#include "etl/string.h"
#include "etl/vector.h"
#include "etl/array.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

namespace modesp {

// ═══════════════════════════════════════════════════════════════
// System limits
// ═══════════════════════════════════════════════════════════════

static constexpr size_t MAX_RELAYS         = 8;
static constexpr size_t MAX_ONEWIRE_BUSES  = 4;
static constexpr size_t MAX_ADC_CHANNELS   = 8;
static constexpr size_t MAX_PWM_CHANNELS   = 8;
static constexpr size_t MAX_BINDINGS       = 24;
static constexpr size_t MAX_SENSORS        = 8;
static constexpr size_t MAX_ACTUATORS      = 8;
static constexpr size_t MAX_I2C_BUSES      = 2;
static constexpr size_t MAX_I2C_EXPANDERS  = 4;
static constexpr size_t MAX_EXPANDER_IOS   = 16;   // Outputs + Inputs через expanders

// ═══════════════════════════════════════════════════════════════
// String types for IDs and names
// ═══════════════════════════════════════════════════════════════

using HalId         = etl::string<16>;
using Role          = etl::string<16>;
using DriverType    = etl::string<16>;
using ModuleName    = etl::string<16>;
using SensorAddress = etl::string<24>;  // "28:FF:AA:BB:CC:DD:EE:01"

// ═══════════════════════════════════════════════════════════════
// Board config structs (parsed from board.json)
// ═══════════════════════════════════════════════════════════════

struct GpioOutputConfig {
    HalId id;
    gpio_num_t gpio;
    bool active_high;
};

struct OneWireBusConfig {
    HalId id;
    gpio_num_t gpio;
};

struct GpioInputConfig {
    HalId id;
    gpio_num_t gpio;
    bool pull_up;       // true = pull-up, false = pull-down
};

struct AdcChannelConfig {
    HalId id;
    gpio_num_t gpio;
    uint8_t atten;      // 0=0dB, 2=2.5dB, 6=6dB, 11=11dB (0-3.3V)
};

struct I2CBusConfig {
    HalId id;               // "i2c_0"
    gpio_num_t sda;
    gpio_num_t scl;
    uint32_t freq_hz;       // 100000 (100kHz standard)
};

struct I2CExpanderConfig {
    HalId id;               // "relay_exp" або "input_exp"
    HalId bus_id;           // "i2c_0"
    HalId chip;             // "pcf8574"
    uint8_t address;        // 0x24
    uint8_t pin_count;      // 8
};

struct I2CExpanderOutputConfig {
    HalId id;               // "relay_1" (hardware_id для bindings)
    HalId expander_id;      // "relay_exp"
    uint8_t pin;            // 0-7
    bool active_high;       // false для KC868-A6 (relay active-LOW)
};

struct I2CExpanderInputConfig {
    HalId id;               // "din_1"
    HalId expander_id;      // "input_exp"
    uint8_t pin;            // 0-7
    bool invert;            // true для KC868-A6 (opto-isolated active-LOW)
};

struct BoardConfig {
    etl::string<24> board_name;
    etl::string<8>  board_version;
    etl::vector<GpioOutputConfig, MAX_RELAYS>                     gpio_outputs;
    etl::vector<OneWireBusConfig, MAX_ONEWIRE_BUSES>              onewire_buses;
    etl::vector<GpioInputConfig, MAX_ADC_CHANNELS>                gpio_inputs;
    etl::vector<AdcChannelConfig, MAX_ADC_CHANNELS>               adc_channels;
    etl::vector<I2CBusConfig, MAX_I2C_BUSES>                      i2c_buses;
    etl::vector<I2CExpanderConfig, MAX_I2C_EXPANDERS>             i2c_expanders;
    etl::vector<I2CExpanderOutputConfig, MAX_EXPANDER_IOS>        expander_outputs;
    etl::vector<I2CExpanderInputConfig, MAX_EXPANDER_IOS>         expander_inputs;
};

// ═══════════════════════════════════════════════════════════════
// Binding config (parsed from bindings.json)
// ═══════════════════════════════════════════════════════════════

struct Binding {
    HalId         hardware_id;
    Role          role;
    DriverType    driver_type;
    ModuleName    module_name;
    SensorAddress address;      // Опціональна ROM адреса (multi-sensor)
};

struct BindingTable {
    etl::vector<Binding, MAX_BINDINGS> bindings;
};

// ═══════════════════════════════════════════════════════════════
// HAL resources (initialized hardware)
// ═══════════════════════════════════════════════════════════════

struct GpioOutputResource {
    HalId id;
    gpio_num_t gpio;
    bool active_high;
    bool initialized = false;
};

struct OneWireBusResource {
    HalId id;
    gpio_num_t gpio;
    bool initialized = false;
};

struct GpioInputResource {
    HalId id;
    gpio_num_t gpio;
    bool pull_up;
    bool initialized = false;
};

struct AdcChannelResource {
    HalId id;
    gpio_num_t gpio;
    uint8_t atten;
    bool initialized = false;
};

struct I2CBusResource {
    HalId id;
    i2c_master_bus_handle_t bus_handle = nullptr;
    bool initialized = false;
};

struct I2CExpanderResource {
    HalId id;
    i2c_master_dev_handle_t dev_handle = nullptr;
    uint8_t address     = 0;
    uint8_t pin_count   = 8;
    uint8_t output_state = 0xFF;   // Все HIGH = все OFF (PCF8574 power-on default)
    bool initialized    = false;

    /// Записати output_state в PCF8574 (один I2C byte write)
    bool write_state();
    /// Прочитати всі 8 входів з PCF8574 (один I2C byte read)
    bool read_state(uint8_t& input_byte);
};

} // namespace modesp
