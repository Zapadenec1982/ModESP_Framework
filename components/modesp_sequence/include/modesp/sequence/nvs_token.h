/**
 * @file nvs_token.h
 * @brief Persistence token format для scenario state recovery (Step 15).
 *
 * Per plan Q7: 96-byte fixed-size POD that captures enough state для
 * power-cycle recovery — scenario state, per-track phase position +
 * elapsed_ms, resource ownership mask, wall-clock anchor.
 *
 * Restore semantics: на recovery engine restores phase_idx + phase_elapsed_ms,
 * but enters PAUSED state (NOT auto-resume). User explicitly resume()/abort()
 * decides whether to continue interrupted scenario.
 *
 * NVS storage layout:
 *   namespace: "seqstate"
 *   key:       "t<slot>" (e.g. "t0", "t1", ..., "t<MAX_SEQUENCES-1>")
 *   value:     seq_token (96 bytes)
 *
 * This header defines pure POD + serialize/deserialize functions. Engine
 * integration (when to persist, NVS callback hook) belongs to Step 14/16.
 *
 * Reference: docs/sequence_engine/07_persistence.md.
 */

#pragma once

#include "modesp/sequence/sequence_state.h"
#include "modesp/sequence/engine_error.h"

#include <cstdint>
#include <cstddef>

namespace modesp::sequence {

/// Magic 'SQTK' як LE uint32. Distinguishes valid token from random NVS bytes.
constexpr uint32_t SEQ_TOKEN_MAGIC = 0x4B545153;  // 'S'(0x53) 'Q'(0x51) 'T'(0x54) 'K'(0x4B) LE

/// Schema version. Bump на breaking changes; loader rejects mismatches.
constexpr uint16_t SEQ_TOKEN_VERSION = 1;

/// Fixed token payload size — 96 bytes per plan Q7.
constexpr size_t SEQ_TOKEN_SIZE = 96;

/**
 * Persistence token. Layout matches plan Q7. POD (no v-tables, no padding
 * beyond що's explicitly declared). All multi-byte fields LE на ESP32 і
 * host для test compatibility.
 *
 * **Update protocol:** add fields ONLY at end (matching modr_format.h
 * convention); bump version коли semantics change incompatibly.
 */
struct seq_token {                       // 96 bytes
    uint32_t magic;                      // [0..3]  = SEQ_TOKEN_MAGIC
    uint16_t version;                    // [4..5]  = SEQ_TOKEN_VERSION
    uint16_t scenario_id;                // [6..7]  djb2(module_name) low16, must match .modr
    uint8_t  scenario_state;             // [8]     SequenceRuntime::State enum value
    uint8_t  track_count;                // [9]     1..MAX_TRACKS_PER_SCENARIO
    uint16_t resource_owner_mask;        // [10..11] reserved (Stage 1.5: bitmap which resources owned)
    uint32_t scenario_elapsed_ms;        // [12..15]
    uint32_t wall_clock_started_at;      // [16..19] unix epoch на start, 0 if no SNTP
    struct {                             // [20..67] 6 tracks × 8 bytes = 48
        uint8_t  state;
        uint8_t  phase_idx;
        uint16_t reserved;
        uint32_t phase_elapsed_ms;
    } tracks[6];
    uint8_t  cont_state[16];             // [68..83] reserved для continuous behaviors (Stage 2)
    uint32_t reserved_a;                 // [84..87]
    uint32_t reserved_b;                 // [88..91]
    uint16_t crc16;                      // [92..93] CRC-CCITT of [0..91]
    uint16_t reserved_c;                 // [94..95]
};
static_assert(sizeof(seq_token) == SEQ_TOKEN_SIZE, "seq_token must be 96 bytes");

// ── Persistence API (pure functions) ──────────────────────────────────

/**
 * Serialize current runtime state to a token buffer.
 *
 * @param sr            scenario runtime to capture
 * @param scenario_id   scenario_id from .modr header (used для NVS keying і
 *                      to verify token matches loaded recipe at recovery time)
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
 * Recovery sets `sr.state = PAUSED` per plan Q8 (manual resume required).
 *
 * Validation:
 *   - Magic == SEQ_TOKEN_MAGIC
 *   - Version == SEQ_TOKEN_VERSION
 *   - CRC16 matches
 *   - scenario_id matches .modr header (cross-check that token belongs to
 *     this recipe — protects against stale NVS після recipe replacement)
 *
 * @return EngineError::OK on success, або specific error code:
 *  - INVALID_FILE         magic mismatch / scenario_id mismatch
 *  - UNSUPPORTED_VERSION  schema version wrong
 *  - CRC_MISMATCH         token corrupted
 */
EngineError deserialize_token(const uint8_t* buf, size_t size,
                              SequenceRuntime& sr);

/**
 * CRC-CCITT (XMODEM variant): poly 0x1021, init 0x0000, no reflection,
 * final XOR 0x0000. Public для test fixtures і engine integration.
 */
uint16_t crc16_ccitt(const uint8_t* data, size_t len);

}  // namespace modesp::sequence
