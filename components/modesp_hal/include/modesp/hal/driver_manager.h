/**
 * @file driver_manager.h
 * @brief Creates and manages sensor/actuator drivers from binding config
 *
 * DriverManager reads the BindingTable, creates concrete driver instances
 * from static pools (zero heap), and provides lookup by role name.
 *
 * Business modules call find_sensor("chamber_temp") / find_actuator("compressor")
 * to get abstract ISensorDriver* / IActuatorDriver* pointers.
 */

#pragma once

#include "modesp/hal/hal.h"
#include "modesp/hal/driver_interfaces.h"
#include "etl/string_view.h"

namespace modesp {

class DriverManager {
public:
    /// Create all drivers from bindings, using HAL resources.
    /// Calls init() on each created driver.
    bool init(const BindingTable& bindings, HAL& hal);

    /// Find a sensor driver by role name (e.g. "chamber_temp")
    ISensorDriver*   find_sensor(etl::string_view role);

    /// Find an actuator driver by role name (e.g. "compressor")
    IActuatorDriver* find_actuator(etl::string_view role);

    /// Update all drivers (call before module updates)
    void update_all(uint32_t dt_ms);

    size_t sensor_count()   const { return sensor_count_; }
    size_t actuator_count() const { return actuator_count_; }

    /// Access sensor by index (for EquipmentBase generic binding)
    ISensorDriver*   sensor_at(size_t i) const { return (i < sensors_.size()) ? sensors_[i].driver : nullptr; }
    IActuatorDriver* actuator_at(size_t i) const { return (i < actuators_.size()) ? actuators_[i].driver : nullptr; }

private:
    struct SensorEntry {
        ISensorDriver* driver;
        Role role;
        ModuleName module;
    };

    struct ActuatorEntry {
        IActuatorDriver* driver;
        Role role;
        ModuleName module;
    };

    etl::vector<SensorEntry, MAX_SENSORS>    sensors_;
    etl::vector<ActuatorEntry, MAX_ACTUATORS> actuators_;
    size_t sensor_count_   = 0;
    size_t actuator_count_ = 0;
};

} // namespace modesp
