/**
 * @file ble_led_panel_driver.cpp
 * @brief iPixel/LED_BLE 16x64 panel actuator — control plane (Increment 1).
 *
 * Connects via modesp_ble's BlePanel (central-connect). Control commands are
 * Confirmed-by-working-code (docs/ble/panel_protocol.md §1.1). On the connection
 * edge the driver auto-sends ON + brightness so the panel is visibly validated.
 */
#include "ble_led_panel_driver.h"
#include "modesp/hal/hal_types.h"        // complete modesp::Binding for apply_settings()
#include "modesp/hal/hal.h"
#include "modesp/hal/driver_registry.h"
#include "modesp/ble/ble_panel.h"
#include "etl/string_view.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "ble_led_panel";

void BleLedPanelDriver::configure(const char* role, const char* name_prefix) {
    role_ = role;
#if defined(CONFIG_MODESP_BLE_CENTRAL)
    modesp::BlePanel::instance().set_target(name_prefix);
#else
    (void)name_prefix;
#endif
}

void BleLedPanelDriver::apply_settings(const modesp::Binding& b) {
    brightness_ = static_cast<int>(b.setting_or("brightness", static_cast<float>(brightness_)));
}

bool BleLedPanelDriver::init() {
    ESP_LOGI(TAG, "[%s] init (brightness=%d%%) — connecting on scan match", role_.c_str(), brightness_);
    return true;
}

bool BleLedPanelDriver::push_power(bool on) {
#if defined(CONFIG_MODESP_BLE_CENTRAL)
    static const uint8_t ON_CMD[5]  = {0x05, 0x00, 0x07, 0x01, 0x01};
    static const uint8_t OFF_CMD[5] = {0x05, 0x00, 0x07, 0x01, 0x00};
    return modesp::BlePanel::instance().write_cmd(on ? ON_CMD : OFF_CMD, 5, /*with_response=*/true);
#else
    (void)on; return false;
#endif
}

bool BleLedPanelDriver::push_brightness(int pct) {
#if defined(CONFIG_MODESP_BLE_CENTRAL)
    if (pct < 5)   pct = 5;
    if (pct > 100) pct = 100;
    const uint8_t cmd[5] = {0x05, 0x00, 0x04, 0x80, static_cast<uint8_t>(pct)};
    return modesp::BlePanel::instance().write_cmd(cmd, 5, /*with_response=*/true);
#else
    (void)pct; return false;
#endif
}

void BleLedPanelDriver::push_display_test() {
#if defined(CONFIG_MODESP_BLE_CENTRAL)
    // TEMP (Increment 1.5): confirm we OVERRIDE the panel's saved animation.
    // Clear → enter DIY pixel mode → light 5 distinct pixels (corners + center) on
    // the 64x16 matrix. Commands Confirmed-by-working-code (docs/ble/panel_protocol.md §1.1).
    auto& p = modesp::BlePanel::instance();
    // All writes WITH-response → write_cmd blocks per write (serialized flow control,
    // like pypixelcolor response=True); the panel no longer drops back-to-back frames.
    static const uint8_t CLEAR[4] = {0x04, 0x00, 0x03, 0x80};
    static const uint8_t DIY[5]   = {0x05, 0x00, 0x04, 0x01, 0x01};
    p.write_cmd(CLEAR, sizeof(CLEAR), true);
    p.write_cmd(DIY,   sizeof(DIY),   true);
    static const uint8_t PX[5][10] = {
        {0x0A,0x00,0x05,0x01,0x00, 0xFF,0x00,0x00,  0,  0},  // red    top-left
        {0x0A,0x00,0x05,0x01,0x00, 0x00,0xFF,0x00, 63,  0},  // green  top-right
        {0x0A,0x00,0x05,0x01,0x00, 0x00,0x00,0xFF,  0, 15},  // blue   bottom-left
        {0x0A,0x00,0x05,0x01,0x00, 0xFF,0xFF,0xFF, 63, 15},  // white  bottom-right
        {0x0A,0x00,0x05,0x01,0x00, 0xFF,0xFF,0x00, 32,  8},  // yellow center
    };
    for (const auto& px : PX) p.write_cmd(px, sizeof(px), true);
    ESP_LOGI(TAG, "[%s] sent DIY display-control test (clear + DIY + 5 px, serialized)", role_.c_str());
#endif
}

void BleLedPanelDriver::update(uint32_t /*dt_ms*/) {
#if defined(CONFIG_MODESP_BLE_CENTRAL)
    bool ready = modesp::BlePanel::instance().is_connected();
    if (ready && !was_ready_) {
        // Connect edge → power ON + (re)send brightness. The panel is self-driving:
        // power follows the link; brightness is reconciled below (de-duped).
        push_power(true);
        sent_brightness_ = -1;            // force the brightness (re)send
        ESP_LOGI(TAG, "[%s] panel connected → ON @ %d%% (text driven by the panel module)",
                 role_.c_str(), brightness_);
    }
    if (ready && sent_brightness_ != brightness_) {
        if (push_brightness(brightness_)) sent_brightness_ = brightness_;
    }
    if (!ready && was_ready_) {
        sent_brightness_ = -1;            // disconnected → re-push on reconnect
    }
    was_ready_ = ready;
#endif
}

bool BleLedPanelDriver::set(bool /*state*/) {
    // Increment 1: the panel is self-driving — power follows the BLE connection
    // (update() turns it ON on connect). EquipmentBase drives every bound actuator
    // to its default request (false) each tick; honoring that here would force the
    // panel OFF and flood the link. Real on/off arrives with the panel module.
    return is_healthy();
}

bool BleLedPanelDriver::set_value(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    brightness_ = static_cast<int>(v * 100.0f + 0.5f);
#if defined(CONFIG_MODESP_BLE_CENTRAL)
    if (modesp::BlePanel::instance().is_connected() && push_brightness(brightness_)) {
        sent_brightness_ = brightness_;
        return true;
    }
#endif
    return false;   // not connected → write dropped
}

bool BleLedPanelDriver::is_healthy() const {
#if defined(CONFIG_MODESP_BLE_CENTRAL)
    return modesp::BlePanel::instance().is_connected();
#else
    return false;
#endif
}

// ═══════════════════════════════════════════════════════════════
// Factory + registration (optional via CONFIG_MODESP_DRIVER_BLE_LED_PANEL).
// Connects by adv-NAME from board.json ble_devices (panel has no fixed MAC config).
// ═══════════════════════════════════════════════════════════════

namespace {
BleLedPanelDriver s_panel;   // single panel instance (multiple_per_bus: false)

modesp::IActuatorDriver* ble_led_panel_factory(const modesp::Binding& b, modesp::HAL& hal) {
    auto* dev = hal.find_ble_device(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!dev) {
        ESP_LOGE(TAG, "BLE device '%s' not in board.json ble_devices", b.hardware_id.c_str());
        return nullptr;
    }
    const char* name = dev->name.c_str();
    if (name[0] == '\0') {
        ESP_LOGE(TAG, "ble_devices '%s' needs a 'name' (panel adv-name prefix) to connect",
                 b.hardware_id.c_str());
        return nullptr;
    }
    s_panel.configure(b.role.c_str(), name);
    s_panel.apply_settings(b);
    return &s_panel;
}
} // namespace

MODESP_REGISTER_ACTUATOR(ble_led_panel, &ble_led_panel_factory)
