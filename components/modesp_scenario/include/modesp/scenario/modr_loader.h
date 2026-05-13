/**
 * @file modr_loader.h
 * @brief `.modr` binary validator + accessor view.
 *
 * The loader takes a contiguous buffer (already read from LittleFS / OTA),
 * fully validates structural integrity (magic, version, CRC32, all internal
 * offsets/counts), resolves action/condition hashes against ActionRegistry,
 * and produces a read-only `LoadedScenario` view that engine code uses to
 * navigate phases, transitions, action params, etc.
 *
 * Validation philosophy:
 *   - Fail-fast з specific EngineError code (not just OK/NOK)
 *   - Bounds-check every offset BEFORE dereferencing
 *   - Validate composite condition tree integrity (no self-reference, no
 *     out-of-bounds child indices, depth-bounded)
 *   - Resolve LEAF condition hashes against registry; recognize
 *     all_of/any_of/not як composites (handled inline by engine, not
 *     registered as functions)
 *
 * Lifetime: caller owns the buffer; LoadedScenario holds raw pointers into
 * that buffer. Engine keeps both (buffer + LoadedScenario) у its
 * SequenceInstance. When unloaded, buffer freed and LoadedScenario discarded.
 *
 * Thread-safety: validation is read-only. Multiple concurrent validations OK.
 *
 * Reference: docs/sequence_engine/02_binary_format.md (validation rules table).
 */

#pragma once

#include "modesp/scenario/modr_format.h"
#include "modesp/scenario/engine_error.h"

#include <cstddef>
#include <cstdint>

namespace modesp::scenario {

class ActionRegistry;  // forward — only pointer/ref needed у header

// ── Composite-condition sentinel hashes ────────────────────────────────
//
// Композитні conditions (all_of/any_of/not) НЕ registered у ActionRegistry —
// engine handles їх inline через recursion над cond_pool entries. Loader
// recognizes ці hashes як valid (treated як composite ops, not functions),
// validates child indices are within cond_pool bounds + depth-limited.

/// djb2_hash16("all_of") — composite AND
constexpr uint16_t MODR_COND_HASH_ALL_OF = djb2_hash16("all_of");

/// djb2_hash16("any_of") — composite OR
constexpr uint16_t MODR_COND_HASH_ANY_OF = djb2_hash16("any_of");

/// djb2_hash16("not") — composite negation
constexpr uint16_t MODR_COND_HASH_NOT = djb2_hash16("not");

/// Returns true якщо hash matches composite sentinel (handled inline).
constexpr bool is_composite_cond(uint16_t hash) noexcept {
    return hash == MODR_COND_HASH_ALL_OF
        || hash == MODR_COND_HASH_ANY_OF
        || hash == MODR_COND_HASH_NOT;
}

// ── Engine-level constants (used by loader та consumers) ───────────────

/// Hard cap on tracks within one scenario (per plan Q2).
/// Loader rejects modr_header.track_count exceeding це.
constexpr uint8_t MAX_TRACKS_PER_SCENARIO = 6;

/// Maximum recursion depth у composite condition tree. Matches compile-time
/// guard у tools/compile_scenario.py (_MAX_CONDITION_DEPTH = 16).
/// Loader detects exceeding це через recursive validation з depth counter.
constexpr uint8_t MAX_CONDITION_DEPTH = 16;

// ── LoadedScenario: validated read-only view ──────────────────────────

/**
 * Read-only view into a validated `.modr` buffer. All accessor methods
 * return raw pointers to data WITHIN the buffer — caller MUST keep buffer
 * alive longer than LoadedScenario.
 *
 * Pre-condition: produced ONLY by `modr_validate()` returning EngineError::OK.
 * If validation failed, fields hold undefined / zero values; do not use.
 */
struct LoadedScenario {
    const uint8_t* buffer = nullptr;   ///< caller-owned; lifetime ≥ this struct
    size_t         buffer_size = 0;    ///< actual buffer length (≥ header.total_size)

    // ── Header accessor (always at offset 0) ──
    const modr_header* header() const noexcept {
        return reinterpret_cast<const modr_header*>(buffer);
    }

    // ── Track table ──
    const modr_track* tracks() const noexcept {
        return reinterpret_cast<const modr_track*>(buffer + header()->track_table_off);
    }

    // ── Phases for one track ──
    const modr_phase* phases(uint8_t track_idx) const noexcept {
        return reinterpret_cast<const modr_phase*>(buffer + tracks()[track_idx].phases_off);
    }

    // ── Transition array for a phase (transitions live inline at phase.transitions_off) ──
    const modr_transition* transitions(uint16_t off) const noexcept {
        return reinterpret_cast<const modr_transition*>(buffer + off);
    }

    // ── Action pool (entry/exit actions) ──
    const modr_action* action_pool() const noexcept {
        return reinterpret_cast<const modr_action*>(buffer + header()->action_pool_off);
    }

    // ── Condition pool (transitions reference it; same struct as action) ──
    const modr_action* cond_pool() const noexcept {
        return reinterpret_cast<const modr_action*>(buffer + header()->cond_pool_off);
    }

    // ── Param pool ──
    const modr_param_entry* param_pool() const noexcept {
        return reinterpret_cast<const modr_param_entry*>(buffer + header()->param_pool_off);
    }

    // ── Global transitions ──
    const modr_global_transition* global_transitions() const noexcept {
        return reinterpret_cast<const modr_global_transition*>(buffer + header()->global_trans_off);
    }

    // ── Resources (scenario-scope) ──
    const modr_resource_decl* resources() const noexcept {
        return reinterpret_cast<const modr_resource_decl*>(buffer + header()->resource_off);
    }

    // ── Phase-scope resource claims (per-phase array) ──
    const modr_phase_resource_claim* phase_resources(uint16_t off) const noexcept {
        return reinterpret_cast<const modr_phase_resource_claim*>(buffer + off);
    }

    // ── String pool: raw byte access ──
    const char* string_pool_data() const noexcept {
        return reinterpret_cast<const char*>(buffer + header()->string_pool_off);
    }

    /// String pool extent: from string_pool_off to total_size - 4 (CRC32 trailer).
    /// Computed at validation time; could be cached but cheap to recompute.
    uint16_t string_pool_size() const noexcept {
        return static_cast<uint16_t>(header()->total_size - 4u - header()->string_pool_off);
    }

    /// Copy length-prefixed string at `offset` into `buf`. Returns false якщо
    /// offset out of bounds, length-prefix exceeds pool, або buf too small.
    /// Output is NUL-terminated on success.
    bool read_string(uint16_t offset, char* buf, size_t buf_size) const noexcept;
};

// ── Loader entry point ────────────────────────────────────────────────

/**
 * Validates `.modr` blob і populates LoadedScenario view on success.
 *
 * @param buffer    Pointer to caller-owned buffer holding entire .modr file.
 * @param size      Buffer length у bytes. Must be ≥ header.total_size.
 * @param registry  ActionRegistry instance — used to resolve action AND
 *                  condition hashes during validation. Caller-owned;
 *                  reference must outlive this call (validation is
 *                  synchronous).
 * @param out       [out] Populated on EngineError::OK. Untouched on error.
 *
 * @return EngineError::OK on success, або specific error code:
 *  - INVALID_FILE         magic mismatch, truncated, total_size inconsistency
 *  - UNSUPPORTED_VERSION  format_version != MODR_FORMAT_VERSION
 *  - CRC_MISMATCH         trailer CRC32 mismatch (corruption або bit flip)
 *  - BUFFER_OVERFLOW      internal offset+count would read past total_size
 *  - TOO_MANY_TRACKS      track_count > MAX_TRACKS_PER_SCENARIO
 *  - INVALID_TRANSITION   bad transition kind, target_phase, або condition pairing
 *  - UNKNOWN_ACTION       action_hash у action_pool не registered
 *  - UNKNOWN_CONDITION    cond_hash у cond_pool не registered AND не composite
 *
 * Pre-conditions:
 *  - `registry` populated з all required actions/conditions (typically
 *    через `register_builtin_actions(registry)` + custom registrations
 *    before this call)
 *  - buffer aligned to at least 4 bytes (caller responsibility; affects
 *    direct struct cast performance)
 *
 * Side effects: none. Pure validation function.
 */
EngineError modr_validate(const uint8_t* buffer, size_t size,
                          const ActionRegistry& registry,
                          LoadedScenario& out);

/**
 * Compute CRC32 (ISO-HDLC, matches Python zlib.crc32 і ESP-IDF esp_crc32_le)
 * over arbitrary buffer. Public для test fixtures and golden file generation.
 */
uint32_t crc32_iso_hdlc(const uint8_t* data, size_t len);

}  // namespace modesp::scenario
