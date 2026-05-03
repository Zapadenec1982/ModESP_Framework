/**
 * @file nvs_token.cpp
 * @brief Implementation of persistence token serialization (Step 15).
 *
 * Pure functions; engine integration (when to call, NVS callback wiring)
 * lives у sequence_engine.cpp і main.cpp glue.
 *
 * CRC-CCITT XMODEM variant (poly 0x1021, init 0x0000) chosen для small
 * payload — 96-byte tokens; CRC32 overkill for це size + faster CRC16
 * matters при frequent throttled writes.
 */

#include "modesp/sequence/nvs_token.h"
#include "modesp/sequence/modr_loader.h"

#include <cstring>

namespace modesp::sequence {

uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0x0000;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                  : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

bool serialize_token(const SequenceRuntime& sr, uint16_t scenario_id,
                     uint32_t wall_clock, uint8_t* out_buf) {
    if (out_buf == nullptr) return false;
    if (sr.scenario.buffer == nullptr) return false;

    seq_token tok{};
    tok.magic = SEQ_TOKEN_MAGIC;
    tok.version = SEQ_TOKEN_VERSION;
    tok.scenario_id = scenario_id;
    tok.scenario_state = static_cast<uint8_t>(sr.state);
    tok.track_count = sr.scenario.header()->track_count;
    tok.resource_owner_mask = 0;  // Stage 1.5
    tok.scenario_elapsed_ms = sr.scenario_elapsed_ms;
    tok.wall_clock_started_at = wall_clock;

    for (uint8_t i = 0; i < tok.track_count && i < 6; ++i) {
        tok.tracks[i].state = static_cast<uint8_t>(sr.tracks[i].state);
        tok.tracks[i].phase_idx = sr.tracks[i].phase_idx;
        tok.tracks[i].reserved = 0;
        tok.tracks[i].phase_elapsed_ms = sr.tracks[i].phase_elapsed_ms;
    }
    // cont_state already zeroed via {}-init
    // CRC computed over [0..91]; crc16 field at [92..93] excluded
    tok.crc16 = 0;
    tok.reserved_c = 0;

    // Compute і store CRC.  The token struct is layout-stable так що we can
    // serialize via memcpy і crc the bytes directly.
    std::memcpy(out_buf, &tok, SEQ_TOKEN_SIZE);
    uint16_t crc = crc16_ccitt(out_buf, 92);
    std::memcpy(out_buf + 92, &crc, sizeof(crc));
    return true;
}

EngineError deserialize_token(const uint8_t* buf, size_t size,
                              SequenceRuntime& sr) {
    if (buf == nullptr || size != SEQ_TOKEN_SIZE) {
        return EngineError::INVALID_FILE;
    }
    seq_token tok;
    std::memcpy(&tok, buf, SEQ_TOKEN_SIZE);

    if (tok.magic != SEQ_TOKEN_MAGIC) return EngineError::INVALID_FILE;
    if (tok.version != SEQ_TOKEN_VERSION) return EngineError::UNSUPPORTED_VERSION;

    // CRC check — recompute over [0..91], compare з stored field
    uint16_t computed = crc16_ccitt(buf, 92);
    uint16_t stored;
    std::memcpy(&stored, buf + 92, sizeof(stored));
    if (computed != stored) return EngineError::CRC_MISMATCH;

    // Cross-check token's scenario_id matches loaded .modr header.
    // Protects against stale token із a previous recipe.
    if (sr.scenario.buffer == nullptr) return EngineError::NOT_LOADED;
    if (sr.scenario.header()->scenario_id != tok.scenario_id) {
        return EngineError::INVALID_FILE;
    }
    if (tok.track_count != sr.scenario.header()->track_count) {
        return EngineError::INVALID_FILE;
    }

    // Apply
    sr.scenario_elapsed_ms = tok.scenario_elapsed_ms;
    for (uint8_t i = 0; i < tok.track_count && i < 6; ++i) {
        sr.tracks[i].state = static_cast<TrackRuntime::State>(tok.tracks[i].state);
        sr.tracks[i].phase_idx = tok.tracks[i].phase_idx;
        sr.tracks[i].phase_elapsed_ms = tok.tracks[i].phase_elapsed_ms;
        sr.tracks[i].entry_action_progress = 0;  // re-run entries when resumed
        sr.tracks[i].exit_action_progress = 0;
        sr.tracks[i].action_pending_ticks = 0;
        sr.tracks[i].running_exit_actions = false;
    }
    // Recovery enters PAUSED per plan Q7 — manual resume required
    sr.state = SequenceRuntime::State::PAUSED;
    return EngineError::OK;
}

}  // namespace modesp::sequence
