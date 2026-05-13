/**
 * @file engine_error.h
 * @brief EngineError enum — uniform error vocabulary for scenario Engine.
 *
 * Used by loader, instance, engine, NVS observer. Plain `enum class` — no
 * string conversion у core (host/HTTP code can stringify через separate
 * helper, engine stays minimal).
 *
 * Lift from `modesp_sequence/engine_error.h` (Phase 1, namespace rename only).
 */

#pragma once

#include <cstdint>

namespace modesp::scenario {

/// Uniform error code for all Engine operations. Returned through API methods
/// (`load`, `start`, `pause`, ...) і loader.
enum class EngineError : uint8_t {
    OK = 0,

    // ── Loader / file format errors ──
    INVALID_FILE,         ///< magic mismatch, truncated, total_size inconsistency
    UNSUPPORTED_VERSION,  ///< format_version != MODR_FORMAT_VERSION
    CRC_MISMATCH,         ///< trailer CRC32 не matches computed
    BUFFER_OVERFLOW,      ///< offset+count would exceed buffer_size
    UNKNOWN_ACTION,       ///< action_hash не у ActionRegistry
    UNKNOWN_CONDITION,    ///< cond_hash не у ActionRegistry conditions і не композит
    INVALID_TRANSITION,   ///< bad target_phase, bad kind, або inconsistent kind+cond_idx
    TOO_MANY_TRACKS,      ///< track_count > MAX_TRACKS_PER_SCENARIO
    NAME_TOO_LONG,        ///< recipe name або track name перевищує budget

    // ── Lifecycle / API errors ──
    NO_SLOT,              ///< all Engine slots зайняті
    NOT_LOADED,           ///< handle valid але scenario не loaded
    INVALID_HANDLE,       ///< handle з range або не allocated
    INVALID_TRACK,        ///< track_idx >= track_count

    // ── Parameter override errors ──
    PARAM_OUT_OF_RANGE,   ///< override value поза [min, max] (Stage 2)
    PARAM_NOT_OVERRIDABLE,///< override targets param без OVERRIDABLE flag

    // ── Resource arbitration ──
    RESOURCE_CONTENDED,   ///< scenario-scope resource не available

    // ── Runtime / persistence ──
    NVS_ERROR,            ///< persistence read/write failed
    ABORTED_BY_SAFETY,    ///< global transition fired
};

/// Returns true якщо `e == OK`. Convenience для error chains.
constexpr bool is_ok(EngineError e) noexcept {
    return e == EngineError::OK;
}

}  // namespace modesp::scenario
