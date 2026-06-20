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

    /// Apply per-binding settings (presence_hold_ms, timeout_ms); absent → defaults.
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

    uint32_t timeout_ms_       = 2000;   // no fresh frame → unhealthy
    uint32_t presence_hold_ms_ = 2000;   // hold "present" after target lost

    // Presence hold timer (per instance; maintained every update())
    uint32_t since_present_ms_ = 0;
    bool     present_held_     = false;
};
