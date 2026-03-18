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

// Forward declarations — concrete drivers included only in .cpp
class DS18B20Driver;
class RelayDriver;
class DigitalInputDriver;
class NtcDriver;
class PCF8574RelayDriver;
class PCF8574InputDriver;

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

    ISensorDriver*   create_sensor(const Binding& binding, HAL& hal);
    ISensorDriver*   create_di_sensor(const Binding& binding, HAL& hal);
    ISensorDriver*   create_ntc_sensor(const Binding& binding, HAL& hal);
    IActuatorDriver* create_actuator(const Binding& binding, HAL& hal);
    IActuatorDriver* create_pcf_actuator(const Binding& binding, HAL& hal);
    ISensorDriver*   create_pcf_sensor(const Binding& binding, HAL& hal);
};

} // namespace modesp
