/**
 * @file driver_registry.h
 * @brief Type-string → factory registry for drivers.
 *
 * Replaces the old hardcoded if-else dispatch in DriverManager. Each driver
 * registers a factory under its manifest "driver" type string; DriverManager
 * looks up by binding.driver_type — no hardcoded #includes, pools or branches.
 *
 * A driver disabled in menuconfig (CONFIG_MODESP_DRIVER_<NAME>=n) is simply not
 * compiled, so its registration function is absent and the generated
 * modesp_register_all_drivers() skips it (guarded by #if). Bindings referencing
 * a disabled/unknown driver are skipped with a warning.
 *
 * Registration is EXPLICIT (not static-init): the generated
 * modesp_register_all_drivers() calls each enabled driver's
 * modesp_register_driver_<name>() — deterministic, no WHOLE_ARCHIVE tricks.
 */

#pragma once

#include "modesp/hal/hal_types.h"          // Binding, MAX_SENSORS/ACTUATORS
#include "modesp/hal/driver_interfaces.h"  // ISensorDriver / IActuatorDriver

namespace modesp {

class HAL;

using SensorFactory   = ISensorDriver*   (*)(const Binding&, HAL&);
using ActuatorFactory = IActuatorDriver* (*)(const Binding&, HAL&);

class DriverRegistry {
public:
    static constexpr size_t MAX_DRIVER_TYPES = 16;

    /// Register a factory under a type string. Idempotent: re-registering the
    /// same type is a no-op (so calling register-all twice is safe).
    static bool register_sensor(const char* type, SensorFactory fn);
    static bool register_actuator(const char* type, ActuatorFactory fn);

    /// Create a driver for the given type. Returns nullptr if the type is not
    /// registered (disabled/unknown) or the factory itself failed.
    static ISensorDriver*   create_sensor(const char* type, const Binding&, HAL&);
    static IActuatorDriver* create_actuator(const char* type, const Binding&, HAL&);

    /// True if any (sensor or actuator) factory is registered for this type.
    static bool is_known(const char* type);
};

} // namespace modesp

// ─────────────────────────────────────────────────────────────────────
// Registration macros — used once at file scope in a driver's .cpp.
//
// Defines `extern "C" void modesp_register_driver_<name>()`, which the
// generated modesp_register_all_drivers() declares and calls. <name> MUST equal
// the manifest "driver" field (== folder name), so the generator can derive the
// symbol mechanically without knowing the C++ class name.
//
//   static ISensorDriver* my_factory(const Binding&, HAL&) { ... }
//   MODESP_REGISTER_SENSOR(ds18b20, &my_factory)
// ─────────────────────────────────────────────────────────────────────
#define MODESP_REGISTER_SENSOR(name, factory_fn)                          \
    extern "C" void modesp_register_driver_##name(void) {                 \
        ::modesp::DriverRegistry::register_sensor(#name, (factory_fn));    \
    }

#define MODESP_REGISTER_ACTUATOR(name, factory_fn)                        \
    extern "C" void modesp_register_driver_##name(void) {                 \
        ::modesp::DriverRegistry::register_actuator(#name, (factory_fn));  \
    }
