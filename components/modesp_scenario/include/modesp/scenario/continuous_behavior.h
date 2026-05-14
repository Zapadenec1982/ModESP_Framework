/**
 * @file continuous_behavior.h
 * @brief Abstract base class для continuous behaviors + ContinuousRegistry.
 *
 * ContinuousBehavior — поведінка що активна протягом фази (PID controller,
 * monitor з hysteresis, ramp generator, periodic timer, etc.). Активується
 * через phase.continuous list + cont_mask bits. Engine ticks each active
 * behavior on phase update cycle.
 *
 * MVP ships **0 built-in continuous behaviors** (per ADR-0006). Domain
 * modules register their own factory functions through ContinuousRegistry.
 * This file defines the interface і registry; built-ins added через
 * domain-specific .cpp files (multicooker_pid.cpp, ferment_ramp.cpp, etc.).
 *
 * Specification: documentation/en/03-framework-reference/scenario-engine/03_api_reference.md.
 * ADR rationale: documentation/en/03-framework-reference/scenario-engine/adr/0006-no-builtin-continuous-behaviors.md
 */

#pragma once

#include "modesp/scenario/action_param.h"
#include "etl/flat_map.h"

namespace modesp::scenario {

/**
 * Abstract base for continuous behaviors. Domain modules subclass і register
 * factory function через ContinuousRegistry. Engine у allocates fixed slots
 * per scenario (cont_count у modr_header) і assigns active behaviors per phase.
 *
 * Lifecycle:
 *   on_activate(params, ctx)  — first phase using цю behavior entered
 *   on_tick(dt_ms, ctx)        — kожен engine tick поки phase активна
 *   on_deactivate(ctx)         — phase exited (next phase doesn't use це)
 *   serialize/deserialize     — optional NVS persistence для resume
 *
 * State preserved across phase transitions WHILE behavior active. Reset
 * implicit at on_deactivate. Domain modules choose state policy.
 */
class ContinuousBehavior {
public:
    virtual ~ContinuousBehavior() = default;

    /// Called when first phase з cont_mask bit для це behavior entered.
    /// `params` — array of params from phase declaration; `ctx` provides
    /// SharedState pointer і other engine state.
    virtual void on_activate(const ActionParam* params, uint8_t param_count,
                             const char* string_pool, ActionContext& ctx) = 0;

    /// Called кожен engine tick (typically 100Hz) поки behavior активна.
    /// `dt_ms` — milliseconds since last tick.
    virtual void on_tick(uint32_t dt_ms, ActionContext& ctx) = 0;

    /// Called on phase exit (next phase does NOT use це behavior).
    virtual void on_deactivate(ActionContext& ctx) = 0;

    /// Optional: persist behavior state to NVS for crash recovery.
    /// Default no-op — behaviors без state can ignore. Returns bytes written
    /// (≤ cap), or 0 on failure / no-state.
    virtual size_t serialize(uint8_t* buf, size_t cap) const { (void)buf; (void)cap; return 0; }

    /// Optional: restore from NVS-persisted state. Returns true on success.
    virtual bool deserialize(const uint8_t* buf, size_t len) { (void)buf; (void)len; return true; }

    /// Behavior identifier (matches registered hash). Used by engine для
    /// matching .modr cont_mask bits to actual behavior instances.
    virtual uint16_t hash() const = 0;

    /// Human-readable name (для diagnostics, NOT used для wire lookup).
    virtual const char* name() const = 0;
};

/// Factory function — returns pointer до static singleton instance.
/// Registry calls це коли engine needs the behavior. Caller does NOT delete
/// the returned pointer (it's static/owned by domain module).
using ContinuousFactory = ContinuousBehavior* (*)();

inline constexpr size_t MAX_CONTINUOUS_REGISTRATIONS = 32;

/**
 * Factory registry для continuous behaviors. Caller-owned (NOT а singleton).
 * Same lifecycle і threading model як ActionRegistry — populate з factories
 * before engine.start(), lookups lock-free thereafter.
 *
 * Engine takes а `ContinuousRegistry&` у its constructor. Multiple registries
 * can coexist у same process (one per engine instance).
 */
class ContinuousRegistry {
public:
    ContinuousRegistry() = default;

    ContinuousRegistry(const ContinuousRegistry&) = delete;
    ContinuousRegistry& operator=(const ContinuousRegistry&) = delete;
    ContinuousRegistry(ContinuousRegistry&&) = default;
    ContinuousRegistry& operator=(ContinuousRegistry&&) = default;

    /// Register a factory by hash. Returns true on success. False if:
    /// - hash already registered (collision)
    /// - registry full
    /// - hash != djb2_hash16(name) (consistency check)
    bool register_factory(uint16_t hash, const char* name, ContinuousFactory factory);

    /// Lookup factory by hash. Returns nullptr if not registered.
    /// Caller invokes factory() to get behavior instance pointer.
    ContinuousFactory find(uint16_t hash) const;

    /// Convenience: lookup AND invoke factory. Returns nullptr if не
    /// registered or factory itself returned nullptr.
    ContinuousBehavior* create(uint16_t hash) const;

    /// Diagnostic.
    size_t count() const { return factories_.size(); }

    /// Test isolation helper.
    void clear() { factories_.clear(); }

private:
    using Map = etl::flat_map<uint16_t, ContinuousFactory, MAX_CONTINUOUS_REGISTRATIONS>;
    Map factories_;
};

}  // namespace modesp::scenario
