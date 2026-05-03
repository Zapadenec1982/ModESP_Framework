/**
 * @file modr_loader.cpp
 * @brief Implementation of `.modr` validator + accessor.
 *
 * Validation algorithm:
 *   1. Header preamble (magic, version, total_size bounds)
 *   2. CRC32 trailer over [0..total_size-4]
 *   3. Top-level table bounds (track_count, pools)
 *   4. Per-track validation: phases array bounds + per-phase recursion
 *   5. Per-phase: action ranges, transition validity, phase-scope resources
 *   6. Action pool: hash resolution у ActionRegistry, param ranges
 *   7. Condition pool: leaf hash resolution OR composite (all_of/any_of/not)
 *   8. Composite tree: bounded recursion depth, in-bounds child indices
 *   9. Param pool: type validity, STR offset within string pool
 *  10. Global transitions: cond_pool_idx, kind, scope
 *  11. Resources: hash, exclusive flag, scope (must be SCENARIO)
 *
 * Bounds-check pattern: `if (off + count*sizeof(T) > limit) → BUFFER_OVERFLOW`
 * computed з overflow protection (off, count both u16; sizeof(T) ≤ 20; product
 * fits у uint32 без overflow until count > 200M which не achievable).
 *
 * Composite condition validation: each composite stores child indices у its
 * params (key_hash=0, type=i32, value=child cond_pool_idx). We follow these
 * indices recursively з depth counter capped at MAX_CONDITION_DEPTH (16).
 * Same depth bound enforced in compile_scenario.py — defense у both directions.
 */

#include "modesp/sequence/modr_loader.h"
#include "modesp/sequence/action_registry.h"

#include <cstring>

namespace modesp::sequence {

// ─────────────────────────────────────────────────────────────────────
// CRC32 (ISO-HDLC / zlib variant)
// ─────────────────────────────────────────────────────────────────────
//
// Reflected polynomial 0xEDB88320 (= bit-reverse of 0x04C11DB7).
// Init 0xFFFFFFFF, final XOR 0xFFFFFFFF. Matches Python zlib.crc32 byte-for-byte
// і ESP-IDF esp_crc32_le. Verified by golden file tests.
//
// Software-only (no PCLMULQDQ optimization). Loader runs once per recipe load
// (not hot path); throughput sufficient at <50 µs / 16 KB on ESP32 @ 240 MHz.

uint32_t crc32_iso_hdlc(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & -static_cast<int32_t>(crc & 1u));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

// ─────────────────────────────────────────────────────────────────────
// Helpers — bounds checking
// ─────────────────────────────────────────────────────────────────────

/// True якщо range [off, off+nbytes) лежить повністю всередині [0, limit).
/// Uses 32-bit math з explicit overflow check (off, nbytes both u16 → safe).
static bool in_bounds(uint32_t off, uint32_t nbytes, uint32_t limit) noexcept {
    if (nbytes == 0) return off <= limit;     // empty range: just need off ≤ limit
    uint32_t end = off + nbytes;
    return off <= limit && end <= limit && end >= off;  // last clause guards overflow
}

/// True якщо length-prefixed string at `offset` у string_pool fits within pool.
static bool string_fits(const uint8_t* pool, uint16_t pool_size, uint16_t offset) noexcept {
    if (offset >= pool_size) return false;
    uint8_t len = pool[offset];
    return offset + 1u + len <= pool_size;
}

// ─────────────────────────────────────────────────────────────────────
// LoadedScenario::read_string
// ─────────────────────────────────────────────────────────────────────

bool LoadedScenario::read_string(uint16_t offset, char* buf, size_t buf_size) const noexcept {
    if (buf == nullptr || buf_size == 0) return false;
    const uint16_t pool_sz = string_pool_size();
    const auto* pool = reinterpret_cast<const uint8_t*>(string_pool_data());
    if (offset >= pool_sz) return false;
    uint8_t len = pool[offset];
    if (offset + 1u + len > pool_sz) return false;
    if (static_cast<size_t>(len) + 1 > buf_size) return false;
    std::memcpy(buf, pool + offset + 1, len);
    buf[len] = '\0';
    return true;
}

// ─────────────────────────────────────────────────────────────────────
// Validation: top-level entry point
// ─────────────────────────────────────────────────────────────────────

namespace {  // internal validation helpers (file-scope, no symbol leakage)

struct ValidatorCtx {
    const uint8_t* buf;
    uint32_t       limit;     // = header.total_size - 4 (everything before CRC trailer)
    const modr_header* hdr;
};

EngineError validate_header_preamble(const uint8_t* buf, size_t size,
                                     const modr_header*& out_hdr) {
    if (buf == nullptr) return EngineError::INVALID_FILE;
    if (size < sizeof(modr_header) + 4u) return EngineError::INVALID_FILE;

    const auto* hdr = reinterpret_cast<const modr_header*>(buf);

    if (hdr->magic != MODR_MAGIC) return EngineError::INVALID_FILE;
    if (hdr->format_version != MODR_FORMAT_VERSION) return EngineError::UNSUPPORTED_VERSION;

    // total_size must include header + CRC trailer at minimum, AND match буфер size
    if (hdr->total_size < sizeof(modr_header) + 4u) return EngineError::INVALID_FILE;
    if (hdr->total_size > size) return EngineError::INVALID_FILE;
    // Defense-in-depth: caller (load_buffer) limits інput buffer to MODR_MAX_SIZE,
    // але public modr_validate API can be called із arbitrary buffer (tests, future
    // integration). Reject claimed size that exceeds the format's hard cap so
    // downstream uint16 truncations (e.g. string_pool_size cast у LoadedScenario)
    // never happen.
    if (hdr->total_size > MODR_MAX_SIZE) return EngineError::INVALID_FILE;

    out_hdr = hdr;
    return EngineError::OK;
}

EngineError validate_crc(const uint8_t* buf, uint32_t total_size) {
    uint32_t computed = crc32_iso_hdlc(buf, total_size - 4u);
    uint32_t stored;
    std::memcpy(&stored, buf + total_size - 4u, sizeof(stored));
    return (computed == stored) ? EngineError::OK : EngineError::CRC_MISMATCH;
}

EngineError validate_action_pool(const ValidatorCtx& ctx) {
    const modr_header* h = ctx.hdr;
    uint32_t pool_bytes = uint32_t{h->action_pool_count} * sizeof(modr_action);
    if (!in_bounds(h->action_pool_off, pool_bytes, ctx.limit)) {
        return EngineError::BUFFER_OVERFLOW;
    }
    auto* actions = reinterpret_cast<const modr_action*>(ctx.buf + h->action_pool_off);
    auto& reg = ActionRegistry::instance();
    for (uint16_t i = 0; i < h->action_pool_count; ++i) {
        const auto& a = actions[i];
        if (reg.find_action(a.action_hash) == nullptr) {
            return EngineError::UNKNOWN_ACTION;
        }
        // param range bounds: param_idx + param_n must lie within param_pool
        if (a.param_n > 0) {
            if (a.param_idx == MODR_NO_OFFSET) {
                // sentinel, but param_n > 0 is contradictory
                return EngineError::INVALID_FILE;
            }
            uint32_t end = uint32_t{a.param_idx} + a.param_n;
            if (a.param_idx >= h->param_pool_count || end > h->param_pool_count) {
                return EngineError::BUFFER_OVERFLOW;
            }
        }
    }
    return EngineError::OK;
}

EngineError validate_cond_at(const ValidatorCtx& ctx, uint16_t cond_idx, uint8_t depth);

EngineError validate_composite(const ValidatorCtx& ctx, const modr_action& cond,
                               uint8_t depth) {
    const modr_header* h = ctx.hdr;
    if (depth >= MAX_CONDITION_DEPTH) {
        return EngineError::INVALID_FILE;  // nested too deep
    }
    if (cond.param_n == 0) {
        // composite з no children — meaningless, treat як malformed
        return EngineError::INVALID_FILE;
    }
    // "not" must have exactly one child
    if (cond.action_hash == MODR_COND_HASH_NOT && cond.param_n != 1) {
        return EngineError::INVALID_FILE;
    }
    if (cond.param_idx == MODR_NO_OFFSET) return EngineError::INVALID_FILE;
    uint32_t end = uint32_t{cond.param_idx} + cond.param_n;
    if (cond.param_idx >= h->param_pool_count || end > h->param_pool_count) {
        return EngineError::BUFFER_OVERFLOW;
    }
    auto* params = reinterpret_cast<const modr_param_entry*>(
        ctx.buf + h->param_pool_off);
    for (uint8_t i = 0; i < cond.param_n; ++i) {
        const auto& p = params[cond.param_idx + i];
        // composite child slots use type=i32, key_hash=0, value=child_idx
        if (p.type != MODR_PARAM_TYPE_I32) return EngineError::INVALID_FILE;
        int32_t child_idx_signed;
        std::memcpy(&child_idx_signed, &p.value, sizeof(int32_t));
        if (child_idx_signed < 0 || child_idx_signed >= h->cond_pool_count) {
            return EngineError::BUFFER_OVERFLOW;
        }
        EngineError err = validate_cond_at(ctx, static_cast<uint16_t>(child_idx_signed),
                                            static_cast<uint8_t>(depth + 1));
        if (err != EngineError::OK) return err;
    }
    return EngineError::OK;
}

EngineError validate_cond_at(const ValidatorCtx& ctx, uint16_t cond_idx, uint8_t depth) {
    const modr_header* h = ctx.hdr;
    if (cond_idx >= h->cond_pool_count) return EngineError::BUFFER_OVERFLOW;
    auto* conds = reinterpret_cast<const modr_action*>(ctx.buf + h->cond_pool_off);
    const auto& cond = conds[cond_idx];
    if (is_composite_cond(cond.action_hash)) {
        return validate_composite(ctx, cond, depth);
    }
    // Leaf: must be registered у ActionRegistry conditions
    auto& reg = ActionRegistry::instance();
    if (reg.find_condition(cond.action_hash) == nullptr) {
        return EngineError::UNKNOWN_CONDITION;
    }
    if (cond.param_n > 0) {
        if (cond.param_idx == MODR_NO_OFFSET) return EngineError::INVALID_FILE;
        uint32_t end = uint32_t{cond.param_idx} + cond.param_n;
        if (cond.param_idx >= h->param_pool_count || end > h->param_pool_count) {
            return EngineError::BUFFER_OVERFLOW;
        }
    }
    return EngineError::OK;
}

EngineError validate_cond_pool(const ValidatorCtx& ctx) {
    const modr_header* h = ctx.hdr;
    uint32_t pool_bytes = uint32_t{h->cond_pool_count} * sizeof(modr_action);
    if (!in_bounds(h->cond_pool_off, pool_bytes, ctx.limit)) {
        return EngineError::BUFFER_OVERFLOW;
    }
    // Validate every cond entry. Composites recurse via validate_cond_at.
    // Each entry independently bounded, so traversal terminates у all cases.
    for (uint16_t i = 0; i < h->cond_pool_count; ++i) {
        EngineError err = validate_cond_at(ctx, i, /*depth=*/0);
        if (err != EngineError::OK) return err;
    }
    return EngineError::OK;
}

EngineError validate_param_pool(const ValidatorCtx& ctx) {
    const modr_header* h = ctx.hdr;
    uint32_t pool_bytes = uint32_t{h->param_pool_count} * sizeof(modr_param_entry);
    if (!in_bounds(h->param_pool_off, pool_bytes, ctx.limit)) {
        return EngineError::BUFFER_OVERFLOW;
    }
    auto* params = reinterpret_cast<const modr_param_entry*>(
        ctx.buf + h->param_pool_off);

    // String pool extent: from string_pool_off to limit (= total_size - 4).
    if (h->string_pool_off > ctx.limit) return EngineError::BUFFER_OVERFLOW;
    uint16_t string_pool_sz = static_cast<uint16_t>(ctx.limit - h->string_pool_off);
    auto* string_pool = ctx.buf + h->string_pool_off;

    for (uint16_t i = 0; i < h->param_pool_count; ++i) {
        const auto& p = params[i];
        if (p.type > MODR_PARAM_TYPE_STR) return EngineError::INVALID_FILE;
        if (p.type == MODR_PARAM_TYPE_STR) {
            // value field holds u16 string pool offset (low 16 bits)
            uint16_t s_off = static_cast<uint16_t>(p.value & 0xFFFFu);
            if (!string_fits(string_pool, string_pool_sz, s_off)) {
                return EngineError::BUFFER_OVERFLOW;
            }
        }
    }
    return EngineError::OK;
}

EngineError validate_transition(const ValidatorCtx& ctx, const modr_transition& t,
                                uint8_t phase_count) {
    const modr_header* h = ctx.hdr;
    // Kind validity
    if (t.kind > MODR_TRANS_KIND_UNCONDITIONAL) return EngineError::INVALID_TRANSITION;
    // target_phase: must be valid index OR sentinel
    if (t.target_phase != MODR_TARGET_COMPLETE && t.target_phase != MODR_TARGET_ABORT) {
        if (t.target_phase >= phase_count) return EngineError::INVALID_TRANSITION;
    }
    // Kind/cond_pool_idx pairing
    bool has_cond_kind = (t.kind == MODR_TRANS_KIND_COND
                       || t.kind == MODR_TRANS_KIND_TIME_OR_COND
                       || t.kind == MODR_TRANS_KIND_TIME_AND_COND);
    if (has_cond_kind) {
        if (t.cond_pool_idx == MODR_NO_OFFSET) {
            return EngineError::INVALID_TRANSITION;  // condition kind вимагає valid idx
        }
        if (t.cond_pool_idx >= h->cond_pool_count) {
            return EngineError::INVALID_TRANSITION;
        }
    }
    // Unconditional/time kinds: cond_pool_idx ignored, but cleanest якщо MODR_NO_OFFSET
    // Don't reject other values though — compiler controls це, runtime tolerant
    return EngineError::OK;
}

EngineError validate_phase(const ValidatorCtx& ctx, const modr_phase& ph,
                           uint8_t phase_count) {
    const modr_header* h = ctx.hdr;
    // Entry actions
    if (ph.entry_action_n > 0) {
        if (ph.entry_action_off == MODR_NO_OFFSET) return EngineError::INVALID_FILE;
        uint32_t end = uint32_t{ph.entry_action_off} + ph.entry_action_n;
        if (ph.entry_action_off >= h->action_pool_count || end > h->action_pool_count) {
            return EngineError::BUFFER_OVERFLOW;
        }
    }
    // Exit actions
    if (ph.exit_action_n > 0) {
        if (ph.exit_action_off == MODR_NO_OFFSET) return EngineError::INVALID_FILE;
        uint32_t end = uint32_t{ph.exit_action_off} + ph.exit_action_n;
        if (ph.exit_action_off >= h->action_pool_count || end > h->action_pool_count) {
            return EngineError::BUFFER_OVERFLOW;
        }
    }
    // Transitions
    if (ph.transition_n > 0) {
        uint32_t bytes = uint32_t{ph.transition_n} * sizeof(modr_transition);
        if (!in_bounds(ph.transitions_off, bytes, ctx.limit)) {
            return EngineError::BUFFER_OVERFLOW;
        }
        auto* trans = reinterpret_cast<const modr_transition*>(ctx.buf + ph.transitions_off);
        for (uint8_t i = 0; i < ph.transition_n; ++i) {
            EngineError err = validate_transition(ctx, trans[i], phase_count);
            if (err != EngineError::OK) return err;
        }
    }
    // Phase-scope resources
    if (ph.phase_resource_n > 0) {
        uint32_t bytes = uint32_t{ph.phase_resource_n} * sizeof(modr_phase_resource_claim);
        if (!in_bounds(ph.phase_resources_off, bytes, ctx.limit)) {
            return EngineError::BUFFER_OVERFLOW;
        }
        auto* claims = reinterpret_cast<const modr_phase_resource_claim*>(
            ctx.buf + ph.phase_resources_off);
        for (uint8_t i = 0; i < ph.phase_resource_n; ++i) {
            if (claims[i].exclusive > 1) return EngineError::INVALID_FILE;
        }
    }
    return EngineError::OK;
}

EngineError validate_track(const ValidatorCtx& ctx, const modr_track& tr) {
    if (tr.phase_count == 0) return EngineError::INVALID_FILE;
    if (tr.initial_phase >= tr.phase_count) return EngineError::INVALID_FILE;
    uint32_t phases_bytes = uint32_t{tr.phase_count} * sizeof(modr_phase);
    if (!in_bounds(tr.phases_off, phases_bytes, ctx.limit)) {
        return EngineError::BUFFER_OVERFLOW;
    }
    auto* phases = reinterpret_cast<const modr_phase*>(ctx.buf + tr.phases_off);
    for (uint8_t i = 0; i < tr.phase_count; ++i) {
        EngineError err = validate_phase(ctx, phases[i], tr.phase_count);
        if (err != EngineError::OK) return err;
    }
    return EngineError::OK;
}

EngineError validate_global_transitions(const ValidatorCtx& ctx) {
    const modr_header* h = ctx.hdr;
    if (h->global_trans_count == 0) return EngineError::OK;
    uint32_t bytes = uint32_t{h->global_trans_count} * sizeof(modr_global_transition);
    if (!in_bounds(h->global_trans_off, bytes, ctx.limit)) {
        return EngineError::BUFFER_OVERFLOW;
    }
    auto* gts = reinterpret_cast<const modr_global_transition*>(
        ctx.buf + h->global_trans_off);
    for (uint8_t i = 0; i < h->global_trans_count; ++i) {
        const auto& g = gts[i];
        if (g.kind > MODR_TRANS_KIND_UNCONDITIONAL) return EngineError::INVALID_TRANSITION;
        if (g.scope > MODR_GLOBAL_SCOPE_MAIN_TRACK) return EngineError::INVALID_FILE;
        // KIND_COND requires valid cond_pool_idx
        if (g.kind == MODR_TRANS_KIND_COND
         || g.kind == MODR_TRANS_KIND_TIME_OR_COND
         || g.kind == MODR_TRANS_KIND_TIME_AND_COND) {
            if (g.cond_pool_idx >= h->cond_pool_count) {
                return EngineError::INVALID_TRANSITION;
            }
        }
    }
    return EngineError::OK;
}

EngineError validate_resources(const ValidatorCtx& ctx) {
    const modr_header* h = ctx.hdr;
    if (h->resource_count == 0) return EngineError::OK;
    uint32_t bytes = uint32_t{h->resource_count} * sizeof(modr_resource_decl);
    if (!in_bounds(h->resource_off, bytes, ctx.limit)) {
        return EngineError::BUFFER_OVERFLOW;
    }
    auto* res = reinterpret_cast<const modr_resource_decl*>(ctx.buf + h->resource_off);
    for (uint8_t i = 0; i < h->resource_count; ++i) {
        if (res[i].exclusive > 1) return EngineError::INVALID_FILE;
        // Scenario-scope resources only — phase-scope claims live в modr_phase_resource_claim
        if (res[i].scope != MODR_RESOURCE_SCOPE_SCENARIO) return EngineError::INVALID_FILE;
    }
    return EngineError::OK;
}

EngineError validate_recipe_name(const ValidatorCtx& ctx) {
    const modr_header* h = ctx.hdr;
    if (h->string_pool_off > ctx.limit) return EngineError::BUFFER_OVERFLOW;
    uint16_t pool_sz = static_cast<uint16_t>(ctx.limit - h->string_pool_off);
    auto* pool = ctx.buf + h->string_pool_off;
    if (!string_fits(pool, pool_sz, h->name_str_idx)) return EngineError::BUFFER_OVERFLOW;
    return EngineError::OK;
}

}  // anonymous namespace

// ─────────────────────────────────────────────────────────────────────
// Public entry point
// ─────────────────────────────────────────────────────────────────────

EngineError modr_validate(const uint8_t* buffer, size_t size, LoadedScenario& out) {
    // 1. Header preamble
    const modr_header* hdr = nullptr;
    EngineError err = validate_header_preamble(buffer, size, hdr);
    if (err != EngineError::OK) return err;

    // 2. CRC32 over [0..total_size-4]
    err = validate_crc(buffer, hdr->total_size);
    if (err != EngineError::OK) return err;

    // 3. Track count limit
    if (hdr->track_count == 0) return EngineError::INVALID_FILE;
    if (hdr->track_count > MAX_TRACKS_PER_SCENARIO) return EngineError::TOO_MANY_TRACKS;

    // Build validator context. limit = everything before CRC trailer.
    ValidatorCtx ctx{buffer, hdr->total_size - 4u, hdr};

    // 4. Track table extent
    uint32_t tt_bytes = uint32_t{hdr->track_count} * sizeof(modr_track);
    if (!in_bounds(hdr->track_table_off, tt_bytes, ctx.limit)) {
        return EngineError::BUFFER_OVERFLOW;
    }

    // 5. Pools (action / cond / param) — validated separately. Phases and
    //    transitions reference indices that must already be valid; це
    //    validation order matters (do pools first, then per-phase).
    err = validate_action_pool(ctx);
    if (err != EngineError::OK) return err;
    err = validate_param_pool(ctx);
    if (err != EngineError::OK) return err;
    err = validate_cond_pool(ctx);
    if (err != EngineError::OK) return err;

    // 6. Per-track validation (phases + transitions internal references)
    auto* tracks = reinterpret_cast<const modr_track*>(buffer + hdr->track_table_off);
    for (uint8_t i = 0; i < hdr->track_count; ++i) {
        err = validate_track(ctx, tracks[i]);
        if (err != EngineError::OK) return err;
    }

    // 7. Global transitions
    err = validate_global_transitions(ctx);
    if (err != EngineError::OK) return err;

    // 8. Resources
    err = validate_resources(ctx);
    if (err != EngineError::OK) return err;

    // 9. Recipe name string fits у pool
    err = validate_recipe_name(ctx);
    if (err != EngineError::OK) return err;

    // All good — populate output view
    out.buffer = buffer;
    out.buffer_size = size;
    return EngineError::OK;
}

}  // namespace modesp::sequence
