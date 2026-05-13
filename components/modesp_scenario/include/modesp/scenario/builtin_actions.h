/**
 * @file builtin_actions.h
 * @brief Registration entry point для built-in actions і conditions.
 *
 * Usage (з main.cpp):
 *
 *     #include "modesp/scenario/builtin_actions.h"
 *     #include "modesp/scenario/action_registry.h"
 *     ...
 *     modesp::scenario::ActionRegistry reg;
 *     modesp::scenario::builtins::register_builtins(reg);
 *
 * Built-in catalog (matches tools/known_actions.json):
 *
 * Actions (3):
 *   log          {msg: string}                       — log diagnostic message
 *   set_state    {key: str, type: i32, value: typed} — write state via backend
 *   wait_ms      {ms: i32}                           — wait specified ms
 *
 * Conditions (10 leaf — composite all_of/any_of/not handled inline у engine):
 *   time_elapsed_ms     {ms: i32}                — phase_elapsed_ms >= ms
 *   state_key_eq/ne/lt/gt/le/ge {key, value}     — typed comparison
 *   state_key_in_range  {key, min, max}          — bounds check
 *   state_key_changed   {key}                    — edge detect (engine tracks prev)
 *   time_of_day_eq      {hh, mm}                 — wall-clock match (requires SNTP)
 *
 * Domain modules add additional actions via `reg.register_action()` after
 * `register_builtins(reg)` completes.
 *
 * **Lift from `modesp_sequence/builtin_actions.h` (Phase 1):**
 *   - `register_builtins()` тепер takes ActionRegistry& parameter (singleton
 *     killed; engine takes registry as DI parameter).
 *   - Actions write state through `ctx.state->set(...)` де `state` is
 *     `IStateBackend*`, not `modesp::SharedState*` directly.
 */

#pragma once

namespace modesp::scenario {
class ActionRegistry;  // forward
}

namespace modesp::scenario::builtins {

/// Register all built-in actions і conditions into supplied registry.
/// Returns true if all built-ins registered successfully.
bool register_builtins(ActionRegistry& registry);

/// Diagnostic — count of expected built-in actions (currently 3).
constexpr int BUILTIN_ACTION_COUNT = 3;

/// Diagnostic — count of expected leaf conditions (currently 10).
constexpr int BUILTIN_CONDITION_COUNT = 10;

}  // namespace modesp::scenario::builtins
