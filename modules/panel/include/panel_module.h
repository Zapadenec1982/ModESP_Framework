/**
 * @file panel_module.h
 * @brief LED-panel content module — rotates equipment.room_temp / room_humid / presence
 *        (health-gated, threshold-coloured) to the iPixel BLE panel via BlePanel::show_text().
 *
 * The panel DRIVER (drivers/ble_led_panel) owns transport/control; this MODULE owns
 * "what to show" (reads SharedState, which drivers cannot). Decoupled via the
 * BlePanel singleton in modesp_ble.
 */
#pragma once

#include "modesp/base_module.h"

class PanelModule : public modesp::BaseModule {
public:
    PanelModule();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

private:
    char     shown_[24]   = {0};   // last text pushed to the panel (de-dupe)
    uint8_t  shown_rgb_[3] = {0};  // last colour pushed (de-dupe on colour-only changes)
    uint32_t eval_ms_     = 0;      // re-evaluate throttle accumulator
    uint32_t rotate_ms_   = 0;      // field-rotation timer
    uint32_t rot_         = 0;      // rotation slot (mod #available fields)
    bool     seen_temp_   = false;  // saw a real (non-zero) temperature — past the 0.00 voltage frame
};
