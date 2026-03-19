/**
 * @file simple_thermo_module.h
 * @brief Simple ON/OFF thermostat — demo module for ModESP Framework
 *
 * Reads temperature from equipment.air_temp (via SharedState).
 * Publishes heating request to simple_thermo.output.
 * Equipment Manager controls the actual relay.
 *
 * Hysteresis logic:
 *   - Turn ON  when temp < setpoint - differential
 *   - Turn OFF when temp >= setpoint
 */

#pragma once

#include "modesp/base_module.h"

class SimpleThermoModule : public modesp::BaseModule {
public:
    SimpleThermoModule();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

private:
    bool heating_ = false;
};
