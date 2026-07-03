/**
 * @file panel_module.h
 * @brief LED-panel content module — rotates equipment.room_temp / room_humid / presence
 *        (health-gated, threshold-coloured) to a text panel via IPanelPort::show_text().
 *
 * The panel DRIVER owns transport + wire format (it publishes an IPanelPort); this
 * MODULE owns "what to show" (reads SharedState, which drivers cannot). Fully
 * hardware-agnostic: it resolves the port in on_bind and never mentions BLE. No panel
 * driver bound → port is null → the module produces no output.
 */
#pragma once

#include "modesp/base_module.h"

namespace modesp::panel { class IPanelPort; }

class PanelModule : public modesp::BaseModule {
public:
    PanelModule();

    void on_bind(modesp::DriverManager& drivers,
                 const modesp::BindingTable& bindings,
                 modesp::HAL& hal) override;
    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

private:
    modesp::panel::IPanelPort* port_ = nullptr;   // resolved in on_bind; owned by the driver
    char     shown_[32]   = {0};   // last text pushed to the panel (de-dupe; 32 = string-state cap)
    uint8_t  shown_rgb_[3] = {0};  // last colour pushed (de-dupe on colour-only changes)
    uint8_t  shown_anim_  = 0;     // last effect pushed (de-dupe on web anim change)
    uint32_t eval_ms_     = 0;      // re-evaluate throttle accumulator
    uint32_t rotate_ms_   = 0;      // field-rotation timer
    uint32_t rot_         = 0;      // rotation slot (mod #available fields)
    bool     seen_temp_   = false;  // saw a real (non-zero) temperature — past the 0.00 voltage frame
    // web/MQTT control de-dupe (sentinel -1 = unknown → re-apply on (re)connect)
    int      last_power_  = -1;     // last panel.power sent (0/1)
    int      last_bright_ = -1;     // last panel.brightness sent (%)
    bool     last_connected_ = false;  // last panel.connected status published
};
