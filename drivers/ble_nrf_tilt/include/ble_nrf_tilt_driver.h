/**
 * @file ble_nrf_tilt_driver.h
 * @brief nRF52832 tilt beacon (HolyIOT 21011 + LIS2DH12) — ISensorDriver
 *
 * A broadcast (observer) BLE sensor that advertises MANUFACTURER data (company 0xFFFF),
 * not service data — so its decoder registers with the transport's manufacturer-data
 * pool (adv_decoder.h: register_adv_mfg_decoder). Because its fields (tilt angle, tilted
 * flag, battery mV, raw axes) are device-specific, this driver owns its OWN per-MAC cache
 * (NrfTiltReading) instead of the shared temp/hum/battery BleReading — the transport stays
 * format-agnostic and each driver publishes its own values.
 *
 * ISensorDriver::read() returns one float, so the binding's `address` selects the channel
 * (angle/tilted/battery/ax/ay/az). One physical device (one MAC / device id) feeds several
 * roles (multiple_per_bus).
 *
 * On-air layout (main.c of the nRF firmware), manufacturer data incl. the 2-byte company:
 *   [0..1] FF FF | [2] ver=1 | [3] flags(bit0=tilted) | [4] tilt_deg (0xFF=invalid)
 *   [5..6] vbat_mV LE | [7] seq | [8] devid | [9..14] ax,ay,az int16 LE
 */
#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "etl/string.h"
#include <cstdint>

namespace modesp { struct Binding; }

/// One decoded nRF tilt reading, keyed by MAC (NimBLE addr.val / little-endian order).
/// Written by the manufacturer-data decoder, read by the driver view. Owned by the driver.
struct NrfTiltReading {
    uint8_t mac[6];
    bool    used;
    int64_t last_us;       // last frame time (freshness) — 0 = never seen
    int8_t  rssi;          // 127 = unavailable
    int     angle_deg;     // -1 = invalid this frame (device sent 0xFF)
    bool    tilted;
    int     batt_mv;
    int16_t ax, ay, az;    // raw accelerometer axes (for web calibration)
    uint8_t seq;
};

class BleNrfTiltDriver : public modesp::ISensorDriver {
public:
    /// Which cached value read() returns for this binding (from `address`).
    enum class Channel : uint8_t { ANGLE, TILTED, BATTERY, AX, AY, AZ };

    BleNrfTiltDriver() = default;

    /// Map a binding `address` string to a channel ("" / "angle" → ANGLE).
    static Channel channel_from_address(const char* address);

    /// Configure before init (called by the factory). `cache` is a slot in the driver pool.
    void configure(const char* role, const NrfTiltReading* cache, Channel channel);

    /// Apply per-binding settings (stale_ms); absent → default.
    void apply_settings(const modesp::Binding& b);

    // ── ISensorDriver interface ──
    bool init() override;
    void update(uint32_t dt_ms) override;
    bool read(float& value) override;
    bool is_healthy() const override;
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "ble_nrf_tilt"; }

private:
    etl::string<16> role_;
    const NrfTiltReading* cache_ = nullptr;   // slot in the driver's cache pool
    Channel  channel_  = Channel::ANGLE;
    uint32_t stale_ms_ = 60000;               // no fresh adv → unhealthy
};
