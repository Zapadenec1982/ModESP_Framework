/**
 * @file action_registry.h
 * @brief Registry mapping uint16 hash → action/condition function pointer.
 *
 * Domain modules register actions і conditions при init through this singleton.
 * Engine при .modr load resolves modr_action.action_hash → ActionDescriptor*
 * через find_action() / find_condition(). Zero-heap — fixed-capacity ETL maps.
 *
 * Two separate registries (actions, conditions) — bywayout вони у separate
 * modr pools. Cross-namespace collisions allowed by design (per Q-discussion
 * Step 2a B2 verification).
 *
 * Specification: docs/sequence_engine/03_api_reference.md (Q3 у plan).
 * ADR rationale: docs/sequence_engine/adr/0006-no-builtin-continuous-behaviors.md
 *   (registry pattern serves both actions і continuous behaviors).
 */

#pragma once

#include "modesp/sequence/action_param.h"
#include "etl/flat_map.h"

namespace modesp::sequence {

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

/// Maximum entries у each pool. Tighter bound у MVP — domain modules add
/// під ~20 actions реально. Bumped у Stage 1.5 if needed via ETL specialization.
inline constexpr size_t MAX_REGISTRY_ENTRIES = 64;

/**
 * Singleton registry. Initialize-on-first-use pattern.
 *
 * Thread safety: registration must complete BEFORE engine starts (typical
 * pattern — domain modules register у on_init, engine у NORMAL/HIGH priority
 * after). Lookups (find_*) are read-only, lock-free після initialization.
 */
class ActionRegistry {
public:
    /// Get singleton instance. Lazy-constructed.
    static ActionRegistry& instance();

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
    size_t action_count() const;
    size_t condition_count() const;

    /// Clear all registrations. Test-only — production engine never calls це.
    /// Provided for unit test isolation.
    void clear_for_tests();

private:
    ActionRegistry() = default;
    ActionRegistry(const ActionRegistry&) = delete;
    ActionRegistry& operator=(const ActionRegistry&) = delete;

    using Map = etl::flat_map<uint16_t, ActionDescriptor, MAX_REGISTRY_ENTRIES>;
    Map actions_;
    Map conditions_;
};

/// Compute djb2_hash16 у constexpr context. Useful для register-by-name patterns:
///     reg.register_action({djb2_hash16_const("log"), "log", &my_log_fn, 1, 1});
constexpr uint16_t djb2_hash16(const char* str) noexcept {
    uint32_t hash = 5381u;
    while (*str) {
        hash = ((hash << 5) + hash) + static_cast<uint8_t>(*str);
        ++str;
    }
    return static_cast<uint16_t>(hash & 0xFFFFu);
}

}  // namespace modesp::sequence
