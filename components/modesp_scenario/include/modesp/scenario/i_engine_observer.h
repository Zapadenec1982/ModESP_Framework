/**
 * @file i_engine_observer.h
 * @brief Engine observer interface — 3 lifecycle hooks for side-effect modules.
 *
 * Engine emits events synchronously у its tick path. Observers react з writes
 * to NVS, external systems, etc. Empty default bodies → compiler inlines
 * unused overrides to no-op (zero runtime cost для observers that ignore
 * given event).
 *
 * **Design (locked у plan A4-A6, A10):**
 *  - Mirror writes are NOT observers (run every tick unconditionally — direct
 *    call). Only edge-triggered side effects belong here.
 *  - **3 hooks** only: scenario_started, phase_entered, scenario_terminal.
 *    Sufficient для NVS persistence policy and any future observer.
 *  - **Empty defaults** — virtual method approach beats tagged union here
 *    because observer overrides only what it cares about; compiler optimizes
 *    away the rest.
 *  - **Observer cannot mutate engine state.** Read-only listener. Prevents
 *    feedback loops.
 *  - Engine accepts observers as `etl::span<IEngineObserver*>` у constructor —
 *    constexpr-known set, no dynamic add/remove у runtime.
 *
 * Reference: docs/scenario_engine/01_architecture.md (observer pattern section).
 */

#pragma once

#include "modesp/scenario/action_param.h"  // SequenceHandle, TrackIdx
#include "modesp/scenario/runtime_types.h" // SequenceRuntime::State

namespace modesp::scenario {

class IEngineObserver {
public:
    virtual ~IEngineObserver() = default;

    /// Fired коли scenario transitions IDLE/LOADED → RUNNING (after start()).
    /// Persistence observer: write initial token immediately.
    virtual void on_scenario_started(SequenceHandle h) { (void)h; }

    /// Fired коли а track enters new phase (including initial phase on start).
    /// Persistence observer: throttled write (main-track immediate, others 1s
    /// debounce).
    virtual void on_phase_entered(SequenceHandle h, TrackIdx t, uint8_t phase_idx) {
        (void)h; (void)t; (void)phase_idx;
    }

    /// Fired коли scenario reaches а terminal state (COMPLETED, FAILED).
    /// Includes ABORTING → FAILED transition. Persistence observer: write
    /// final token immediately.
    virtual void on_scenario_terminal(SequenceHandle h, SequenceRuntime::State final_state) {
        (void)h; (void)final_state;
    }

    /// Tick advance hook. Engine calls це once per `on_update()` for every
    /// observer. Empty default. NvsObserver uses це до accumulate throttle
    /// counters. Generic enough для any observer that needs tick-driven
    /// internal state without touching engine FSM.
    virtual void on_tick(uint32_t dt_ms) { (void)dt_ms; }
};

}  // namespace modesp::scenario
