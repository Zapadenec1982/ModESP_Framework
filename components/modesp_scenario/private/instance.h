/**
 * @file instance.h (private)
 * @brief Per-instance scenario tick logic.
 *
 * Internal API. Engine calls `instance_tick()` для every running instance
 * once per engine tick. Inside: global transitions evaluated FIRST, then
 * each track ticks у declaration order, then completion_rule checked.
 *
 * Scenario FSM:
 *   IDLE → LOADED (after modr_validate в external load() call)
 *   LOADED → RUNNING (start() — after acquiring scenario-scope resources)
 *   RUNNING → PAUSED → RUNNING (pause()/resume())
 *   RUNNING → ABORTING → FAILED (global transition fired або abort() called)
 *   RUNNING → COMPLETED (per completion_rule satisfied)
 *
 * Tick-order cross-track sync: each track reads state backend live; producer
 * track must be declared before consumer (ADR-0003).
 *
 * **Lift from `modesp_sequence/sequence_instance.h` (Phase 1):**
 *   - File renamed `sequence_instance.h` → `instance.h`, moved into `private/`.
 *   - `modesp::SharedState*` → `IStateBackend*`.
 *   - Functions take `RegistryRefs` bundle (no more singleton registry access).
 */

#pragma once

#include "modesp/scenario/runtime_types.h"
#include "modesp/scenario/resource_arbiter.h"
#include "track.h"  // RegistryRefs

namespace modesp::scenario {

class IStateBackend;

/// Initialise scenario runtime для start. Sets state RUNNING, resets all
/// elapsed_ms, marks each track RUNNING from initial_phase.
void instance_start(SequenceRuntime& sr);

/// One tick of one instance. Drives global-transition eval, per-track ticks,
/// і completion-rule checking. No-op якщо state ∉ {RUNNING, ABORTING}.
void instance_tick(SequenceRuntime& sr, uint32_t dt_ms,
                   IStateBackend* state, const RegistryRefs& regs,
                   ResourceArbiter* arbiter);

/// Returns true якщо scenario completion_rule satisfied based on current
/// per-track states. Used by instance_tick і tests.
bool completion_satisfied(const SequenceRuntime& sr);

/// Trigger abort. Sets scenario state ABORTING і forces every non-terminal
/// track to FAILED. Phase-scope resources released через arbiter (if non-null)
/// so they don't leak.
void instance_abort(SequenceRuntime& sr, ResourceArbiter* arbiter = nullptr);

}  // namespace modesp::scenario
