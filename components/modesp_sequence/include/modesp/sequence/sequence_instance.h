/**
 * @file sequence_instance.h
 * @brief Per-instance scenario tick logic (Step 12).
 *
 * Functions operating on SequenceRuntime. SequenceEngine (Step 14) calls
 * instance_tick() для every running instance once per engine tick. Inside,
 * global transitions evaluated FIRST, then each track ticks у declaration
 * order, then completion_rule checked.
 *
 * Scenario FSM (per plan Q6, ADR-0007 mandatory phase timeouts):
 *   IDLE → LOADED (after modr_validate в external load() call)
 *   LOADED → RUNNING (start() — after acquiring scenario-scope resources)
 *   RUNNING → PAUSED → RUNNING (pause()/resume())
 *   RUNNING → ABORTING → FAILED (global transition fired або abort() called)
 *   RUNNING → COMPLETED (per completion_rule satisfied)
 *
 * Tick-order cross-track sync: each track reads SharedState live; producer
 * track must be declared before consumer (ADR-0003).
 */

#pragma once

#include "modesp/sequence/sequence_state.h"
#include "modesp/sequence/resource_arbiter.h"

namespace modesp {
class SharedState;
}

namespace modesp::sequence {

/// Initialise scenario runtime для start. Sets state RUNNING, resets all
/// elapsed_ms, marks each track RUNNING from initial_phase. Caller must
/// already have acquired scenario-scope resources via arbiter і set sr.handle.
void instance_start(SequenceRuntime& sr);

/// One tick of one instance. Drives global-transition eval, per-track ticks,
/// і completion-rule checking. No-op якщо state ∉ {RUNNING, ABORTING}.
void instance_tick(SequenceRuntime& sr, uint32_t dt_ms,
                   modesp::SharedState* state, ResourceArbiter* arbiter);

/// Returns true якщо scenario completion_rule satisfied based on current
/// per-track states. Used by instance_tick і tests.
bool completion_satisfied(const SequenceRuntime& sr);

/// Trigger abort. Sets state ABORTING; tracks transition through their abort
/// paths (running exit actions if any) і terminate FAILED. Used by external
/// abort() calls (Step 14) і global transitions.
void instance_abort(SequenceRuntime& sr);

}  // namespace modesp::sequence
