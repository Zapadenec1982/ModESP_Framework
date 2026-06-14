/**
 * @file nvs_observer.cpp
 * @brief NVS persistence observer impl.
 *
 * Edge-triggered persist: scenario start, phase entry (main = immediate;
 * non-main = 1s throttle), terminal state. Lifted з old
 * SequenceEngine::persist_scan з verbatim throttle policy (plan A14).
 */

#include "modesp/scenario/nvs_observer.h"
#include "modesp/scenario/engine.h"
#include "modesp/scenario/nvs_token.h"
#include "modesp/scenario/modr_format.h"  // MODR_TRACK_FLAG_MAIN

#include <cstdint>

namespace modesp::scenario {

// ── Helpers ──

bool NvsObserver::throttle_check(uint8_t slot_idx) {
    if (slot_idx >= MAX_SLOTS) return false;
    if (time_since_persist_ms_[slot_idx] < 1000) {
        return false;  // throttled
    }
    time_since_persist_ms_[slot_idx] = 0;
    return true;
}

bool NvsObserver::persist_slot(SequenceHandle h, NvsWriteMode mode) {
    if (write_fn_ == nullptr || engine_ == nullptr) return false;
    const SequenceRuntime* sr = engine_->runtime_for(h);
    if (sr == nullptr || sr->scenario.header() == nullptr) return false;

    uint8_t token[SEQ_TOKEN_SIZE];
    uint16_t scenario_id = sr->scenario.header()->scenario_id;
    if (!serialize_token(*sr, scenario_id, /*wall_clock=*/0, token)) {
        return false;
    }
    uint8_t slot_idx = static_cast<uint8_t>(h - 1);
    bool ok = write_fn_(user_, slot_idx, token, SEQ_TOKEN_SIZE, mode);
    if (ok && slot_idx < MAX_SLOTS) {
        time_since_persist_ms_[slot_idx] = 0;
    }
    return ok;
}

// ── Tick advance: фолджу throttle counter across all slots ──

void NvsObserver::advance_throttle(uint8_t slot_idx, uint32_t dt_ms) {
    if (slot_idx >= MAX_SLOTS) return;
    // Saturating add — same convention як engine's elapsed_ms.
    if (UINT32_MAX - dt_ms < time_since_persist_ms_[slot_idx]) {
        time_since_persist_ms_[slot_idx] = UINT32_MAX;
    } else {
        time_since_persist_ms_[slot_idx] += dt_ms;
    }
}

void NvsObserver::on_tick(uint32_t dt_ms) {
    // Advance throttle counters для всіх slots. We don't query engine to
    // skip unloaded slots — counter on empty slot has no observable effect
    // (no persist trigger), cost is negligible (saturating add x 8).
    for (uint8_t i = 0; i < MAX_SLOTS; ++i) {
        advance_throttle(i, dt_ms);
    }
}

// ── IEngineObserver overrides ──

void NvsObserver::on_scenario_started(SequenceHandle h) {
    // Crash-critical event — durable write before return, ignore throttle.
    // Fired from start() (caller/HTTP task, off the control loop), so the
    // synchronous commit here does not stall the engine tick.
    (void)persist_slot(h, NvsWriteMode::CrashCritical);
}

void NvsObserver::on_phase_entered(SequenceHandle h, TrackIdx t, uint8_t /*phase*/) {
    if (engine_ == nullptr) return;
    const SequenceRuntime* sr = engine_->runtime_for(h);
    if (sr == nullptr || sr->scenario.header() == nullptr) return;

    // Main-track phase change: immediate write.
    // Non-main: throttled.
    const auto* tracks = sr->scenario.tracks();
    uint8_t track_count = sr->scenario.header()->track_count;
    bool is_main = (t < track_count) && (tracks[t].flags & MODR_TRACK_FLAG_MAIN);

    uint8_t slot_idx = static_cast<uint8_t>(h - 1);
    // Phase-change edges fire during the engine tick — defer the actual flash
    // write off the control loop (Deferred). Main track = every phase, non-main
    // = throttled, same policy as before; only the durability mode differs.
    if (is_main) {
        (void)persist_slot(h, NvsWriteMode::Deferred);
    } else if (throttle_check(slot_idx)) {
        (void)persist_slot(h, NvsWriteMode::Deferred);
    }
    // else: throttled — counter continues accumulating; next phase change
    // OR scenario state change will trigger write коли gate opens.
}

void NvsObserver::on_scenario_terminal(SequenceHandle h, SequenceRuntime::State /*final_state*/) {
    // Crash-critical edge — durable write before return. Fired from the engine
    // tick, but only once per scenario run, so the one-off commit cost is
    // acceptable; recovery must never falsely re-recover a finished scenario.
    (void)persist_slot(h, NvsWriteMode::CrashCritical);
}

// ── Recovery ──

EngineError NvsObserver::try_recover(SequenceHandle h, SequenceRuntime& sr) {
    if (read_fn_ == nullptr) return EngineError::NVS_ERROR;
    if (sr.scenario.header() == nullptr) return EngineError::NOT_LOADED;

    uint8_t token[SEQ_TOKEN_SIZE];
    size_t len = SEQ_TOKEN_SIZE;
    uint8_t slot_idx = static_cast<uint8_t>(h - 1);
    if (!read_fn_(user_, slot_idx, token, &len)) {
        return EngineError::NVS_ERROR;  // no persisted token
    }
    EngineError err = deserialize_token(token, len, sr);
    if (err != EngineError::OK) {
        return err;  // Old 'SQTK' tokens land here з INVALID_FILE
    }
    // Recovered. Per plan Q8: enter PAUSED (manual resume required).
    sr.state = SequenceRuntime::State::PAUSED;
    if (slot_idx < MAX_SLOTS) {
        time_since_persist_ms_[slot_idx] = 0;
    }
    return EngineError::OK;
}

}  // namespace modesp::scenario
