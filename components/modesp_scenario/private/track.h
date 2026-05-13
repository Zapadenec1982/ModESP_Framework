/**
 * @file track.h (private)
 * @brief Per-track tick logic + condition evaluator.
 *
 * Internal API. Engine calls `track_tick()` для each track of each running
 * instance once per update tick.
 *
 * Per-track FSM (per ADR-0003):
 *   IDLE → RUNNING (on instance start)
 *   RUNNING → COMPLETED (transition target == $complete)
 *   RUNNING → ABORTING → FAILED (target == $abort або action FAILED_ABORT)
 *   RUNNING ↔ WAITING_FOR_RESOURCE (phase resource contention)
 *
 * Cross-track sync: tick-order semantics — track 0 writes a state key,
 * track 1 (later у same tick) reads fresh value. No snapshot. Author
 * declares producer track перед consumer.
 *
 * **Lift from `modesp_sequence/sequence_track.h` (Phase 1):**
 *   - File renamed `sequence_track.h` → `track.h`, moved into `private/`
 *     (engine-internal, not part of public API).
 *   - `modesp::SharedState*` → `IStateBackend*`.
 *   - `ActionRegistry::instance()` removed — engine passes registry into
 *     `track_tick` via `RegistryRefs` bundle.
 */

#pragma once

#include "modesp/scenario/runtime_types.h"
#include "modesp/scenario/resource_arbiter.h"

namespace modesp::scenario {

class IStateBackend;
class ActionRegistry;
class ContinuousRegistry;

/// Bundle passed to instance/track tick functions. Held by Engine, reference
/// passed down each tick. Keeps function signatures clean (one ref instead
/// of three).
struct RegistryRefs {
    const ActionRegistry*     actions;
    const ContinuousRegistry* continuous;  // reserved для Stage 1.5 continuous wiring
};

/**
 * Evaluate condition at cond_pool_idx within scenario context. Returns true
 * якщо condition holds. Composite conditions (all_of/any_of/not) recursed
 * inline до MAX_CONDITION_DEPTH. Leaf conditions invoked через `actions`
 * registry.
 */
bool evaluate_condition(const SequenceRuntime& sr, const TrackRuntime& tr,
                        TrackIdx track_idx, uint16_t cond_pool_idx,
                        IStateBackend* state, const RegistryRefs& regs,
                        uint8_t depth = 0);

/**
 * Execute one tick of a track. Advances entry actions, evaluates transitions,
 * runs exit actions on transition fire, handles phase resource acquisition.
 *
 * @param sr         parent scenario runtime
 * @param track_idx  index of track within sr.tracks[]
 * @param dt_ms      ticks since previous tick (typically 10 ms)
 * @param state      state backend для action invocations і condition eval
 * @param regs       action/continuous registries injected by engine
 * @param arbiter    resource arbiter для phase-scope claims
 *
 * On track completion, releases any held phase-scope resources.
 */
void track_tick(SequenceRuntime& sr, TrackIdx track_idx, uint32_t dt_ms,
                IStateBackend* state, const RegistryRefs& regs,
                ResourceArbiter* arbiter);

}  // namespace modesp::scenario
