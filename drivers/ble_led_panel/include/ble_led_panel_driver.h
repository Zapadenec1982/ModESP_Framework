/**
 * @file ble_led_panel_driver.h
 * @brief iPixel Color / LED_BLE 16x64 BLE LED-matrix panel — actuator + IPanelPort.
 *
 * A CONNECT BLE device. The driver supplies its adv-name prefix + GATT write/notify
 * UUIDs to modesp_ble's GENERIC central link (central_link.h) and then owns ALL the
 * device wire format: control byte-commands, the native iPixel text-frame encoder,
 * the glyph font, and a background render task. modesp_ble knows none of this.
 *
 * The class wears two hats:
 *   - IActuatorDriver — bound to the `equipment` module; set_value = brightness.
 *   - IPanelPort      — the `panel` module resolves it via DriverRegistry::panel_port()
 *                       and drives content (power / brightness / text) through it.
 * Protocol: docs/ble/panel_protocol.md.
 */
#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "modesp/hal/panel_port.h"
#include "modesp/ble/central_link.h"
#include "etl/string.h"
#include <cstdint>

namespace modesp { struct Binding; }

class BleLedPanelDriver : public modesp::IActuatorDriver,
                          public modesp::panel::IPanelPort {
public:
    BleLedPanelDriver() = default;

    /// Configure before init: role + the central link the factory obtained by
    /// registering this panel's connect profile with modesp_ble.
    void configure(const char* role, modesp::ble::ICentralLink* link);
    /// Apply per-binding settings (brightness); absent → default.
    void apply_settings(const modesp::Binding& b);

    // ── IActuatorDriver ──
    bool init() override;
    void update(uint32_t dt_ms) override;
    bool set(bool state) override;               // power is owned by the panel module (no-op)
    bool get_state() const override { return is_healthy(); }
    bool set_value(float value_0_1) override;    // brightness 0..1 → 5..100 %
    float get_value() const override { return brightness_ / 100.0f; }
    bool supports_analog() const override { return true; }
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "ble_led_panel"; }
    bool is_healthy() const override;            // central link connected

    // ── IPanelPort (the panel module drives content through this) ──
    bool connected() const override;
    void set_power(bool on) override;
    void set_brightness(int pct) override;
    void show_text(const char* s, uint8_t r, uint8_t g, uint8_t b,
                   uint8_t anim, uint8_t speed, uint8_t rainbow) override;

    /// Build the native iPixel text frame (glyphs + colour + effect) and send it
    /// atomically through the link. Runs on the background render task only.
    void render_text_frame(const char* s, uint8_t r, uint8_t g, uint8_t b,
                           uint8_t anim, uint8_t speed, uint8_t rainbow);

private:
    bool push_brightness(int pct);     // encode + write the brightness command

    etl::string<16> role_;
    modesp::ble::ICentralLink* link_ = nullptr;   // owned by modesp_ble (central link)
    int  brightness_      = 80;        // desired brightness %
    int  sent_brightness_ = -1;        // last value actually written (-1 = none) — de-dupe
    bool was_ready_       = false;     // edge-detect the link connection
};
