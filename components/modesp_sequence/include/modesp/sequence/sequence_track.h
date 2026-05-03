/**
 * @file sequence_track.h
 * @brief Per-track tick logic + condition evaluator (Step 11).
 *
 * Stateless functions operating on TrackRuntime references. Engine (Step 14)
 * calls track_tick() для each track of each running instance once per engine
 * update tick.
 *
 * Per-track FSM (per plan Q6, docs/sequence_engine/04_state_machines.md):
 *   IDLE → RUNNING (on instance start)
 *   RUNNING → COMPLETED (transition target == $complete)
 *   RUNNING → ABORTING → FAILED (transition target == $abort, або action FAILED_ABORT)
 *   RUNNING ↔ WAITING_FOR_RESOURCE (phase resource contention)
 *
 * Cross-track sync semantics (per ADR-0003, plan Q6):
 *   Tick-order у declaration order. Within а tick, track 0 writes a
 *   SharedState key, track 1 reads same key — track 1 sees fresh value.
 *   No snapshot, just live reads. Author's responsibility to declare
 *   producer track before consumer track.
 */

#pragma once

#include "modesp/sequence/sequence_state.h"
#include "modesp/sequence/resource_arbiter.h"

namespace modesp {
class SharedState;
}

namespace modesp::sequence {

class ActionRegistry;

/**
 * Evaluate condition at cond_pool_idx within scenario context. Returns true
 * якщо condition holds. Composite conditions (all_of/any_of/not) recursed
 * inline до MAX_CONDITION_DEPTH. Leaf conditions invoked through
 * ActionRegistry::find_condition.
 *
 * `context` provides phase_elapsed_ms, scenario_elapsed_ms, etc. fields.
 * SharedState pointer used by state_key_* leaf conditions.
 */
bool evaluate_condition(const SequenceRuntime& sr, const TrackRuntime& tr,
                        TrackIdx track_idx, uint16_t cond_pool_idx,
                        modesp::SharedState* state, uint8_t depth = 0);

/**
 * Execute one tick of a track. Advances entry actions, evaluates transitions,
 * runs exit actions on transition fire, handles phase resource acquisition.
 *
 * @param sr           parent scenario runtime (shared elapsed_ms, handle, etc.)
 * @param track_idx    index of track within sr.tracks[]
 * @param dt_ms        ticks since previous tick (typically 10 ms)
 * @param state        SharedState handle for action invocations і condition eval
 * @param arbiter      resource arbiter для phase-scope claims
 *
 * On track completion, releases any held phase-scope resources.
 */
void track_tick(SequenceRuntime& sr, TrackIdx track_idx, uint32_t dt_ms,
                modesp::SharedState* state, ResourceArbiter* arbiter);

}  // namespace modesp::sequence
