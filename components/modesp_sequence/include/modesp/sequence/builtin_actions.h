/**
 * @file builtin_actions.h
 * @brief Registration entry point для built-in actions і conditions.
 *
 * Usage (from main.cpp or engine init):
 *
 *     #include "modesp/sequence/builtin_actions.h"
 *     ...
 *     modesp::sequence::builtins::register_builtins();
 *
 * Built-in catalog (matches tools/known_actions.json):
 *
 * Actions (3):
 *   log          {msg: string}                       — log diagnostic message
 *   set_state    {key: str, type: i32, value: typed} — write SharedState key
 *   wait_ms      {ms: i32}                           — wait specified ms
 *
 * Conditions (10 leaf — composite all_of/any_of/not handled inline у engine):
 *   time_elapsed_ms     {ms: i32}                — phase_elapsed_ms >= ms
 *   state_key_eq/ne/lt/gt/le/ge {key, value}     — typed comparison
 *   state_key_in_range  {key, min, max}          — bounds check
 *   state_key_changed   {key}                    — edge detect (engine tracks prev)
 *   time_of_day_eq      {hh, mm}                 — wall-clock match (requires SNTP)
 *
 * Domain modules add additional actions через ActionRegistry::register_action()
 * after register_builtins() completes.
 */

#pragma once

#include <stdbool.h>

namespace modesp::sequence::builtins {

/// Register all built-in actions і conditions з ActionRegistry::instance().
/// Idempotent: calling twice не registers duplicates (returns true on second
/// call якщо first succeeded, false if any registration fails).
/// Returns true if все built-ins registered successfully.
bool register_builtins();

/// Diagnostic — count of expected built-in actions (currently 3).
/// Used by tests до verify register_builtins() completeness.
constexpr int BUILTIN_ACTION_COUNT = 3;

/// Diagnostic — count of expected leaf conditions (currently 10).
/// Composite conditions (all_of, any_of, not) handled inline у engine,
/// not registered as separate functions.
constexpr int BUILTIN_CONDITION_COUNT = 10;

}  // namespace modesp::sequence::builtins
