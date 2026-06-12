/**
 * @file equipment_base.h
 * @brief EquipmentBase — universal HAL owner with manifest-driven roles
 *
 * Framework provides:
 *   - Driver binding by role name (from manifest "requires")
 *   - Sensor reading with EMA filter + health monitoring
 *   - Actuator state apply + publishing
 *   - Anti-short-cycle helper (ShortCycleGuard)
 *   - Auto-publish has_* capability flags
 *
 * Product overrides:
 *   - apply_arbitration() — business-specific priority logic
 *   - on_product_init() / on_product_update() — optional hooks
 *
 * Usage:
 *   class MyEquipment : public EquipmentBase {
 *   protected:
 *       void apply_arbitration(uint32_t dt_ms) override {
 *           bool request = read_bool("controller.req.output1");
 *           set_actuator("output1", request);
 *       }
 *   };
 */

#pragma once

#include "modesp/base_module.h"
#include "modesp/hal/driver_interfaces.h"
#include <cstring>

namespace modesp {
class DriverManager;
}

class EquipmentBase : public modesp::BaseModule {
public:
    explicit EquipmentBase(const char* name = "equipment",
                           modesp::ModulePriority priority = modesp::ModulePriority::CRITICAL);

    /// Bind drivers from DriverManager (reads roles from manifest "requires")
    void bind_drivers(modesp::DriverManager& dm);

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_message(const etl::imessage& msg) override;
    void on_stop() override;

protected:
    // ══════════════════════════════════════════════════════════
    // Driver access (by role name)
    // ══════════════════════════════════════════════════════════

    /// Find bound sensor by role name (nullptr if not bound)
    modesp::ISensorDriver*   sensor(const char* role) const;

    /// Find bound actuator by role name (nullptr if not bound)
    modesp::IActuatorDriver* actuator(const char* role) const;

    /// Check if a driver is bound for this role
    bool has_driver(const char* role) const;

    // ══════════════════════════════════════════════════════════
    // Sensor helpers
    // ══════════════════════════════════════════════════════════

    /// Read sensor value with optional EMA filter.
    /// Returns filtered value, or 0.0f if sensor not bound/unhealthy.
    float read_sensor_value(const char* role);

    /// Check sensor health
    bool is_sensor_healthy(const char* role) const;

    // ══════════════════════════════════════════════════════════
    // Actuator helpers
    // ══════════════════════════════════════════════════════════

    /// Request actuator state (applied in apply_outputs phase)
    void set_actuator(const char* role, bool state);

    /// Get current actuator state (from driver)
    bool get_actuator_state(const char* role) const;

    // ══════════════════════════════════════════════════════════
    // Anti-short-cycle helper
    // ══════════════════════════════════════════════════════════

    struct ShortCycleGuard {
        uint32_t min_on_ms;
        uint32_t min_off_ms;
        uint32_t since_change_ms = modesp::TIMER_SATISFIED;
        bool     current_state = false;

        /// Apply min on/off guard. Returns actual allowed state.
        bool apply(bool requested, uint32_t dt_ms);
    };

    // ══════════════════════════════════════════════════════════
    // Product hooks (override in your product)
    // ══════════════════════════════════════════════════════════

    /// MUST override — product-specific arbitration logic
    virtual void apply_arbitration(uint32_t dt_ms) = 0;

    /// Optional — called after base on_init()
    virtual void on_product_init() {}

    /// Optional — called after sensors read, before arbitration
    virtual void on_product_update(uint32_t dt_ms) { (void)dt_ms; }

    // ══════════════════════════════════════════════════════════
    // EMA filter coefficient (from SharedState "equipment.filter_coeff")
    // ══════════════════════════════════════════════════════════
    float ema_alpha_ = 0.0f;

private:
    // ── Role bindings (populated from manifest "requires" + DriverManager) ──
    static constexpr size_t MAX_ROLES = 12;

    struct RoleBinding {
        char     role[24]  = {};
        char     type[12]  = {};        // "sensor" or "actuator"
        modesp::ISensorDriver*   as_sensor   = nullptr;
        modesp::IActuatorDriver* as_actuator = nullptr;
        float    ema_value    = 0.0f;
        bool     ema_init     = false;
        bool     output_req   = false;   // requested output state (for actuators)
        bool     optional     = false;
    };

    RoleBinding roles_[MAX_ROLES] = {};
    size_t      role_count_ = 0;

    /// Latched safe-mode: once a FATAL/SAFE_MODE message arrives, ALL outputs
    /// stay forced OFF every cycle (product apply_arbitration is bypassed)
    /// until reboot. Without this latch a product override would re-energize
    /// actuators on the very next on_update() tick after safe mode.
    bool        safe_mode_ = false;

    /// Find role index by name (-1 if not found)
    int find_role(const char* role) const;

    /// Read all sensors, apply EMA, publish to SharedState
    void read_all_sensors();

    /// Apply output requests to actuator drivers
    void apply_all_outputs();

    /// Publish actuator states + has_* flags to SharedState
    void publish_all_states();

#ifdef HOST_BUILD
public:
    /// Test-only: inject mock drivers
    void inject_sensor(const char* role, modesp::ISensorDriver* s);
    void inject_actuator(const char* role, modesp::IActuatorDriver* a);
#endif
};
