/**
 * @file nvs_observer.h
 * @brief NVS persistence observer — edge-triggered scenario state persist.
 *
 * Implements `IEngineObserver`. Listens to scenario lifecycle edges
 * (started/phase_entered/terminal) and writes 96-byte tokens to NVS via
 * caller-supplied write callback. Throttle policy preserved verbatim
 * з old `SequenceEngine::persist_scan` (plan A14):
 *
 *   - on_scenario_started → immediate write
 *   - on_scenario_terminal → immediate write
 *   - on_phase_entered (main track) → immediate write
 *   - on_phase_entered (non-main track) → throttle 1 s minimum між writes
 *
 * Engine snapshot обтикає у `seq_token` через `serialize_token()` з
 * `nvs_token.h`. Write callback (passed з main.cpp wiring) writes blob
 * to NVS namespace + key. Observer doesn't link `nvs_flash` directly —
 * stays target-agnostic; host tests inject а mock callback.
 *
 * **Recovery flow:** Engine.try_recover() (Phase 2.5/Phase 3) calls
 * `try_recover_via_observer()` which reads NVS via supplied read callback,
 * validates magic ('SCTK' rejects old 'SQTK'), restores runtime state, sets
 * scenario state to PAUSED (manual resume required).
 */

#pragma once

#include "modesp/scenario/i_engine_observer.h"
#include "modesp/scenario/runtime_types.h"
#include "modesp/scenario/engine_error.h"

#include <cstddef>
#include <cstdint>

namespace modesp::scenario {

class Engine;

/// Write callback signature. `user` is caller-supplied opaque pointer.
/// `slot` is the engine slot index (0-based, не handle). `token`+`len` is
/// the 96-byte payload. Return true on success.
using NvsWriteFn = bool (*)(void* user, uint8_t slot,
                            const uint8_t* token, size_t len);

/// Read callback signature. `slot` — engine slot index.
/// `in_out_len` is input buffer capacity, set to actual bytes read on success.
using NvsReadFn = bool (*)(void* user, uint8_t slot,
                           uint8_t* token_buf, size_t* in_out_len);

/**
 * NVS persistence observer. Registered with engine у constructor via
 * `etl::span<IEngineObserver*>`. Lifetime owned by caller (typically static
 * у main.cpp).
 *
 * **Why an observer, not engine-internal:** mirror writes run every tick
 * (direct call). NVS writes are edge-triggered (state change, phase entry,
 * terminal) — the natural fit для observer pattern. Engine emits events;
 * observer decides what to persist when.
 */
class NvsObserver : public IEngineObserver {
public:
    /// Construct з required callbacks і opaque user pointer.
    /// Both callbacks must remain valid for observer's lifetime.
    /// Engine reference required to read runtime state for serialization
    /// inside callbacks (passed lazily через `bind_engine`).
    NvsObserver(NvsWriteFn write_fn, NvsReadFn read_fn, void* user)
        : write_fn_(write_fn), read_fn_(read_fn), user_(user) {}

    /// Bind engine — must be called після engine construction, before
    /// engine.start(). Required because observer needs to read engine
    /// runtime state to serialize.
    void bind_engine(const Engine& eng) { engine_ = &eng; }

    // ── IEngineObserver overrides ──

    void on_scenario_started(SequenceHandle h) override;
    void on_phase_entered(SequenceHandle h, TrackIdx t, uint8_t phase_idx) override;
    void on_scenario_terminal(SequenceHandle h, SequenceRuntime::State final_state) override;
    void on_tick(uint32_t dt_ms) override;

    // ── Recovery API (called by Engine::try_recover) ──

    /// Attempt to recover persisted state for slot owned by handle. Caller
    /// must have already loaded the .modr (sr.scenario valid). Returns OK
    /// on successful restore, NVS_ERROR if no token, або validation errors.
    EngineError try_recover(SequenceHandle h, SequenceRuntime& sr);

private:
    NvsWriteFn  write_fn_;
    NvsReadFn   read_fn_;
    void*       user_;
    const Engine* engine_ = nullptr;

    // Per-slot throttle state — index by (handle - 1), bounded by
    // MAX_SEQUENCES (defined у engine.h). We allocate а small fixed array
    // following same convention. 8 elements — generous headroom (max
    // Kconfig setting is 8).
    static constexpr size_t MAX_SLOTS = 8;
    uint32_t time_since_persist_ms_[MAX_SLOTS] = {};

    /// Throttle gate. Returns true якщо write should proceed (resets counter).
    /// false → throttled, observer skips цей write.
    bool throttle_check(uint8_t slot_idx);

    /// Persist а snapshot of slot's runtime. Returns true on success.
    bool persist_slot(SequenceHandle h);

    /// Saturating per-slot counter advance — folded into on_tick().
    void advance_throttle(uint8_t slot_idx, uint32_t dt_ms);
};

}  // namespace modesp::scenario
