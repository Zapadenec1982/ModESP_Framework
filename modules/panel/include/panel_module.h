/**
 * @file panel_module.h
 * @brief LED-panel content module — formats equipment.room_temp and pushes it to
 *        the iPixel BLE panel via BlePanel::show_text().
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
    char     shown_[20] = {0};   // last text pushed to the panel (de-dupe)
    uint32_t since_ms_  = 0;      // re-evaluate throttle accumulator
};
