/**
 * @file nvs_token.h
 * @brief Persistence token format для scenario state recovery.
 *
 * Per plan Q7: 96-byte fixed-size POD that captures enough state для
 * power-cycle recovery — scenario state, per-track phase position +
 * elapsed_ms, resource ownership mask, wall-clock anchor.
 *
 * Restore semantics: на recovery engine restores phase_idx + phase_elapsed_ms,
 * but enters PAUSED state (NOT auto-resume). User explicitly resume()/abort()
 * decides whether to continue interrupted scenario.
 *
 * NVS storage layout (caller's responsibility — observer wires NVS write/read
 * callbacks):
 *   namespace: typically "scnstate"
 *   key:       "t<slot>" (e.g. "t0", "t1", ...)
 *   value:     seq_token (96 bytes)
 *
 * **Phase 2 lift (magic bump):** Old engine used `'SQTK'` magic (sequence
 * token kit). New engine uses `'SCTK'` (scenario token kit). After firmware
 * upgrade, OLD `'SQTK'` tokens у NVS are silently rejected with INVALID_FILE
 * — scenario starts fresh. Documented behavior (plan A13).
 */

#pragma once

#include "modesp/scenario/runtime_types.h"
#include "modesp/scenario/engine_error.h"

#include <cstdint>
#include <cstddef>

namespace modesp::scenario {

/// Magic 'SCTK' як LE uint32. **Bumped from 'SQTK' у Phase 2 rebuild** —
/// forces clean reject of any tokens written by old modesp_sequence engine.
constexpr uint32_t SEQ_TOKEN_MAGIC = 0x4B544353;  // 'S'(0x53) 'C'(0x43) 'T'(0x54) 'K'(0x4B) LE

/// Schema version. Layout unchanged from old engine — only magic differs.
constexpr uint16_t SEQ_TOKEN_VERSION = 1;

/// Fixed token payload size.
constexpr size_t SEQ_TOKEN_SIZE = 96;

/**
 * Persistence token. POD layout, LE byte order.
 *
 * **Update protocol:** add fields ONLY at end; bump version коли semantics
 * change incompatibly.
 */
struct seq_token {                       // 96 bytes
    uint32_t magic;                      // [0..3]  = SEQ_TOKEN_MAGIC
    uint16_t version;                    // [4..5]  = SEQ_TOKEN_VERSION
    uint16_t scenario_id;                // [6..7]  djb2(module_name) low16
    uint8_t  scenario_state;             // [8]     SequenceRuntime::State enum
    uint8_t  track_count;                // [9]     1..MAX_TRACKS_PER_SCENARIO
    uint16_t resource_owner_mask;        // [10..11] reserved (Stage 1.5)
    uint32_t scenario_elapsed_ms;        // [12..15]
    uint32_t wall_clock_started_at;      // [16..19] unix epoch, 0 if no SNTP
    struct {                             // [20..67] 6 tracks × 8 bytes = 48
        uint8_t  state;
        uint8_t  phase_idx;
        uint16_t reserved;
        uint32_t phase_elapsed_ms;
    } tracks[6];
    uint8_t  cont_state[16];             // [68..83] reserved (Stage 2)
    uint32_t reserved_a;                 // [84..87]
    uint32_t reserved_b;                 // [88..91]
    uint16_t crc16;                      // [92..93] CRC-CCITT of [0..91]
    uint16_t reserved_c;                 // [94..95]
};
static_assert(sizeof(seq_token) == SEQ_TOKEN_SIZE, "seq_token must be 96 bytes");

// ── Persistence API ──────────────────────────────────────────────────

/**
 * Serialize current runtime state to a token buffer.
 *
 * @param sr            scenario runtime to capture
 * @param scenario_id   scenario_id from .modr header
 * @param wall_clock    optional wall-clock anchor (unix seconds), 0 if no SNTP
 * @param out_buf       buffer of at least SEQ_TOKEN_SIZE bytes
 * @return true on success, false якщо sr.scenario.header() unavailable.
 */
bool serialize_token(const SequenceRuntime& sr, uint16_t scenario_id,
                     uint32_t wall_clock, uint8_t* out_buf);

/**
 * Validate і deserialize token. On success populates `sr.tracks[*]` AND
 * `sr.scenario_elapsed_ms`. Caller is responsible for `sr.scenario` already
 * being valid (recipe must be reloaded BEFORE token recovery).
 *
 * Recovery sets `sr.state = PAUSED` per plan Q8.
 *
 * Validation:
 *   - Magic == SEQ_TOKEN_MAGIC ('SCTK')   — old 'SQTK' tokens rejected
 *   - Version == SEQ_TOKEN_VERSION
 *   - CRC16 matches
 *   - scenario_id matches .modr header
 *
 * @return EngineError::OK on success, або:
 *  - INVALID_FILE         magic mismatch / scenario_id mismatch
 *  - UNSUPPORTED_VERSION  schema version wrong
 *  - CRC_MISMATCH         token corrupted
 */
EngineError deserialize_token(const uint8_t* buf, size_t size,
                              SequenceRuntime& sr);

/**
 * CRC-CCITT (XMODEM variant): poly 0x1021, init 0x0000.
 */
uint16_t crc16_ccitt(const uint8_t* data, size_t len);

}  // namespace modesp::scenario
