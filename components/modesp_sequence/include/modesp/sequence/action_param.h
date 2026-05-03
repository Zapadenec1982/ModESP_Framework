/**
 * @file action_param.h
 * @brief POD types для action і condition parameters.
 *
 * Used by ActionRegistry, ContinuousRegistry і engine execution code.
 * Pure POD design — no ETL containers crossing FFI boundary, no heap.
 * Caller-friendly з будь-якого C++ модуля без modesp_sequence header
 * dependency cascade.
 *
 * Specification: docs/sequence_engine/03_api_reference.md (Q3 у plan).
 * Wire format: matches modr_param_entry у modr_format.h byte-for-byte.
 */

#pragma once

#include <cstdint>

namespace modesp {
class SharedState;  // forward — full include not required для pointer
}

namespace modesp::sequence {

/// Param type codes (must match MODR_PARAM_TYPE_* у modr_format.h).
enum class ParamType : uint8_t {
    I32  = 0,
    F32  = 1,
    BOOL = 2,
    STR  = 3,    ///< value field stores u16 string pool offset (low 16 bits)
};

/// One parameter passed to action/condition. POD, exactly 8 bytes.
/// Mirrors `modr_param_entry` byte-for-byte у wire format.
struct ActionParam {
    uint16_t key_hash;    ///< djb2_hash16 of param name (e.g., djb2("msg"))
    uint8_t  type;        ///< ParamType enum value
    uint8_t  flags;       ///< per-param flags (currently MODR_PARAM_FLAG_OVERRIDABLE)
    union {
        int32_t  i;
        float    f;
        bool     b;
        uint16_t s_idx;   ///< string pool offset (only valid коли type == STR)
    } v;
};
static_assert(sizeof(ActionParam) == 8, "ActionParam wire size matches modr_param_entry");

/// Status returned by action функцією. Engine uses це для transition decisions.
/// Per Q12 (action failure policy machine у plan).
enum class ActionStatus : uint8_t {
    OK                  = 0,  ///< action complete, engine advances
    PENDING             = 1,  ///< re-call next tick (long-running ops)
    FAILED_RECOVERABLE  = 2,  ///< log + skip remaining у phase, continue з transitions
    FAILED_ABORT        = 3,  ///< track -> TRACK_FAILED
};

/// Type alias for sequence handle (1..MAX_SEQUENCES, 0 = invalid).
using SequenceHandle = uint8_t;

/// Type alias for track index within scenario.
using TrackIdx = uint8_t;

/// Context passed to each action/condition виклик. POD pointers only —
/// caller's responsibility to keep underlying data valid for call duration.
/// Engine populates на кожен call; action reads, optionally writes via state ptr.
struct ActionContext {
    modesp::SharedState* state;             ///< pointer to global SharedState
    const ActionParam*   params;            ///< array of param_count entries
    uint8_t              param_count;       ///< number of params (0..255)
    uint8_t              _pad0;
    uint16_t             string_pool_size;  ///< extent of string_pool buffer
    const char*          string_pool;       ///< pool used for STR-type param resolution
    uint32_t             scenario_elapsed_ms; ///< ms since scenario.start()
    uint32_t             phase_elapsed_ms;  ///< ms since current phase entered
    uint8_t              phase_idx;         ///< current phase у track (0..phase_count-1)
    SequenceHandle       handle;            ///< scenario instance handle
    TrackIdx             track;             ///< current track index
    uint8_t              _pad1;
    const char*          recipe_name;       ///< for diagnostics + error messages
    const char*          track_name;        ///< for diagnostics + error messages
};

}  // namespace modesp::sequence
