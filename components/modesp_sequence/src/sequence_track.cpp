/**
 * @file sequence_track.cpp
 * @brief Per-track tick implementation (Step 11).
 *
 * Algorithm summary (one tick of one track):
 *   1. If state ∈ {COMPLETED, FAILED, IDLE}: nothing to do.
 *   2. If WAITING_FOR_RESOURCE: try to claim phase-scope resources; on success
 *      → RUNNING (start phase entry actions next tick). On phase timeout while
 *      waiting → ABORTING.
 *   3. If running_exit_actions: process one exit action per tick. When done,
 *      apply pending transition (advance phase, complete, or abort).
 *   4. Otherwise (RUNNING normal):
 *      a. Increment phase_elapsed_ms
 *      b. Process one entry action per tick (if any pending)
 *      c. After all entry actions done: evaluate transitions in declaration
 *         order. First firing transition: latch target і begin running exit
 *         actions next tick.
 *      d. Check phase timeout (if applicable і no transition fired yet)
 *
 * One action invocation per tick keeps cost bounded і predictable. Long-running
 * actions return PENDING і are re-invoked next tick. Composite conditions
 * recurse inline (no per-tick stepping для evaluation).
 */

#include "modesp/sequence/sequence_track.h"
#include "modesp/sequence/action_registry.h"
#include "modesp/shared_state.h"

#include <cstring>

namespace modesp::sequence {

// ─────────────────────────────────────────────────────────────────────
// Condition evaluation (recursive для composite ops)
// ─────────────────────────────────────────────────────────────────────

namespace {

/// Build ActionContext from track + scenario state. Used for both action
/// invocation і condition evaluation. Conditions don't write SharedState
/// but the contract requires non-null state pointer when state_key_* used.
ActionContext make_action_context(const SequenceRuntime& sr,
                                  const TrackRuntime& tr,
                                  TrackIdx track_idx,
                                  modesp::SharedState* state,
                                  uint16_t param_idx, uint8_t param_n) {
    ActionContext ctx{};
    ctx.state = state;
    // modr_param_entry і ActionParam are layout-identical (8 bytes, same field
    // order — both validated by static_assert у respective headers).
    // Reinterpret cast safe by spec; would need conversion if layouts diverge.
    ctx.params = (param_n > 0)
        ? reinterpret_cast<const ActionParam*>(sr.scenario.param_pool() + param_idx)
        : nullptr;
    ctx.param_count = param_n;
    ctx.string_pool = sr.scenario.string_pool_data();
    ctx.string_pool_size = sr.scenario.string_pool_size();
    ctx.scenario_elapsed_ms = sr.scenario_elapsed_ms;
    ctx.phase_elapsed_ms = tr.phase_elapsed_ms;
    ctx.phase_idx = tr.phase_idx;
    ctx.handle = sr.handle;
    ctx.track = track_idx;
    // recipe_name + track_name resolved from string pool (skip для now —
    // logging actions can use them but не critical для state machine logic).
    ctx.recipe_name = nullptr;
    ctx.track_name = nullptr;
    return ctx;
}

}  // anonymous namespace

bool evaluate_condition(const SequenceRuntime& sr, const TrackRuntime& tr,
                        TrackIdx track_idx, uint16_t cond_pool_idx,
                        modesp::SharedState* state, uint8_t depth) {
    if (depth >= MAX_CONDITION_DEPTH) return false;
    if (cond_pool_idx >= sr.scenario.header()->cond_pool_count) return false;
    const modr_action& cond = sr.scenario.cond_pool()[cond_pool_idx];

    // Composite: recurse over child indices stored у params з type=i32
    if (is_composite_cond(cond.action_hash)) {
        if (cond.param_n == 0) return false;
        const modr_param_entry* params = sr.scenario.param_pool() + cond.param_idx;
        if (cond.action_hash == MODR_COND_HASH_NOT) {
            int32_t child_idx;
            std::memcpy(&child_idx, &params[0].value, sizeof(int32_t));
            return !evaluate_condition(sr, tr, track_idx,
                                        static_cast<uint16_t>(child_idx),
                                        state, depth + 1);
        }
        if (cond.action_hash == MODR_COND_HASH_ALL_OF) {
            for (uint8_t i = 0; i < cond.param_n; ++i) {
                int32_t child_idx;
                std::memcpy(&child_idx, &params[i].value, sizeof(int32_t));
                if (!evaluate_condition(sr, tr, track_idx,
                                         static_cast<uint16_t>(child_idx),
                                         state, depth + 1)) {
                    return false;
                }
            }
            return true;
        }
        if (cond.action_hash == MODR_COND_HASH_ANY_OF) {
            for (uint8_t i = 0; i < cond.param_n; ++i) {
                int32_t child_idx;
                std::memcpy(&child_idx, &params[i].value, sizeof(int32_t));
                if (evaluate_condition(sr, tr, track_idx,
                                        static_cast<uint16_t>(child_idx),
                                        state, depth + 1)) {
                    return true;
                }
            }
            return false;
        }
        return false;
    }

    // Leaf condition: invoke registered ActionFn through ActionRegistry
    auto* desc = ActionRegistry::instance().find_condition(cond.action_hash);
    if (desc == nullptr) return false;
    ActionContext ctx = make_action_context(sr, tr, track_idx, state,
                                            cond.param_idx, cond.param_n);
    ActionStatus status = desc->fn(ctx);
    return status == ActionStatus::OK;
}

// ─────────────────────────────────────────────────────────────────────
// Transition firing predicate
// ─────────────────────────────────────────────────────────────────────

namespace {

bool transition_fires(const SequenceRuntime& sr, const TrackRuntime& tr,
                      TrackIdx track_idx, const modr_transition& t,
                      modesp::SharedState* state) {
    bool time_satisfied = (tr.phase_elapsed_ms >= t.time_threshold_ms);
    bool cond_satisfied = (t.cond_pool_idx != MODR_NO_OFFSET)
        && evaluate_condition(sr, tr, track_idx, t.cond_pool_idx, state);

    switch (t.kind) {
        case MODR_TRANS_KIND_UNCONDITIONAL: return true;
        case MODR_TRANS_KIND_TIME:          return time_satisfied;
        case MODR_TRANS_KIND_COND:          return cond_satisfied;
        case MODR_TRANS_KIND_TIME_OR_COND:  return time_satisfied || cond_satisfied;
        case MODR_TRANS_KIND_TIME_AND_COND: return time_satisfied && cond_satisfied;
        default:                            return false;
    }
}

/// Run one action by index у action_pool. Returns the ActionStatus.
ActionStatus invoke_action_at(const SequenceRuntime& sr, TrackRuntime& tr,
                              TrackIdx track_idx, uint16_t action_idx,
                              modesp::SharedState* state) {
    const modr_action& a = sr.scenario.action_pool()[action_idx];
    auto* desc = ActionRegistry::instance().find_action(a.action_hash);
    if (desc == nullptr) return ActionStatus::FAILED_ABORT;
    ActionContext ctx = make_action_context(sr, tr, track_idx, state,
                                            a.param_idx, a.param_n);
    return desc->fn(ctx);
}

/// Apply latched transition target after exit actions complete.
/// Releases phase-scope resources from old phase і attempts acquire of
/// new phase's claims (if any).
void apply_transition_target(SequenceRuntime& sr, TrackRuntime& tr,
                             TrackIdx track_idx, uint16_t target,
                             ResourceArbiter* arbiter) {
    if (arbiter) arbiter->release_phase(sr.handle, track_idx);

    if (target == MODR_TARGET_COMPLETE) {
        tr.state = TrackRuntime::State::COMPLETED;
        return;
    }
    if (target == MODR_TARGET_ABORT) {
        tr.state = TrackRuntime::State::FAILED;
        return;
    }
    // Regular phase advance
    tr.phase_idx = static_cast<uint8_t>(target);
    tr.phase_elapsed_ms = 0;
    tr.entry_action_progress = 0;
    tr.exit_action_progress = 0;
    tr.running_exit_actions = false;

    // Try acquire phase-scope resources of the target phase
    const modr_phase& next = sr.scenario.phases(track_idx)[tr.phase_idx];
    if (next.phase_resource_n > 0 && arbiter) {
        const modr_phase_resource_claim* claims = sr.scenario.phase_resources(
            next.phase_resources_off);
        bool ok = arbiter->try_acquire_phase(sr.handle, track_idx,
                                             tr.phase_idx, claims,
                                             next.phase_resource_n);
        if (!ok) {
            tr.state = TrackRuntime::State::WAITING_FOR_RESOURCE;
            return;
        }
    }
    tr.state = TrackRuntime::State::RUNNING;
}

}  // anonymous namespace

// ─────────────────────────────────────────────────────────────────────
// Public: track_tick
// ─────────────────────────────────────────────────────────────────────

void track_tick(SequenceRuntime& sr, TrackIdx track_idx, uint32_t dt_ms,
                modesp::SharedState* state, ResourceArbiter* arbiter) {
    TrackRuntime& tr = sr.tracks[track_idx];
    using TS = TrackRuntime::State;

    if (tr.state == TS::IDLE
     || tr.state == TS::COMPLETED
     || tr.state == TS::FAILED) {
        return;
    }

    const modr_phase& phase = sr.scenario.phases(track_idx)[tr.phase_idx];

    // Single-source increment of phase_elapsed_ms — applied once per tick
    // regardless of state transitions within the tick. Saturating add prevents
    // uint32 wrap at ~49.7 days: коли phase_elapsed_ms approaches UINT32_MAX,
    // clamp instead of wrapping to 0 (що would unstick time-based transitions
    // in а phase running multi-month). Time-condition firing remains correct
    // because thresholds are also < UINT32_MAX.
    if (UINT32_MAX - dt_ms < tr.phase_elapsed_ms) {
        tr.phase_elapsed_ms = UINT32_MAX;
    } else {
        tr.phase_elapsed_ms += dt_ms;
    }

    // ── WAITING_FOR_RESOURCE: retry phase claim ──
    if (tr.state == TS::WAITING_FOR_RESOURCE) {
        if (phase.phase_resource_n > 0 && arbiter) {
            const modr_phase_resource_claim* claims = sr.scenario.phase_resources(
                phase.phase_resources_off);
            if (arbiter->try_acquire_phase(sr.handle, track_idx, tr.phase_idx,
                                           claims, phase.phase_resource_n)) {
                tr.state = TS::RUNNING;
                // Fall through into RUNNING логіки this same tick
            } else {
                // Still waiting. Check for phase timeout.
                uint32_t timeout = (phase.timeout_ms != 0)
                    ? phase.timeout_ms
                    : sr.scenario.header()->default_phase_timeout_ms;
                if (timeout != 0 && tr.phase_elapsed_ms >= timeout) {
                    tr.state = TS::FAILED;
                }
                return;
            }
        } else {
            tr.state = TS::RUNNING;  // no claims to acquire
        }
    }

    // ── RUNNING normal flow ──
    if (tr.state != TS::RUNNING && tr.state != TS::ABORTING) return;

    // Exit actions in flight (transition latched, running exits in sequence)
    if (tr.running_exit_actions) {
        if (tr.exit_action_progress < phase.exit_action_n) {
            uint16_t idx = static_cast<uint16_t>(phase.exit_action_off + tr.exit_action_progress);
            ActionStatus s = invoke_action_at(sr, tr, track_idx, idx, state);
            if (s == ActionStatus::OK) {
                ++tr.exit_action_progress;
            } else if (s == ActionStatus::PENDING) {
                ++tr.action_pending_ticks;
            } else {
                // any failure during exit → just advance, log via action's own path
                ++tr.exit_action_progress;
                tr.action_pending_ticks = 0;
            }
            return;
        }
        // Exits done — apply latched transition
        uint16_t target = tr.pending_target_phase;
        if (tr.state == TS::ABORTING) {
            tr.state = TS::FAILED;
            if (arbiter) arbiter->release_phase(sr.handle, track_idx);
            return;
        }
        apply_transition_target(sr, tr, track_idx, target, arbiter);
        return;
    }

    // Entry actions
    if (tr.entry_action_progress < phase.entry_action_n) {
        uint16_t idx = static_cast<uint16_t>(phase.entry_action_off + tr.entry_action_progress);
        ActionStatus s = invoke_action_at(sr, tr, track_idx, idx, state);
        switch (s) {
            case ActionStatus::OK:
                ++tr.entry_action_progress;
                tr.action_pending_ticks = 0;
                break;
            case ActionStatus::PENDING:
                ++tr.action_pending_ticks;
                // Step 14 will add escalation policy after N ticks; for MVP
                // tests we just stay PENDING indefinitely (recipes use wait_ms
                // via time conditions instead).
                break;
            case ActionStatus::FAILED_RECOVERABLE:
                // Skip remaining entry actions і fall through to transitions
                tr.entry_action_progress = phase.entry_action_n;
                tr.action_pending_ticks = 0;
                break;
            case ActionStatus::FAILED_ABORT:
                tr.state = TS::FAILED;
                if (arbiter) arbiter->release_phase(sr.handle, track_idx);
                return;
        }
        return;  // one action per tick
    }

    // ── All entry actions done → evaluate transitions ──
    if (phase.transition_n > 0) {
        const modr_transition* trans = sr.scenario.transitions(phase.transitions_off);
        for (uint8_t i = 0; i < phase.transition_n; ++i) {
            if (transition_fires(sr, tr, track_idx, trans[i], state)) {
                // Latch target і start exit actions
                tr.pending_target_phase = trans[i].target_phase;
                tr.running_exit_actions = true;
                tr.exit_action_progress = 0;
                if (trans[i].target_phase == MODR_TARGET_ABORT) {
                    tr.state = TS::ABORTING;
                }
                return;
            }
        }
    }

    // Phase timeout (if no transition fired)
    uint32_t timeout = (phase.timeout_ms != 0)
        ? phase.timeout_ms
        : sr.scenario.header()->default_phase_timeout_ms;
    if (timeout != 0 && tr.phase_elapsed_ms >= timeout) {
        tr.state = TS::FAILED;
        if (arbiter) arbiter->release_phase(sr.handle, track_idx);
    }
}

}  // namespace modesp::sequence
