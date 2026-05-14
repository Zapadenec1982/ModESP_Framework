/**
 * @file runtime_types.h
 * @brief Runtime state structs для tracks і instances. Pure POD types.
 *
 * Owned by Engine (multi-instance, fixed-capacity slot array). Track state +
 * scenario state machines per
 * documentation/en/03-framework-reference/scenario-engine/04_state_machines.md.
 *
 * Lift from `modesp_sequence/sequence_state.h` (Phase 1):
 *   - namespace `sequence` → `scenario`
 *   - file renamed to `runtime_types.h` (more accurate)
 *   - `SequenceRuntime` retained — it's the per-instance runtime state object.
 *
 * Engine ticks these per update(); track FSM у src/core/track.cpp, instance
 * FSM у src/core/instance.cpp.
 */

#pragma once

#include "modesp/scenario/action_param.h"
#include "modesp/scenario/modr_loader.h"

#include <cstdint>

namespace modesp::scenario {

/// Per-track state machine. Each track within scenario advances independently
/// through its own phase sequence. Phase entry/exit actions, transition
/// evaluation, і resource arbitration all keyed off TrackRuntime fields.
struct TrackRuntime {
    enum class State : uint8_t {
        IDLE = 0,                ///< before scenario start
        RUNNING,                 ///< executing phase entry/dwell/transitions
        WAITING_FOR_RESOURCE,    ///< phase entry blocked by phase-scope claim
        ABORTING,                ///< per-phase $abort transition fired; running phase
                                 ///< exit actions, then → FAILED. NOTE: scenario-level
                                 ///< abort does NOT enter це state — it transitions
                                 ///< tracks straight to FAILED. Stage 1.5.
        COMPLETED,               ///< reached MODR_TARGET_COMPLETE
        FAILED,                  ///< MODR_TARGET_ABORT або action FAILED_ABORT
                                 ///< OR scenario-level abort
    };

    State state = State::IDLE;
    uint8_t  phase_idx = 0;            ///< current phase index у track
    uint8_t  entry_action_progress = 0;///< how many entry actions completed
    uint8_t  exit_action_progress = 0; ///< how many exit actions completed
    uint8_t  action_pending_ticks = 0; ///< consecutive PENDING ticks (escalates after N)
    uint16_t pending_target_phase = 0; ///< latched transition target while exit actions run
    bool     running_exit_actions = false; ///< true коли exit actions in flight
    uint32_t phase_elapsed_ms = 0;     ///< ms since current phase entered
};

/// Per-instance scenario runtime state. Engine maintains array of these
/// (capacity MAX_SEQUENCES). Public API methods operate on instance addressed
/// by SequenceHandle. Name preserved (`SequenceRuntime`) because semantically
/// це per-execution state, not "sequence engine" coupling.
struct SequenceRuntime {
    enum class State : uint8_t {
        IDLE = 0,        ///< slot empty
        LOADED,          ///< .modr loaded і validated, awaiting start()
        RUNNING,
        PAUSED,
        ABORTING,        ///< global transition fired або abort() called; running exits
        COMPLETED,       ///< completion_rule satisfied
        FAILED,          ///< abort path completed
    };

    State state = State::IDLE;
    SequenceHandle handle = 0;
    LoadedScenario scenario;        ///< view into owned buffer
    uint32_t scenario_elapsed_ms = 0;
    TrackRuntime tracks[6];         ///< MAX_TRACKS_PER_SCENARIO (per modr_loader.h)
};

}  // namespace modesp::scenario
