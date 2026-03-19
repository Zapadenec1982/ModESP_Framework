/**
 * @file simple_thermo_module.cpp
 * @brief Simple ON/OFF thermostat — demo implementation
 */

#include "simple_thermo_module.h"
#include "esp_log.h"

static const char* TAG = "SimpleThermo";

SimpleThermoModule::SimpleThermoModule()
    : BaseModule("simple_thermo", modesp::ModulePriority::NORMAL)
{}

bool SimpleThermoModule::on_init() {
    state_set("simple_thermo.temperature", 0.0f);
    state_set("simple_thermo.state", "off");
    state_set("simple_thermo.output", false);

    float sp = read_float("simple_thermo.setpoint", 22.0f);
    ESP_LOGI(TAG, "Initialized (setpoint=%.1f°C)", sp);
    return true;
}

void SimpleThermoModule::on_update(uint32_t dt_ms) {
    (void)dt_ms;

    // Read current temperature from Equipment Manager
    float temp = read_float("equipment.air_temp", 0.0f);
    state_set("simple_thermo.temperature", temp);

    // Read settings
    float setpoint = read_float("simple_thermo.setpoint", 22.0f);
    float diff = read_float("simple_thermo.differential", 1.0f);

    // ON/OFF hysteresis
    if (heating_) {
        // Currently heating — turn OFF when temp reaches setpoint
        if (temp >= setpoint) {
            heating_ = false;
            ESP_LOGI(TAG, "OFF (temp=%.1f >= setpoint=%.1f)", temp, setpoint);
        }
    } else {
        // Currently idle — turn ON when temp drops below setpoint - differential
        if (temp < (setpoint - diff)) {
            heating_ = true;
            ESP_LOGI(TAG, "ON (temp=%.1f < %.1f)", temp, setpoint - diff);
        }
    }

    // Publish state
    state_set("simple_thermo.output", heating_);
    state_set("simple_thermo.state", heating_ ? "heating" : "idle");
}
