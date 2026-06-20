/**
 * @file presence_module.h
 * @brief Presence / occupancy module for the LD2410b mmWave radar
 *
 * Pure SharedState consumer (like simple_thermo): the EquipmentModule binds the
 * ld2410b driver and publishes equipment.<role>; this module reads those, applies
 * occupancy logic, and publishes presence.* for the web UI + MQTT + datalogger.
 *
 * Inputs (published by EquipmentModule from ld2410b bindings):
 *   equipment.presence        — target present 0/1  (role "presence", REQUIRED)
 *   equipment.presence_ok     — sensor health
 *   equipment.move_distance   — moving target cm    (role "move_distance", optional)
 *   equipment.still_distance  — static target cm     (role "still_distance", optional)
 *
 * Logic: enable gate → zone gate (max_distance, if a distance channel is bound)
 *        → occupancy hold (hold_sec) → presence.detected + human-readable state.
 *
 * Settings (web, persisted): presence.enabled, presence.hold_sec, presence.max_distance
 */

#pragma once

#include "modesp/base_module.h"

class PresenceModule : public modesp::BaseModule {
public:
    PresenceModule();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

private:
    uint32_t since_clear_ms_ = 0;   // time since effective presence last true (hold timer)
    uint32_t idle_ms_        = 0;    // time since detected_ last true (for idle_sec)
    bool     detected_       = false;
};
