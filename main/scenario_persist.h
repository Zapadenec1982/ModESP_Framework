/**
 * @file scenario_persist.h
 * @brief Off-loop NVS persistence backend for the scenario engine.
 *
 * The scenario `NvsObserver` is target-agnostic: it serialises a 96-byte token
 * on lifecycle edges and hands it to a `NvsWriteFn` callback together with a
 * `NvsWriteMode` durability hint. This module is the target-side implementation
 * of that callback — it owns the FreeRTOS machinery the observer must not.
 *
 * **Why:** phase-change edges are detected *inside* the 100 Hz engine tick. A
 * synchronous `nvs_set_blob` + `nvs_commit` there (tens of ms of flash I/O)
 * blows the 10 ms tick budget and stalls the control loop. This backend moves
 * the flash write to a dedicated low-priority drain task.
 *
 * **Crash-recovery correctness (single writer + FIFO):**
 *   - One queue, one drain task — the *only* code that calls `nvs_set_blob` for
 *     the "scnstate" namespace. So writes never reorder against each other.
 *   - `Deferred` (phase changes): enqueue and return immediately — no tick stall.
 *   - `CrashCritical` (scenario start / terminal): enqueue, then block until the
 *     drain task has committed that item. Because the queue is FIFO, by the time
 *     the crash-critical item commits, every earlier Deferred write for that
 *     slot has already been flushed — so a stale phase token can never land on
 *     top of a terminal token, and the token is durable before the edge handler
 *     returns. `on_scenario_started` runs on the HTTP task (off the loop) so the
 *     block is free; `on_scenario_terminal` blocks the tick once per run only.
 *
 * If FreeRTOS allocation fails at init, the module degrades to a direct
 * synchronous write for every mode (old behaviour) so persistence still works.
 */

#pragma once

#include "modesp/scenario/nvs_observer.h"  // NvsWriteMode

#include <cstddef>
#include <cstdint>

namespace modesp::scenario_persist {

/// Create the queue, flush primitives, and drain task. Call once during wiring,
/// before the engine starts running scenarios. Idempotent. Returns true if the
/// async path is active; false means allocation failed and writes fall back to
/// synchronous (still correct, just on-loop).
bool init();

/// Write callback — matches `modesp::scenario::NvsWriteFn`. Deferred writes are
/// queued; CrashCritical writes block until durably committed.
bool write(void* user, uint8_t slot, const uint8_t* token, std::size_t len,
           modesp::scenario::NvsWriteMode mode);

/// Read callback — matches `modesp::scenario::NvsReadFn`. Synchronous NVS read
/// (recovery happens at boot, off the control loop).
bool read(void* user, uint8_t slot, uint8_t* buf, std::size_t* in_out_len);

}  // namespace modesp::scenario_persist
