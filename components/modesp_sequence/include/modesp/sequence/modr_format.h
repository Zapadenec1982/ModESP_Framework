/**
 * @file modr_format.h
 * @brief Binary `.modr` recipe file format definitions.
 *
 * `.modr` — compiled scenario binary loaded by SequenceEngine at runtime.
 * Single-file, little-endian, 4-byte aligned. Whole file завантажується
 * одним f_read у фіксований буфер (MODR_MAX_SIZE = 16 KB).
 *
 * Authoring → JSON manifest section (`scenario`) у modules/<recipe>/manifest.json
 * Build-time → tools/compile_scenario.py emits .modr into data/scenarios/
 * Runtime    → SequenceEngine::load() reads .modr from /lfs/scenarios/
 *
 * **Specification reference:** docs/sequence_engine/02_binary_format.md
 * **ADR rationale:** docs/sequence_engine/adr/0001-binary-format-not-constexpr.md
 *
 * **Alignment guarantee:** all structs have natural alignment (no packed
 * attribute needed). Layouts reorganized у Step 1 from plan Q1 spec to
 * achieve this. See "Layout corrections" comment block у tests/fixtures.
 *
 * **Byte layout invariants** (validated by tools/tests/test_modr_format.py):
 *   modr_header              = 56 bytes
 *   modr_track               = 16 bytes
 *   modr_phase               = 20 bytes
 *   modr_transition          = 12 bytes
 *   modr_global_transition   =  8 bytes
 *   modr_action              =  8 bytes (padded from 6 for alignment)
 *   modr_param_entry         =  8 bytes
 *   modr_resource_decl       =  4 bytes
 *   modr_phase_resource_claim=  4 bytes
 *
 * Total file = header + track table + phase tables + pools + string pool + CRC32 trailer.
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace modesp::sequence {

// ── Magic + version constants ──

/// Magic 'MODR' як LE uint32. Verifies we're reading correct file type.
constexpr uint32_t MODR_MAGIC = 0x52444F4D;  // 'M'(0x4D) 'O'(0x4F) 'D'(0x44) 'R'(0x52) LE

/// Current binary format version. Bumped on breaking schema changes.
/// Loader rejects files with mismatched format_version.
constexpr uint16_t MODR_FORMAT_VERSION = 1;

/// Maximum total `.modr` file size. Buffer pre-allocated у engine.
/// 16 KB accommodates realistic recipes (greenhouse pilot ~2 KB; multicooker ~5 KB).
constexpr size_t MODR_MAX_SIZE = 16 * 1024;

// ── Header flags (modr_header.flags bitfield) ──

constexpr uint16_t MODR_FLAG_CONTINUOUS_USED = 1u << 0;
constexpr uint16_t MODR_FLAG_HAS_GLOBALS     = 1u << 1;
constexpr uint16_t MODR_FLAG_HAS_RESOURCES   = 1u << 2;

// ── Completion rule values (modr_header.completion_rule) ──

constexpr uint8_t MODR_COMPLETION_ALL_TRACKS  = 0;
constexpr uint8_t MODR_COMPLETION_ANY_TRACK   = 1;
constexpr uint8_t MODR_COMPLETION_MAIN_TRACK  = 2;

// ── Track flags (modr_track.flags bitfield) ──

constexpr uint8_t MODR_TRACK_FLAG_MAIN          = 1u << 0;
constexpr uint8_t MODR_TRACK_FLAG_LOOP_ON_DONE  = 1u << 1;

// ── Transition kind (modr_transition.kind / modr_global_transition.kind) ──

constexpr uint8_t MODR_TRANS_KIND_TIME           = 0;
constexpr uint8_t MODR_TRANS_KIND_COND           = 1;
constexpr uint8_t MODR_TRANS_KIND_TIME_OR_COND   = 2;
constexpr uint8_t MODR_TRANS_KIND_TIME_AND_COND  = 3;

// ── Special transition target_phase values ──

constexpr uint16_t MODR_TARGET_COMPLETE = 0xFFFF;  // track reaches $complete
constexpr uint16_t MODR_TARGET_ABORT    = 0xFFFE;  // track triggers scenario abort

// ── Global transition scope (modr_global_transition.scope) ──

constexpr uint8_t MODR_GLOBAL_SCOPE_SCENARIO   = 0;  // abort whole scenario
constexpr uint8_t MODR_GLOBAL_SCOPE_MAIN_TRACK = 1;  // abort only main track

// ── Param type (modr_param_entry.type) ──

constexpr uint8_t MODR_PARAM_TYPE_I32   = 0;
constexpr uint8_t MODR_PARAM_TYPE_F32   = 1;
constexpr uint8_t MODR_PARAM_TYPE_BOOL  = 2;
constexpr uint8_t MODR_PARAM_TYPE_STR   = 3;  // value is u16 string pool index

// ── Param flags (modr_param_entry.flags bitfield) ──

constexpr uint8_t MODR_PARAM_FLAG_OVERRIDABLE = 1u << 0;

// ── Action flags (modr_action.flags bitfield) ──

constexpr uint8_t MODR_ACTION_FLAG_FAIL_ABORTS    = 1u << 0;
constexpr uint8_t MODR_ACTION_FLAG_FAIL_LOGS_ONLY = 1u << 1;

// ── Resource scope (modr_resource_decl.scope) ──

constexpr uint8_t MODR_RESOURCE_SCOPE_SCENARIO = 0;  // claim at start, release at end
constexpr uint8_t MODR_RESOURCE_SCOPE_PHASE    = 1;  // claim at phase entry; phase-level
                                                     // claim arrays referenced via
                                                     // modr_phase.phase_resources_off

// ── Sentinel offsets ──

constexpr uint16_t MODR_NO_OFFSET = 0xFFFF;  // "no entry" marker (used by entry/exit_action_off)

// ─────────────────────────────────────────────────────────────────────
// Binary structs (PODs, naturally aligned, no padding needed)
// ─────────────────────────────────────────────────────────────────────

/**
 * File header — 56 bytes at offset 0.
 *
 * Trailing CRC32 is at total_size - 4. CRC computed over [start..total_size-4]
 * using CRC-32/ISO-HDLC (matches Python zlib.crc32 і ESP-IDF esp_crc32_le).
 */
struct modr_header {                  // 56 bytes
    uint32_t magic;                   // [0..3]   = MODR_MAGIC
    uint16_t format_version;          // [4..5]   = MODR_FORMAT_VERSION
    uint16_t flags;                   // [6..7]   bitfield: MODR_FLAG_*
    uint32_t total_size;              // [8..11]  full file size incl. CRC32 trailer
    uint16_t scenario_id;             // [12..13] djb2(module_name) low16, used для NVS keying
    uint16_t name_str_idx;            // [14..15] index into string pool — recipe name
    uint8_t  track_count;             // [16]     1..MAX_TRACKS_PER_SCENARIO (6)
    uint8_t  cont_count;              // [17]     ContinuousBehavior slot count (0 у MVP)
    uint8_t  resource_count;          // [18]     scenario-scope resource declarations
    uint8_t  global_trans_count;      // [19]     global transition count
    uint8_t  completion_rule;         // [20]     MODR_COMPLETION_*
    uint8_t  reserved_a;              // [21]
    uint16_t track_table_off;         // [22..23] → modr_track[track_count]
    uint16_t param_pool_off;          // [24..25] → modr_param_entry[param_pool_count]
    uint16_t param_pool_count;        // [26..27]
    uint16_t action_pool_off;         // [28..29] → modr_action[action_pool_count]
    uint16_t action_pool_count;       // [30..31]
    uint16_t cond_pool_off;           // [32..33] → modr_action[cond_pool_count] (conditions)
    uint16_t cond_pool_count;         // [34..35]
    uint16_t string_pool_off;         // [36..37] → length-prefixed strings, 1-byte aligned
    uint16_t global_trans_off;        // [38..39] → modr_global_transition[global_trans_count]
    uint16_t resource_off;            // [40..41] → modr_resource_decl[resource_count]
    uint16_t reserved_b;              // [42..43] padding for uint32 alignment
    uint32_t default_phase_timeout_ms;// [44..47] applied to phases without explicit timeout
    uint32_t scenario_timeout_max_ms; // [48..51] hard scenario cap (0 = no limit)
    uint32_t reserved_c;              // [52..55] reserved для future use
};
static_assert(sizeof(modr_header) == 56, "modr_header must be 56 bytes");

/**
 * Track entry — 16 bytes per track. Located at header.track_table_off.
 */
struct modr_track {                   // 16 bytes
    uint16_t name_str_idx;            // [0..1]   string pool index
    uint16_t phases_off;              // [2..3]   → modr_phase[phase_count]
    uint16_t initial_phase;           // [4..5]   usually 0
    uint16_t reserved_a;              // [6..7]
    uint8_t  phase_count;             // [8]      number of phases у цьому track
    uint8_t  flags;                   // [9]      MODR_TRACK_FLAG_*
    uint16_t reserved_b;              // [10..11]
    uint32_t reserved_c;              // [12..15] reserved для future use
};
static_assert(sizeof(modr_track) == 16, "modr_track must be 16 bytes");

/**
 * Phase entry — 20 bytes. Each track has its own phase array.
 *
 * Layout reorganized from plan Q1 для natural alignment (uint16 fields grouped,
 * uint8 fields grouped, uint32 last). +4 bytes для phase_resources_* fields
 * added у Step 0.75 paper pilot (phase-level resource claims).
 */
struct modr_phase {                   // 20 bytes
    uint16_t name_str_idx;            // [0..1]   string pool index
    uint16_t entry_action_off;        // [2..3]   → action pool index, або MODR_NO_OFFSET
    uint16_t exit_action_off;         // [4..5]   → action pool index, або MODR_NO_OFFSET
    uint16_t transitions_off;         // [6..7]   → transition[] inline у phase data area
    uint8_t  entry_action_n;          // [8]
    uint8_t  exit_action_n;           // [9]
    uint8_t  transition_n;            // [10]
    uint8_t  cont_mask;               // [11]     bitmask which ContinuousBehaviors active
    uint32_t timeout_ms;              // [12..15] 0 → use header.default_phase_timeout_ms
    uint16_t phase_resources_off;     // [16..17] → modr_phase_resource_claim[]
    uint8_t  phase_resource_n;        // [18]     0 if no phase-scope resources used
    uint8_t  reserved;                // [19]
};
static_assert(sizeof(modr_phase) == 20, "modr_phase must be 20 bytes");

/**
 * Transition record — 12 bytes (corrected from plan Q1 spec'd 8 bytes).
 *
 * Plan spec was: 2+2+1+1+4 = 10 bytes але listed як 8. Actual layout requires
 * 12 bytes для 4-byte alignment of time_threshold_ms uint32.
 */
struct modr_transition {              // 12 bytes
    uint16_t cond_pool_idx;           // [0..1]   condition index, 0xFFFF = unconditional
    uint16_t target_phase;            // [2..3]   target phase idx, MODR_TARGET_COMPLETE/ABORT
    uint32_t time_threshold_ms;       // [4..7]   used якщо kind ∈ {time, time_or_cond, time_and_cond}
    uint8_t  kind;                    // [8]      MODR_TRANS_KIND_*
    uint8_t  reserved_a;              // [9]
    uint16_t reserved_b;              // [10..11]
};
static_assert(sizeof(modr_transition) == 12, "modr_transition must be 12 bytes");

/**
 * Global transition — 8 bytes. Applied to ALL tracks each tick before per-phase
 * transitions. Sorted by priority (descending). First match fires.
 */
struct modr_global_transition {       // 8 bytes
    uint16_t cond_pool_idx;           // [0..1]
    uint16_t reserved_a;              // [2..3]
    uint8_t  kind;                    // [4]      MODR_TRANS_KIND_COND для globals
    uint8_t  priority;                // [5]      higher = evaluated first
    uint8_t  scope;                   // [6]      MODR_GLOBAL_SCOPE_SCENARIO або _MAIN_TRACK
    uint8_t  reserved_b;              // [7]
};
static_assert(sizeof(modr_global_transition) == 8, "modr_global_transition must be 8 bytes");

/**
 * Action record — 8 bytes (padded from 6 для alignment).
 *
 * Plan spec'd 6 bytes але 6 не aligned. Padded to 8 з reserved field.
 */
struct modr_action {                  // 8 bytes
    uint16_t action_hash;             // [0..1]   djb2 low16 of action name
    uint16_t param_idx;               // [2..3]   first param index, MODR_NO_OFFSET = none
    uint8_t  param_n;                 // [4]      number of params consumed by this action
    uint8_t  flags;                   // [5]      MODR_ACTION_FLAG_*
    uint16_t reserved;                // [6..7]
};
static_assert(sizeof(modr_action) == 8, "modr_action must be 8 bytes");

/**
 * Param pool entry — 8 bytes.
 *
 * Параметри referenceable з conditions/actions через `@param:<name>` syntax у
 * authoring JSON. Compiler resolves `@param:moisture_low` → param_idx (uint16);
 * runtime substitutes value при evaluation. See plan Q1 і Step 0.75 ADR-0008.
 */
struct modr_param_entry {             // 8 bytes
    uint16_t key_hash;                // [0..1]   recipe-scoped param name hash (djb2)
    uint8_t  type;                    // [2]      MODR_PARAM_TYPE_*
    uint8_t  flags;                   // [3]      MODR_PARAM_FLAG_*
    uint32_t value;                   // [4..7]   payload (i32/f32/bool padded/u16 str_idx)
};
static_assert(sizeof(modr_param_entry) == 8, "modr_param_entry must be 8 bytes");

/**
 * Scenario-scope resource declaration — 4 bytes. Engine claims at start(),
 * releases on completion/abort/unload.
 */
struct modr_resource_decl {           // 4 bytes
    uint16_t resource_hash;           // [0..1]   djb2 of resource name (e.g. "equipment.pump")
    uint8_t  exclusive;               // [2]      1 = exclusive, 0 = shared
    uint8_t  scope;                   // [3]      MODR_RESOURCE_SCOPE_SCENARIO для цієї struct
};
static_assert(sizeof(modr_resource_decl) == 4, "modr_resource_decl must be 4 bytes");

/**
 * Phase-scope resource claim — 4 bytes. Engine claims at phase entry, releases
 * at phase exit. Used для shared resources used briefly (greenhouse pump shared
 * between zones). Discovered у Step 0.75 paper pilot.
 */
struct modr_phase_resource_claim {    // 4 bytes
    uint16_t resource_hash;           // [0..1]
    uint8_t  exclusive;               // [2]
    uint8_t  reserved;                // [3]
};
static_assert(sizeof(modr_phase_resource_claim) == 4,
              "modr_phase_resource_claim must be 4 bytes");

// ─────────────────────────────────────────────────────────────────────
// Helper functions (header-only, no .cpp file для Step 1)
// ─────────────────────────────────────────────────────────────────────

/**
 * djb2 hash producing low-16 bits. Used для action names, condition names,
 * resource names, param names. Matched bit-for-bit з Python implementation
 * у tools/compile_scenario.py.
 */
constexpr uint16_t djb2_hash16(const char* str) noexcept {
    uint32_t hash = 5381u;
    while (*str) {
        hash = ((hash << 5) + hash) + static_cast<uint8_t>(*str);
        ++str;
    }
    return static_cast<uint16_t>(hash & 0xFFFFu);
}

}  // namespace modesp::sequence
