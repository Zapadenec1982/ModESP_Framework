/**
 * @file equipment_module.h
 * @brief Template Equipment Module — extends EquipmentBase with pass-through arbitration
 *
 * This is a TEMPLATE for framework projects. It provides no business-specific
 * arbitration — all actuator requests pass through directly.
 *
 * For product-specific arbitration, create your own class:
 *   class MyEquipment : public EquipmentBase {
 *       void apply_arbitration(uint32_t dt_ms) override { ... }
 *   };
 *
 * See ModESP_v4 ColdRoomEquipment for a refrigeration example.
 */

#pragma once

#include "equipment_base.h"

class EquipmentModule : public EquipmentBase {
public:
    EquipmentModule() : EquipmentBase("equipment", modesp::ModulePriority::CRITICAL) {}

protected:
    /// Pass-through arbitration — no business logic, all requests applied directly.
    /// Override this in your product to add priority, interlocks, anti-short-cycle.
    void apply_arbitration(uint32_t dt_ms) override {
        (void)dt_ms;
        // Template: no arbitration, actuators follow requests from SharedState.
        // Product should override with business-specific logic.
    }
};
