/**
 * @file sequence_state.h
 * @brief Runtime state structs для tracks і instances. Pure POD types.
 *
 * Owned by SequenceEngine (Step 14 wraps these у array of fixed-capacity slots).
 * Track state + scenario state machines defined per plan Q6 і docs/sequence_engine/
 * 04_state_machines.md.
 *
 * Steps 11-13 implement the tick logic that mutates these structs;
 * Step 14 wires multi-instance dispatch on top.
 */

#pragma once

#include "modesp/sequence/action_param.h"
#include "modesp/sequence/modr_loader.h"

#include <cstdint>

namespace modesp::sequence {

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
                                 ///< abort (instance_abort) does NOT enter це state —
                                 ///< it transitions tracks straight to FAILED. Stage 1.5.
        COMPLETED,               ///< reached MODR_TARGET_COMPLETE
        FAILED,                  ///< triggered MODR_TARGET_ABORT або action FAILED_ABORT
                                 ///< OR scenario-level abort
    };

    State state = State::IDLE;
    uint8_t  phase_idx = 0;            ///< current phase index у track
    uint8_t  entry_action_progress = 0;///< how many entry actions completed
    uint8_t  exit_action_progress = 0; ///< how many exit actions completed (during transition/abort)
    uint8_t  action_pending_ticks = 0; ///< consecutive PENDING ticks (escalates після N — Step 14)
    uint16_t pending_target_phase = 0; ///< latched transition target while exit actions run
    bool     running_exit_actions = false; ///< true коли exit actions in flight
    uint32_t phase_elapsed_ms = 0;     ///< ms since current phase entered
};

/// Per-instance scenario state. SequenceEngine maintains array of these
/// (capacity MAX_SEQUENCES). Public API methods (load/start/pause/...) operate
/// on instance addressed by SequenceHandle.
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

}  // namespace modesp::sequence
