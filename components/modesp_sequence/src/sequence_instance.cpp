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

#include "modesp/sequence/sequence_instance.h"
#include "modesp/sequence/sequence_track.h"
#include "modesp/shared_state.h"

namespace modesp::sequence {

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
bool process_global_transitions(SequenceRuntime& sr, modesp::SharedState* state,
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
            && evaluate_condition(sr, sr.tracks[0], 0, g.cond_pool_idx, state);
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

void instance_abort(SequenceRuntime& sr) {
    sr.state = SequenceRuntime::State::ABORTING;
    for (uint8_t i = 0; i < sr.scenario.header()->track_count; ++i) {
        TrackRuntime& tr = sr.tracks[i];
        if (tr.state == TrackRuntime::State::COMPLETED
         || tr.state == TrackRuntime::State::FAILED) continue;
        // Force tracks toward FAILED через ABORTING. Currently tracks already
        // running exit actions of their phase — we force-skip і fail.
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
                   modesp::SharedState* state, ResourceArbiter* arbiter) {
    using SS = SequenceRuntime::State;
    using TS = TrackRuntime::State;
    if (sr.state != SS::RUNNING && sr.state != SS::ABORTING) return;

    sr.scenario_elapsed_ms += dt_ms;
    auto* hdr = sr.scenario.header();

    // 1. Global transitions
    if (sr.state == SS::RUNNING) {
        bool main_only = false;
        if (process_global_transitions(sr, state, main_only)) {
            if (main_only) {
                uint8_t main = find_main_track(sr);
                sr.tracks[main].state = TS::FAILED;
                if (arbiter) arbiter->release_phase(sr.handle, main);
            } else {
                instance_abort(sr);
            }
        }
    }

    // 2. Tick each track in declaration order (cross-track sync via SharedState).
    for (uint8_t i = 0; i < hdr->track_count; ++i) {
        track_tick(sr, i, dt_ms, state, arbiter);
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

}  // namespace modesp::sequence
