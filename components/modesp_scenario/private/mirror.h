/**
 * @file mirror.h (private)
 * @brief Direct-call mirror publisher — writes per-instance state keys to backend.
 *
 * Replaces old `SequenceEngine::publish_mirror_keys` member function. Same
 * behavior, lifted з engine internals into а free function so engine code
 * stays focused on FSM driving.
 *
 * **Locked у plan A4:** mirror writes are NOT а separate observer. They run
 * every tick unconditionally, для every loaded slot. Funneling them through
 * an observer interface adds vtable dispatch without semantic gain — mirror
 * is fundamentally а projection of engine state, not an edge-triggered side
 * effect.
 *
 * Keys written per instance:
 *   <recipe>.scenario_state       (string — "idle"/"running"/"paused"/...)
 *   <recipe>.scenario_elapsed_s   (int — seconds since start)
 *   <recipe>.<track>_state        (string — track FSM state)
 *   <recipe>.<track>_phase_idx    (int)
 *   <recipe>.<track>_phase_name   (string — resolved з string pool)
 *   <recipe>.<track>_elapsed_s    (int)
 *
 * Recipe + track names are name-budgeted (≤ 12 + ≤ 8 chars) so the composed
 * key fits у the 32-char SharedState limit. Overflow is logged once per slot
 * (dedup flag passed by engine).
 */

#pragma once

#include "modesp/scenario/runtime_types.h"

#include <cstdint>

namespace modesp::scenario {

class IStateBackend;

namespace mirror {

/// Write all mirror keys для one loaded scenario instance. Idempotent — safe
/// to call every tick. Engine drives це from on_update().
///
/// @param backend       IStateBackend* (nullable; no-op якщо null)
/// @param sr            scenario runtime
/// @param name_warn_logged [in/out] dedup flag. Set to true when long-name
///                      truncation warning fires; never resets until slot
///                      reloaded.
void publish(IStateBackend* backend, const SequenceRuntime& sr,
             bool& name_warn_logged);

}  // namespace mirror

}  // namespace modesp::scenario
