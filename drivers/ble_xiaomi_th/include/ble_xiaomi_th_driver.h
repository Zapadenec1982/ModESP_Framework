/**
 * @file ble_xiaomi_th_driver.h
 * @brief Xiaomi LYWSD03MMC (pvvx/ATC/BTHome) BLE-observer — ISensorDriver
 *
 * A broadcast (observer) BLE sensor: it never connects. The modesp_ble passive
 * scanner decodes its advertisements and caches the reading in BleCentral, keyed
 * by MAC. This driver is a thin view onto one channel of that cache.
 *
 * Because ISensorDriver::read() returns one float, the binding's `address` field
 * selects the channel (temperature/humidity/battery). One physical sensor (one MAC,
 * declared in board.json `ble_devices`) feeds several roles (multiple_per_bus).
 *
 * Lifecycle (mirrors ld2410b):
 *   1. factory: find_ble_device(hardware_id) -> MAC; BleCentral::register_mac(MAC)
 *      -> cache ptr; configure(role, cache, channel)
 *   2. init()
 *   3. update(dt_ms) — no-op (push model; BleCentral is fed by the scanner)
 *   4. read(value) -> cached channel value; is_healthy() = fresh within stale_ms
 */
#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "modesp/ble/ble_central.h"
#include "etl/string.h"
#include <cstdint>

namespace modesp { struct Binding; }

class BleXiaomiThDriver : public modesp::ISensorDriver {
public:
    /// Which cached value read() returns for this binding (from `address`).
    enum class Channel : uint8_t { TEMPERATURE, HUMIDITY, BATTERY };

    BleXiaomiThDriver() = default;

    /// Map a binding `address` string to a channel ("" / "temperature" → TEMPERATURE).
    static Channel channel_from_address(const char* address);

    /// Configure before init (called by the factory). `cache` is owned by BleCentral.
    void configure(const char* role, const modesp::BleReading* cache, Channel channel);

    /// Apply per-binding settings (stale_ms); absent → default.
    void apply_settings(const modesp::Binding& b);

    // ── ISensorDriver interface ──
    bool init() override;
    void update(uint32_t dt_ms) override;
    bool read(float& value) override;
    bool is_healthy() const override;
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "ble_xiaomi_th"; }

private:
    etl::string<16> role_;
    const modesp::BleReading* cache_ = nullptr;   // owned by BleCentral
    Channel  channel_  = Channel::TEMPERATURE;
    uint32_t stale_ms_ = 60000;                   // no fresh adv → unhealthy
};
