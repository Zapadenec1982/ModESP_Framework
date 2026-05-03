/**
 * @file sequence_engine.h
 * @brief Multi-instance scenario engine — public C++ API (Step 14).
 *
 * Top-level facade exposing the engine to business modules. Holds a fixed-size
 * array of scenario slots; each slot owns its `.modr` buffer і a
 * SequenceRuntime. Lifecycle methods (load_path / load_buffer / start / pause /
 * resume / abort / unload) operate on slot addressed by SequenceHandle (1..N).
 *
 * Engine is **not** a separate FreeRTOS task. Per plan Q10, on_update() is
 * called by ModuleManager on its update task at 100 Hz (10 ms tick period).
 * Public API methods (load/start/...) callable from any task — у current MVP
 * these execute synchronously inline; cross-task safety enhancement (queue +
 * sync result slot) deferred to Step 14.5 if profiling shows contention.
 *
 * Memory:
 *   - Per slot: SequenceRuntime ~600 bytes + buffer up to MODR_MAX_SIZE (16 KB)
 *   - Default MAX_SEQUENCES = 4: ~64 KB total. Adjust through Kconfig.
 *
 * Reference: docs/sequence_engine/03_api_reference.md (full API surface),
 * docs/sequence_engine/08_lifecycle.md (state transition diagrams).
 */

#pragma once

#include "modesp/sequence/sequence_state.h"
#include "modesp/sequence/resource_arbiter.h"
#include "modesp/sequence/engine_error.h"
#include "modesp/sequence/modr_format.h"   // MODR_MAX_SIZE
#include "modesp/base_module.h"

#include <cstdint>

namespace modesp {
class SharedState;
}

namespace modesp::sequence {

/// Maximum concurrent scenario instances. Default матча plan Q2 (default 6,
/// Kconfig 2-8). Per-slot memory ~16 KB buffer + ~600 B runtime, so 4 ≈ 66 KB.
#ifndef MODESP_MAX_SEQUENCES
#define MODESP_MAX_SEQUENCES 4
#endif
constexpr size_t MAX_SEQUENCES = MODESP_MAX_SEQUENCES;

/**
 * Multi-instance scenario engine. Owns slot pool, ResourceArbiter, і ticks
 * each running instance once per engine update.
 *
 * Construction: pass SharedState pointer (used by actions і conditions).
 * Pointer must remain valid for engine lifetime.
 *
 * Typical use:
 *   SequenceEngine engine(&shared_state);
 *   engine.on_init();
 *   // ... ModuleManager registration ...
 *   SequenceHandle h = engine.load_buffer(blob, blob_size);
 *   engine.start(h);
 *   // engine.on_update(10) called by ModuleManager at 100 Hz
 */
class SequenceEngine : public modesp::BaseModule {
public:
    explicit SequenceEngine(modesp::SharedState* state = nullptr)
        : modesp::BaseModule("scenario", modesp::ModulePriority::HIGH)
        , state_(state) {}

    /// Inject SharedState pointer post-construction (для main.cpp wiring after
    /// app.state() is available).
    void set_state(modesp::SharedState* s) { state_ = s; }

    // ── BaseModule interface ──

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_stop() override;

    // ── Public C++ API (per plan Q2) ──

    /// Load .modr from в-memory buffer. Buffer is COPIED to slot's internal
    /// storage (engine-owned). Returns handle 1..MAX_SEQUENCES on success,
    /// 0 on failure (sets last_error()).
    ///
    /// Validates blob через modr_validate; rejects on any error.
    SequenceHandle load_buffer(const uint8_t* data, size_t size);

    /// Load .modr from filesystem path (typically /lfs/scenarios/<name>.modr).
    /// Reads entire file into slot's buffer, then calls load_buffer's logic.
    /// Returns 0 on file-not-found, read-error, або validation failure.
    ///
    /// Host build (HOST_BUILD): uses std::fopen. Target: ESP-IDF VFS opens
    /// LittleFS-mounted path.
    SequenceHandle load_path(const char* path);

    /// Release slot owned by handle (no-op якщо not loaded). Releases any
    /// scenario-scope resources held. Returns OK or INVALID_HANDLE.
    EngineError unload(SequenceHandle h);

    /// Begin executing scenario. Acquires scenario-scope resources atomically
    /// (RESOURCE_CONTENDED on conflict). On success, scenario state →
    /// RUNNING і tracks initialise to RUNNING from initial_phase.
    EngineError start(SequenceHandle h);

    /// Pause running scenario. State → PAUSED; on_update no-ops для це slot
    /// until resume(). Returns INVALID_HANDLE / NOT_LOADED if applicable.
    EngineError pause(SequenceHandle h);

    /// Resume paused scenario. State → RUNNING. Returns INVALID_HANDLE /
    /// NOT_LOADED if applicable.
    EngineError resume(SequenceHandle h);

    /// Force scenario into ABORTING state. Tracks transition through their
    /// abort paths (running phase exit actions if applicable) і scenario
    /// reaches FAILED on next tick. `reason_code` recorded for diagnostics
    /// (Stage 1.5: ring buffer of recent aborts).
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

private:
    struct Slot {
        SequenceRuntime runtime;
        uint8_t         buffer[MODR_MAX_SIZE];
        size_t          buffer_size = 0;  // 0 = unused slot
        bool            name_warn_logged = false;  ///< dedup для long-name warning
    };

    modesp::SharedState* state_;
    Slot                 slots_[MAX_SEQUENCES];
    ResourceArbiter      arbiter_;
    EngineError          last_error_ = EngineError::OK;

    /// Find first unused slot index (buffer_size == 0). Returns -1 if все full.
    int find_free_slot() const;

    /// Validate handle і return slot pointer, або nullptr.
    Slot*       slot_for(SequenceHandle h);
    const Slot* slot_for(SequenceHandle h) const;

    /// Write per-tick mirror keys for slot's runtime to SharedState.
    /// Keys: <recipe>.scenario_state, scenario_elapsed_s, AND per-track
    /// <track>_state, _phase_idx, _phase_name, _elapsed_s. Values dedupe
    /// internally у SharedState (no version bump for unchanged values).
    /// Mutates `s.name_warn_logged` коли а name overflow happens (logs once).
    void publish_mirror_keys(Slot& s);
};

}  // namespace modesp::sequence
