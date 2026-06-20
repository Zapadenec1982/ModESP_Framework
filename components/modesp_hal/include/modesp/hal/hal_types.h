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
static constexpr size_t MAX_I2C_DISPLAYS   = 2;
static constexpr size_t MAX_I2S_BUSES      = 1;   // I2S TX (аудіо: MAX98357A тощо)
static constexpr size_t MAX_EXPANDER_IOS   = 16;   // Outputs + Inputs через expanders
static constexpr size_t MAX_BINDING_SETTINGS = 6;  // per-binding driver settings

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
    bool invert = false; // true = NC contact / active-low (door, limit switch)
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

/// I²C-дисплей (multi-address пристрій на шині). На відміну від expander —
/// чіп сам володіє своїми адресами (банками), HAL лише надає bus_handle.
/// Бекенд (driver у bindings) обирає, як саме керувати чіпом.
struct I2CDisplayConfig {
    HalId   id;             // "disp_0" (hardware_id для bindings)
    HalId   bus_id;         // "i2c_0" — посилання на i2c_buses
    HalId   chip;           // "amt630a"
    uint8_t cols;           // символьна сітка OSD
    uint8_t rows;
};

/// I²S-шина (аудіо-вихід: MAX98357A Class-D amp тощо). HAL зберігає лише конфіг
/// (піни + sample_rate + SD-пін) — фактичний I2S-канал створює драйвер, бо handle
/// (i2s_chan_handle_t) разом із DMA-буферами та фоновим audio-таском належить
/// драйверу (як ADC oneshot). HAL лишається без залежності від driver/i2s_std.h.
struct I2SBusConfig {
    HalId      id;                       // "i2s_0" (hardware_id для bindings)
    gpio_num_t bclk           = GPIO_NUM_NC;  // bit clock (BCLK/SCK)
    gpio_num_t ws             = GPIO_NUM_NC;  // word select (LRCLK/WS)
    gpio_num_t dout           = GPIO_NUM_NC;  // serial data → DIN підсилювача
    gpio_num_t sd             = GPIO_NUM_NC;  // SD/shutdown (hardware-mute), -1 якщо підтягнутий
    uint32_t   sample_rate_hz = 16000;        // частота дискретизації (аларм-тони: 16k достатньо)
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
    etl::vector<I2CDisplayConfig, MAX_I2C_DISPLAYS>               i2c_displays;
    etl::vector<I2SBusConfig, MAX_I2S_BUSES>                     i2s_buses;
};

// ═══════════════════════════════════════════════════════════════
// Binding config (parsed from bindings.json)
// ═══════════════════════════════════════════════════════════════

/// One per-binding driver setting (e.g. "beta" → 3900). Every driver setting
/// is numeric and fits exactly in a float (ints up to 2^24), so a single float
/// store covers int and float settings alike.
struct BindingSetting {
    etl::string<16> key;
    float           value = 0.0f;
};

struct Binding {
    HalId         hardware_id;
    Role          role;
    DriverType    driver_type;
    ModuleName    module_name;
    SensorAddress address;      // Опціональна ROM адреса (multi-sensor)
    etl::vector<BindingSetting, MAX_BINDING_SETTINGS> settings;  // per-binding driver config

    /// Look up a per-binding setting by key; returns `def` when not present.
    float setting_or(const char* key, float def) const {
        for (const auto& s : settings) {
            if (s.key == key) return s.value;
        }
        return def;
    }
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
    bool invert = false;   // NC contact / active-low
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
    uint32_t freq_hz = 100000;   // швидкість шини (per-device scl_speed_hz)
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

/// I²S-ресурс: лише конфіг (піни + sample_rate + SD). Канал створює драйвер —
/// HAL не тримає i2s_chan_handle_t, тож не тягне driver/i2s_std.h у hal_types.h.
struct I2SBusResource {
    HalId      id;
    gpio_num_t bclk           = GPIO_NUM_NC;
    gpio_num_t ws             = GPIO_NUM_NC;
    gpio_num_t dout           = GPIO_NUM_NC;
    gpio_num_t sd             = GPIO_NUM_NC;
    uint32_t   sample_rate_hz = 16000;
    bool       initialized    = false;
};

} // namespace modesp
