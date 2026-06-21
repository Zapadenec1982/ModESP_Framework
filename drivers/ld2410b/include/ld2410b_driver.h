/**
 * @file ld2410b_driver.h
 * @brief HLK-LD2410B 24GHz mmWave presence radar — ISensorDriver
 *
 * The LD2410B streams "target data" frames over UART (256000 8N1) without any
 * host command. The HAL owns the UART port (board.json uart_buses); this driver
 * only reads bytes via uart_read_bytes() and parses the frame protocol:
 *
 *   F4 F3 F2 F1 | len(2 LE) | type AA state mov(2) mov_e still(2) still_e det(2) ...
 *               | 55 00 | F8 F7 F6 F5
 *
 * A single physical sensor exposes several values. Because ISensorDriver::read()
 * returns one float, the binding's `address` field selects which value this
 * instance reports (default = presence). Several roles may bind the same UART
 * hardware (multiple_per_bus) — they share one parser (Ld2410Port); one instance
 * owns pumping the UART, the rest are passive views.
 *
 * Lifecycle:
 *   1. DriverManager (factory) looks up the UART bus + shared port, calls configure()
 *   2. DriverManager calls init()
 *   3. Main loop calls update(dt_ms) — owner instance pumps + parses UART
 *   4. Business module calls read(value) → channel value (presence 0/1, cm, energy)
 */

#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "etl/string.h"
#include <cstdint>

namespace modesp { struct Binding; }

/// Shared per-UART-port frame parser (defined in the .cpp). One per physical
/// sensor; forward-declared here so the driver can hold a pointer to it.
class Ld2410Port;

class Ld2410bDriver : public modesp::ISensorDriver {
public:
    /// Which decoded value read() returns for this binding (from `address`).
    enum class Channel : uint8_t {
        PRESENCE,        // any target present → 0.0 / 1.0
        MOVING_DIST,     // moving target distance, cm
        STATIC_DIST,     // stationary target distance, cm
        DETECT_DIST,     // detection distance, cm
        MOVING_ENERGY,   // moving target energy, 0..100
        STATIC_ENERGY,   // stationary target energy, 0..100
    };

    Ld2410bDriver() = default;

    /// Map a binding `address` string to a channel ("" → PRESENCE).
    static Channel channel_from_address(const char* address);

    /// Configure before init (called by DriverManager factory).
    /// owner == true → this instance pumps the UART in update(); others are views.
    void configure(const char* role, Ld2410Port* port, Channel channel, bool owner);

    /// Apply per-binding settings (absence_s, max_cm, move_sens, still_sens,
    /// timeout_ms); absent → defaults. Sensor-config values are pushed to the
    /// radar at init() by the owner instance.
    void apply_settings(const modesp::Binding& b);

    // ── ISensorDriver interface ──
    bool init() override;
    void update(uint32_t dt_ms) override;
    bool read(float& value) override;
    bool is_healthy() const override;
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "ld2410b"; }

private:
    etl::string<16> role_;
    Ld2410Port* port_     = nullptr;
    Channel     channel_  = Channel::PRESENCE;
    bool        owner_    = false;

    uint32_t timeout_ms_ = 2000;   // no fresh frame → unhealthy

    // Sensor config — pushed to the radar once at init() by the owner instance.
    // The radar itself holds "present" for absence_s after the target leaves and
    // gates detection to max_gate, so the driver needs no presence-hold of its own.
    uint32_t absence_s_  = 1;       // radar "no-one duration" (s) — reaction speed
    uint16_t max_gate_   = 8;       // max distance gate (×0.75 m); derived from max_cm
    uint8_t  move_sens_  = 0;       // moving sensitivity 0-100 (0 = leave default)
    uint8_t  still_sens_ = 0;       // static sensitivity 0-100 (0 = leave default)
};
