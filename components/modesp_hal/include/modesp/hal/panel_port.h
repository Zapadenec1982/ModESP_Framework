/**
 * @file panel_port.h
 * @brief Semantic seam of a text/LED panel (panel analogue of IDisplayPort).
 *
 * The panel MODULE decides WHAT to show (clock / sensor rotation / web message,
 * colours, icons, effect) and pushes semantic calls into an IPanelPort. The port
 * (implemented by a connect driver, e.g. ble_led_panel) owns HOW: the device wire
 * format, the glyph font, the BLE link and its background render task. The module
 * never touches BLE or byte encoding; the port never decides content.
 *
 * The owning driver publishes its instance via DriverRegistry::set_panel_port() at
 * factory time; the panel module resolves it in on_bind via panel_port(). No
 * binding → null → the module simply produces no output.
 */
#pragma once

#include <cstdint>

namespace modesp::panel {

/// Semantic control surface of a text panel. All calls come from the module's
/// single update context; the port is responsible for not blocking that context
/// on slow I/O (e.g. show_text enqueues to a background task).
class IPanelPort {
public:
    virtual ~IPanelPort() = default;

    /// True while the panel link is up and ready to accept writes.
    virtual bool connected() const = 0;

    /// Screen power on/off.
    virtual void set_power(bool on) = 0;

    /// Brightness in percent (clamped by the port to the device's valid range).
    virtual void set_brightness(int pct) = 0;

    /// Show a short string in colour (r,g,b) with a device-native effect: anim
    /// (0 static, others scroll/blink/breathe/…), speed (0..100), rainbow (0..9).
    /// NON-BLOCKING — the port renders/sends in the background. No-op if null s or
    /// not connected.
    virtual void show_text(const char* s, uint8_t r, uint8_t g, uint8_t b,
                           uint8_t anim, uint8_t speed, uint8_t rainbow) = 0;
};

} // namespace modesp::panel
