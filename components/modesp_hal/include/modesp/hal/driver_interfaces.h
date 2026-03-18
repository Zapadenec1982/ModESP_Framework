/**
 * @file driver_interfaces.h
 * @brief Abstract interfaces for sensor and actuator drivers
 *
 * Business logic modules (thermostat, alarm, etc.) interact
 * with hardware through these interfaces only — never
 * accessing GPIO or bus protocols directly.
 *
 * Concrete drivers (DS18B20Driver, RelayDriver, etc.)
 * implement these interfaces and are created by DriverManager.
 */

#pragma once

#include <cstdint>

namespace modesp {

// ═══════════════════════════════════════════════════════════════
// ISensorDriver — read-only hardware input
// ═══════════════════════════════════════════════════════════════

class ISensorDriver {
public:
    virtual ~ISensorDriver() = default;

    virtual bool init() = 0;
    virtual void update(uint32_t dt_ms) = 0;

    /// Read latest valid value. Returns true if a valid reading exists.
    virtual bool read(float& value) = 0;

    virtual bool is_healthy() const = 0;
    virtual const char* role() const = 0;
    virtual const char* type() const = 0;
    virtual uint32_t error_count() const { return 0; }
};

// ═══════════════════════════════════════════════════════════════
// IActuatorDriver — controllable hardware output
// ═══════════════════════════════════════════════════════════════

class IActuatorDriver {
public:
    virtual ~IActuatorDriver() = default;

    virtual bool init() = 0;
    virtual void update(uint32_t dt_ms) = 0;

    // Discrete control
    virtual bool set(bool state) = 0;
    virtual bool get_state() const = 0;

    // Analog control — default maps to discrete
    virtual bool set_value(float value_0_1) { return set(value_0_1 > 0.5f); }
    virtual float get_value() const { return get_state() ? 1.0f : 0.0f; }
    virtual bool supports_analog() const { return false; }

    virtual const char* role() const = 0;
    virtual const char* type() const = 0;
    virtual bool is_healthy() const = 0;

    virtual void emergency_stop() { set(false); }
    virtual uint32_t switch_count() const { return 0; }
};

} // namespace modesp
