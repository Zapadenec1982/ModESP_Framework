/**
 * @file ds18b20_driver.h
 * @brief DS18B20 temperature sensor driver implementing ISensorDriver
 *
 * Implementation:
 *   - OneWire protocol via GPIO (bit-bang with critical sections)
 *   - CRC8 scratchpad validation
 *   - Retry (3 attempts with delay) on read
 *   - Validation: range -55..+125C, rate of change < 1C/sec
 *   - After 5 consecutive errors — is_healthy() returns false
 *
 * Lifecycle:
 *   1. DriverManager calls configure() with role, GPIO, interval
 *   2. DriverManager calls init()
 *   3. Main loop calls update(dt_ms) every cycle
 *   4. Business module calls read(value) to get latest temperature
 */

#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "driver/gpio.h"
#include "etl/string.h"

class DS18B20Driver : public modesp::ISensorDriver {
public:
    DS18B20Driver() = default;

    /// Configure before init (called by DriverManager).
    /// address: optional "28:FF:AA:BB:CC:DD:EE:01" for MATCH_ROM (multi-sensor).
    void configure(const char* role, gpio_num_t gpio,
                   uint32_t read_interval_ms = 1000,
                   const char* address = nullptr);

    // ── ISensorDriver interface ──
    bool init() override;
    void update(uint32_t dt_ms) override;
    bool read(float& value) override;
    bool is_healthy() const override;
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "ds18b20"; }
    uint32_t error_count() const override { return consecutive_errors_; }

    // ── Scan API (статичні — не потребують instance) ──
    struct RomAddress {
        uint8_t bytes[8];   // family + serial + CRC
    };
    static constexpr size_t MAX_DEVICES_PER_BUS = 8;

    /// Сканує OneWire шину, повертає кількість знайдених пристроїв
    static size_t scan_bus(gpio_num_t gpio, RomAddress* results, size_t max_results);

    /// Читає температуру з конкретного датчика по адресі (для preview)
    static bool read_temp_by_address(gpio_num_t gpio, const RomAddress& addr, float& temp_out);

    /// Форматує адресу в "28:FF:AA:BB:CC:DD:EE:01"
    static void format_address(const uint8_t* addr, char* out, size_t out_size);

private:
    // ── OneWire low-level ──
    bool     onewire_reset();
    void     onewire_write_byte(uint8_t data);
    uint8_t  onewire_read_byte();
    void     onewire_write_bit(uint8_t bit);
    uint8_t  onewire_read_bit();

    // ── DS18B20 commands ──
    bool     start_conversion();
    bool     read_scratchpad(uint8_t* buf, size_t len);
    bool     read_temperature(float& temp_out);
    void     send_rom_command();    // Завжди MATCH_ROM — адреса обов'язкова
    bool     parse_address(const char* addr_str);  // "28:FF:..." → rom_address_[]

    // ── Static OneWire helpers (для scan — без instance) ──
    static bool     ow_reset(gpio_num_t gpio);
    static void     ow_write_byte(gpio_num_t gpio, uint8_t data);
    static uint8_t  ow_read_byte(gpio_num_t gpio);
    static void     ow_write_bit(gpio_num_t gpio, uint8_t bit);
    static uint8_t  ow_read_bit(gpio_num_t gpio);

    // ── CRC8 (Dallas/Maxim) ──
    static uint8_t crc8(const uint8_t* data, size_t len);

    // ── Retry pattern ──
    template<typename F>
    bool retry(F operation, uint8_t max_attempts = 3, uint32_t delay_ms = 50);

    // ── Validation ──
    bool validate_reading(float value);

    // ── State ──
    etl::string<16> role_;
    gpio_num_t gpio_              = GPIO_NUM_NC;
    uint32_t read_interval_ms_    = 1000;
    float    current_temp_        = 0.0f;
    float    last_valid_temp_     = 0.0f;
    bool     has_valid_reading_   = false;
    uint32_t ms_since_read_       = 0;
    uint32_t uptime_ms_           = 0;
    uint32_t last_valid_reading_ms_ = 0;
    uint8_t  consecutive_errors_  = 0;
    bool     conversion_started_  = false;
    bool     configured_          = false;

    // MATCH_ROM address (multi-sensor)
    uint8_t  rom_address_[8]      = {};
    bool     has_address_         = false;

    // Китайські клони DS18B20 (A5 A5 в reserved bytes) мають невірний CRC
    bool     clone_detected_      = false;

    // Validation limits
    static constexpr float MIN_VALID_TEMP  = -55.0f;
    static constexpr float MAX_VALID_TEMP  = 125.0f;
    static constexpr float MAX_RATE_PER_SEC = 1.0f;
    static constexpr uint8_t MAX_CONSECUTIVE_ERRORS = 5;
};
