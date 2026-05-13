/**
 * @file action_registry.h
 * @brief Registry mapping uint16 hash → action/condition function pointer.
 *
 * Caller (typically main.cpp) constructs an ActionRegistry instance і injects
 * it into the engine constructor + into `register_builtin_actions(reg)`.
 * Engine resolves modr_action.action_hash → ActionDescriptor* during loader
 * validation і during phase entry/exit execution.
 *
 * Two separate flat_maps (actions, conditions) — they live у separate `.modr`
 * pools. Cross-namespace collisions allowed by design (same hash може mean
 * different things в action vs condition context).
 *
 * **Lift from `modesp_sequence/action_registry.h` (Phase 1):**
 *   - Singleton removed (`::instance()` killed). Real coupling problem у old
 *     code was hidden inside this `instance()` accessor — every caller pulled
 *     а global, untestable, init-order-dependent reference.
 *   - Now caller owns lifetime. Engine takes `ActionRegistry&` у constructor.
 *   - Multiple registries can coexist у same process (each engine instance
 *     has its own — useful для embedded test harnesses що run multiple
 *     scenarios in parallel without polluting shared state).
 *
 * Zero-heap — fixed-capacity ETL flat_maps.
 */

#pragma once

#include "modesp/scenario/action_param.h"
#include "modesp/scenario/modr_format.h"  // djb2_hash16 (single source of truth)
#include "etl/flat_map.h"

namespace modesp::scenario {

/// Function pointer signature для action AND condition handlers.
/// Conditions return OK (true) або FAILED_RECOVERABLE (false); other statuses
/// reserved для actions з side effects.
using ActionFn = ActionStatus (*)(ActionContext&);

/// Descriptor що domain module supplies при registration.
/// hash MUST equal djb2_hash16(name) — registry verifies на register.
struct ActionDescriptor {
    uint16_t hash;
    const char* name;
    ActionFn fn;
    uint8_t param_min;
    uint8_t param_max;
};

/// Maximum entries у each pool. Domain modules add ~20 actions realistically.
/// Bumped у Stage 1.5 if needed via ETL specialization.
inline constexpr size_t MAX_REGISTRY_ENTRIES = 64;

/**
 * Action/condition registry. Caller-owned, NOT а singleton.
 *
 * Typical lifetime: static-global у main.cpp, populated via
 * `register_builtin_actions(reg)` + custom domain registrations before
 * engine construction. Live data after engine starts is read-only.
 *
 * Thread safety: lookups (`find_*`) are read-only і lock-free after
 * initialization. Registrations should complete before engine starts
 * (typical setup order). Concurrent register_* calls are NOT safe.
 */
class ActionRegistry {
public:
    ActionRegistry() = default;

    // Movable, non-copyable. Caller manages single ownership.
    ActionRegistry(const ActionRegistry&) = delete;
    ActionRegistry& operator=(const ActionRegistry&) = delete;
    ActionRegistry(ActionRegistry&&) = default;
    ActionRegistry& operator=(ActionRegistry&&) = default;

    /// Register an action descriptor. Returns true on success, false if:
    /// - hash already registered (collision)
    /// - registry is full (MAX_REGISTRY_ENTRIES exceeded)
    /// - hash != djb2_hash16(name) (descriptor inconsistency)
    bool register_action(const ActionDescriptor& d);

    /// Register a condition descriptor (separate namespace from actions).
    bool register_condition(const ActionDescriptor& d);

    /// Lookup action by hash. Returns nullptr if not found.
    const ActionDescriptor* find_action(uint16_t hash) const;

    /// Lookup condition by hash. Returns nullptr if not found.
    const ActionDescriptor* find_condition(uint16_t hash) const;

    /// Diagnostic: number of registered entries.
    size_t action_count() const { return actions_.size(); }
    size_t condition_count() const { return conditions_.size(); }

    /// Clear all registrations. Test isolation helper.
    void clear() { actions_.clear(); conditions_.clear(); }

private:
    using Map = etl::flat_map<uint16_t, ActionDescriptor, MAX_REGISTRY_ENTRIES>;
    Map actions_;
    Map conditions_;
};

// djb2_hash16 lives in modr_format.h — included above. Single source of truth
// avoids ODR violations when both headers are pulled into the same TU.

}  // namespace modesp::scenario
