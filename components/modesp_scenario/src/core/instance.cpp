/**
 * @file sequence_instance.cpp
 * @brief Per-instance scenario tick (Step 12).
 *
 * Tick algorithm:
 *   1. If global transitions present, evaluate у priority order. First
 *      firing transition aborts scenario (or main track only, depending
 *      on scope).
 *   2. Tick each track in declaration order. Within а tick, track 0 writes
 *      become visible to track 1's reads same tick (tick-order sync,
 *      ADR-0003).
 *   3. Compute completion: depending on completion_rule, scenario → COMPLETED
 *      when satisfied. Failed tracks: scenario → FAILED якщо main track
 *      failed або completion_rule == MAIN_TRACK and main failed.
 */

#include "instance.h"
#include "track.h"
#include "modesp/scenario/i_state_backend.h"

namespace modesp::scenario {

namespace {

/// Find main track index (track з MODR_TRACK_FLAG_MAIN), або 0 if none flagged.
uint8_t find_main_track(const SequenceRuntime& sr) {
    auto* tracks = sr.scenario.tracks();
    for (uint8_t i = 0; i < sr.scenario.header()->track_count; ++i) {
        if (tracks[i].flags & MODR_TRACK_FLAG_MAIN) return i;
    }
    return 0;  // default to first track
}

/// Evaluate global transitions у priority order. Returns true якщо ANY
/// global transition fired (scenario should abort). Caller mutates state.
bool process_global_transitions(SequenceRuntime& sr, IStateBackend* state,
                                const RegistryRefs& regs,
                                bool& main_track_only_out) {
    main_track_only_out = false;
    auto* hdr = sr.scenario.header();
    if (hdr->global_trans_count == 0) return false;
    auto* gts = sr.scenario.global_transitions();

    // Priority sort пре-done by compiler? Plan says compiler sorts descending
    // by priority. Loader doesn't validate sort order, so engine could
    // double-check; для MVP we trust compiler ordering.
    for (uint8_t i = 0; i < hdr->global_trans_count; ++i) {
        const auto& g = gts[i];
        bool time_ok = false;  // global transitions don't have time threshold
        bool cond_ok = (g.cond_pool_idx != MODR_NO_OFFSET)
            && evaluate_condition(sr, sr.tracks[0], 0, g.cond_pool_idx, state, regs);
        bool fires = false;
        switch (g.kind) {
            case MODR_TRANS_KIND_UNCONDITIONAL: fires = true; break;
            case MODR_TRANS_KIND_COND:          fires = cond_ok; break;
            case MODR_TRANS_KIND_TIME_OR_COND:  fires = time_ok || cond_ok; break;
            case MODR_TRANS_KIND_TIME_AND_COND: fires = time_ok && cond_ok; break;
            default:                            fires = false;
        }
        if (fires) {
            main_track_only_out = (g.scope == MODR_GLOBAL_SCOPE_MAIN_TRACK);
            return true;
        }
    }
    return false;
}

}  // anonymous namespace

void instance_start(SequenceRuntime& sr) {
    sr.scenario_elapsed_ms = 0;
    sr.state = SequenceRuntime::State::RUNNING;
    auto* tracks = sr.scenario.tracks();
    for (uint8_t i = 0; i < sr.scenario.header()->track_count; ++i) {
        TrackRuntime& tr = sr.tracks[i];
        tr.state = TrackRuntime::State::RUNNING;
        tr.phase_idx = static_cast<uint8_t>(tracks[i].initial_phase & 0xFF);
        tr.phase_elapsed_ms = 0;
        tr.entry_action_progress = 0;
        tr.exit_action_progress = 0;
        tr.action_pending_ticks = 0;
        tr.pending_target_phase = 0;
        tr.running_exit_actions = false;
    }
}

void instance_abort(SequenceRuntime& sr, ResourceArbiter* arbiter) {
    // MVP-level abort: tracks transition straight to FAILED без running phase
    // exit actions. Phase-scope resources released here so they don't leak
    // (track_tick early-returns on FAILED і would never call release_phase).
    //
    // Recipe authors needing exit-on-abort safety shutdowns (close valve,
    // de-energize heater) MUST implement це via global transitions to а
    // dedicated cleanup phase, NOT через relying on the active phase's
    // exit actions. Full exit-on-abort path (where instance_abort sets
    // tracks to ABORTING і track_tick walks через phase exit actions
    // before terminating) is а Stage 1.5 enhancement.
    //
    // Note: TrackRuntime::ABORTING docstring previously claimed exit actions
    // run у це state. That's true ONLY when а transition fires з
    // target=$abort (per-phase abort), NOT for scenario-level abort.
    sr.state = SequenceRuntime::State::ABORTING;
    for (uint8_t i = 0; i < sr.scenario.header()->track_count; ++i) {
        TrackRuntime& tr = sr.tracks[i];
        if (tr.state == TrackRuntime::State::COMPLETED
         || tr.state == TrackRuntime::State::FAILED) continue;
        if (arbiter) arbiter->release_phase(sr.handle, i);
        tr.state = TrackRuntime::State::FAILED;
    }
}

bool completion_satisfied(const SequenceRuntime& sr) {
    auto* hdr = sr.scenario.header();
    using TS = TrackRuntime::State;
    switch (hdr->completion_rule) {
        case MODR_COMPLETION_ALL_TRACKS: {
            for (uint8_t i = 0; i < hdr->track_count; ++i) {
                if (sr.tracks[i].state != TS::COMPLETED) return false;
            }
            return true;
        }
        case MODR_COMPLETION_ANY_TRACK: {
            for (uint8_t i = 0; i < hdr->track_count; ++i) {
                if (sr.tracks[i].state == TS::COMPLETED) return true;
            }
            return false;
        }
        case MODR_COMPLETION_MAIN_TRACK: {
            uint8_t main = find_main_track(sr);
            return sr.tracks[main].state == TS::COMPLETED;
        }
        default:
            return false;
    }
}

void instance_tick(SequenceRuntime& sr, uint32_t dt_ms,
                   IStateBackend* state, const RegistryRefs& regs,
                   ResourceArbiter* arbiter) {
    using SS = SequenceRuntime::State;
    using TS = TrackRuntime::State;
    if (sr.state != SS::RUNNING && sr.state != SS::ABORTING) return;

    // Saturating increment: clamp to UINT32_MAX instead of wrapping at
    // ~49.7 days. Same rationale as track_tick's phase_elapsed_ms.
    if (UINT32_MAX - dt_ms < sr.scenario_elapsed_ms) {
        sr.scenario_elapsed_ms = UINT32_MAX;
    } else {
        sr.scenario_elapsed_ms += dt_ms;
    }
    auto* hdr = sr.scenario.header();

    // 1. Global transitions
    if (sr.state == SS::RUNNING) {
        bool main_only = false;
        if (process_global_transitions(sr, state, regs, main_only)) {
            if (main_only) {
                uint8_t main = find_main_track(sr);
                sr.tracks[main].state = TS::FAILED;
                if (arbiter) arbiter->release_phase(sr.handle, main);
            } else {
                instance_abort(sr, arbiter);
            }
        }
    }

    // 2. Tick each track in declaration order (cross-track sync via SharedState).
    for (uint8_t i = 0; i < hdr->track_count; ++i) {
        track_tick(sr, i, dt_ms, state, regs, arbiter);
    }

    // 3. Completion check (if still RUNNING)
    if (sr.state == SS::RUNNING) {
        // Has any track failed і failure forces scenario fail?
        // Per plan: scenario → FAILED якщо main track FAILED
        // (also covered коли completion_rule=MAIN_TRACK і main fails).
        uint8_t main = find_main_track(sr);
        if (sr.tracks[main].state == TS::FAILED) {
            sr.state = SS::FAILED;
            if (arbiter) arbiter->release_scenario(sr.handle);
            return;
        }

        if (completion_satisfied(sr)) {
            sr.state = SS::COMPLETED;
            if (arbiter) arbiter->release_scenario(sr.handle);
        }
    } else if (sr.state == SS::ABORTING) {
        // Wait для all non-completed tracks to reach FAILED, then mark scenario FAILED.
        bool any_running = false;
        for (uint8_t i = 0; i < hdr->track_count; ++i) {
            if (sr.tracks[i].state != TS::COMPLETED
             && sr.tracks[i].state != TS::FAILED) {
                any_running = true;
                break;
            }
        }
        if (!any_running) {
            sr.state = SS::FAILED;
            if (arbiter) arbiter->release_scenario(sr.handle);
        }
    }
}

}  // namespace modesp::scenario
