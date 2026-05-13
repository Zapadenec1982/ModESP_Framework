/**
 * @file engine.h
 * @brief Multi-instance scenario engine — public API.
 *
 * Engine is а `modesp::BaseModule` що ticks all loaded scenarios on its
 * 100 Hz update task. Each scenario instance occupies one of MAX_SEQUENCES
 * slots (default 2, Kconfig-configurable).
 *
 * **Constructor injection (locked у plan A8-A10):**
 *   - `IStateBackend&` — engine writes mirror keys і forwards to actions
 *   - `ActionRegistry&` — caller-populated, NOT а singleton
 *   - `ContinuousRegistry&` — same
 *   - `etl::span<IEngineObserver*>` — constexpr-known observer list; engine
 *     dispatches events synchronously у tick path
 *
 * No singletons. No global state. Lifetime of injected references must
 * exceed engine's lifetime.
 *
 * **Lift from `modesp_sequence::SequenceEngine` (Phase 1):**
 *   - Renamed to `Engine` (concise; namespace makes "scenario" implicit).
 *   - NVS callbacks moved out (Phase 2 — NvsObserver).
 *   - Mirror keys moved out (direct call to `mirror::publish` у tick).
 *   - `publish_mirror_keys`, `persist_scan`, `persist_slot` deleted.
 */

#pragma once

#include "modesp/scenario/runtime_types.h"
#include "modesp/scenario/resource_arbiter.h"
#include "modesp/scenario/engine_error.h"
#include "modesp/scenario/modr_format.h"  // MODR_MAX_SIZE
#include "modesp/scenario/i_engine_observer.h"
#include "modesp/base_module.h"

#include <etl/span.h>
#include <cstdint>

namespace modesp::scenario {

class IStateBackend;
class ActionRegistry;
class ContinuousRegistry;

/// Maximum concurrent scenario instances. Configured через Kconfig
/// CONFIG_MODESP_MAX_SEQUENCES (default 2 — per-slot cost MODR_MAX_SIZE + ~600 B).
#ifdef CONFIG_MODESP_MAX_SEQUENCES
constexpr size_t MAX_SEQUENCES = CONFIG_MODESP_MAX_SEQUENCES;
#elif defined(MODESP_MAX_SEQUENCES)
constexpr size_t MAX_SEQUENCES = MODESP_MAX_SEQUENCES;
#else
constexpr size_t MAX_SEQUENCES = 2;
#endif

class Engine : public modesp::BaseModule {
public:
    /// Construct engine з injected dependencies.
    ///
    /// @param state       state backend; reference must outlive engine.
    /// @param actions     action/condition registry; caller populates перед start.
    /// @param continuous  continuous behaviour factory registry; caller populates.
    /// @param observers   optional list of side-effect observers (NVS, etc.).
    ///                    Synchronously dispatched з tick path. Empty span OK.
    Engine(IStateBackend& state,
           ActionRegistry& actions,
           ContinuousRegistry& continuous,
           etl::span<IEngineObserver*> observers = {})
        : modesp::BaseModule("scenario", modesp::ModulePriority::HIGH)
        , state_(&state)
        , actions_(&actions)
        , continuous_(&continuous)
        , observers_(observers) {}

    // ── BaseModule interface ──
    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_stop() override;

    // ── Public C++ API ──

    /// Load `.modr` from in-memory buffer. Buffer is copied to internal slot
    /// storage. Returns handle 1..MAX_SEQUENCES on success, 0 on failure
    /// (sets `last_error()`).
    SequenceHandle load_buffer(const uint8_t* data, size_t size);

    /// Load `.modr` from filesystem path. Reads entire file into slot's
    /// buffer, then validates. Host uses fopen; target uses ESP-IDF VFS.
    SequenceHandle load_path(const char* path);

    /// Release slot owned by handle. Releases any scenario-scope resources.
    EngineError unload(SequenceHandle h);

    /// Begin executing scenario. Acquires scenario-scope resources atomically
    /// (RESOURCE_CONTENDED on conflict). State → RUNNING.
    EngineError start(SequenceHandle h);

    /// Pause running scenario. State → PAUSED.
    EngineError pause(SequenceHandle h);

    /// Resume paused scenario. State → RUNNING.
    EngineError resume(SequenceHandle h);

    /// Force ABORTING state. Tracks transition straight to FAILED.
    EngineError abort(SequenceHandle h, uint8_t reason_code = 0);

    // ── Diagnostic accessors ──

    SequenceRuntime::State state(SequenceHandle h) const;
    EngineError last_error() const { return last_error_; }
    uint32_t scenario_elapsed_ms(SequenceHandle h) const;
    uint8_t  track_count(SequenceHandle h) const;
    TrackRuntime::State track_state(SequenceHandle h, TrackIdx t) const;
    uint8_t  track_phase_idx(SequenceHandle h, TrackIdx t) const;
    uint32_t track_phase_elapsed_ms(SequenceHandle h, TrackIdx t) const;

    /// Number of currently-running OR paused instances.
    uint8_t active_count() const;

    /// Direct arbiter access — used by tests і diagnostic APIs.
    ResourceArbiter& arbiter() { return arbiter_; }
    const ResourceArbiter& arbiter() const { return arbiter_; }

    /// Direct registry access — for HTTP handler diagnostics і test fixtures.
    /// Caller should generally use the references they injected — these are
    /// convenience accessors equivalent to those references.
    ActionRegistry&     actions() { return *actions_; }
    const ActionRegistry& actions() const { return *actions_; }
    ContinuousRegistry& continuous() { return *continuous_; }

    /// Slot access for persistence observer (Phase 2).
    /// Returns nullptr якщо handle invalid or slot empty.
    SequenceRuntime* runtime_for(SequenceHandle h);
    const SequenceRuntime* runtime_for(SequenceHandle h) const;

private:
    struct Slot {
        SequenceRuntime runtime;
        uint8_t         buffer[MODR_MAX_SIZE];
        size_t          buffer_size = 0;  // 0 = unused slot
        bool            name_warn_logged = false;
    };

    IStateBackend*       state_;
    ActionRegistry*      actions_;
    ContinuousRegistry*  continuous_;
    etl::span<IEngineObserver*> observers_;
    Slot                 slots_[MAX_SEQUENCES];
    ResourceArbiter      arbiter_;
    EngineError          last_error_ = EngineError::OK;

    // Track terminal-state transition: edge-detect coming from track_count
    // changing OR scenario state moving INTO COMPLETED/FAILED. Set per slot
    // to suppress repeated `on_scenario_terminal` events while engine ticks.
    SequenceRuntime::State last_emitted_state_[MAX_SEQUENCES] = {};
    uint8_t                last_emitted_phase_[MAX_SEQUENCES][6] = {};

    // ── Helpers ──
    int find_free_slot() const;
    Slot*       slot_for(SequenceHandle h);
    const Slot* slot_for(SequenceHandle h) const;

    /// Fire on_scenario_started callbacks across observers (single edge).
    void emit_scenario_started(SequenceHandle h);

    /// Fire on_phase_entered callbacks across observers.
    void emit_phase_entered(SequenceHandle h, TrackIdx t, uint8_t phase_idx);

    /// Fire on_scenario_terminal across observers.
    void emit_scenario_terminal(SequenceHandle h, SequenceRuntime::State s);
};

}  // namespace modesp::scenario
