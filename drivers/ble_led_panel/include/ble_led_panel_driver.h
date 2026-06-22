/**
 * @file ble_led_panel_driver.h
 * @brief iPixel Color / LED_BLE 16x64 BLE LED-matrix panel — IActuatorDriver.
 *
 * A CONNECT BLE device (not observer). The driver registers the panel's adv-name
 * prefix with modesp_ble's BlePanel, which scans → connects → discovers fa02/fa03.
 * Control plane only (Increment 1): set() = screen ON/OFF, set_value() = brightness.
 * Native scrolling-text comes in Increment 2. Protocol: docs/ble/panel_protocol.md.
 */
#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "etl/string.h"
#include <cstdint>

namespace modesp { struct Binding; }

class BleLedPanelDriver : public modesp::IActuatorDriver {
public:
    BleLedPanelDriver() = default;

    /// Configure before init: role + the panel adv-name prefix (e.g. "LED_BLE_E6C5EBE2").
    void configure(const char* role, const char* name_prefix);
    /// Apply per-binding settings (brightness); absent → default.
    void apply_settings(const modesp::Binding& b);

    // ── IActuatorDriver ──
    bool init() override;
    void update(uint32_t dt_ms) override;        // auto-ON + brightness on connect edge
    bool set(bool state) override;               // Increment 1: power follows connection (see .cpp)
    bool get_state() const override { return is_healthy(); }  // ON whenever the BLE link is up
    bool set_value(float value_0_1) override;    // brightness 0..1 → 5..100 %
    float get_value() const override { return brightness_ / 100.0f; }
    bool supports_analog() const override { return true; }
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "ble_led_panel"; }
    bool is_healthy() const override;            // BlePanel connected

private:
    bool push_brightness(int pct);     // returns the BLE write result (used by set_value)
    void push_display_test();          // TEMP (Increment 1.5): clear + DIY + corner pixels

    etl::string<16> role_;
    int  brightness_      = 80;        // desired brightness %
    int  sent_brightness_ = -1;        // last value actually written (-1 = none) — de-dupe
    bool was_ready_       = false;     // edge-detect the BLE connection
};
