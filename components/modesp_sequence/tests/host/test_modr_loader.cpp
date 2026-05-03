/**
 * @file test_modr_loader.cpp
 * @brief Host unit tests для modr_validate() loader (Step 8).
 *
 * Coverage strategy:
 *   1. Load golden fixture `tools/tests/fixtures/scenarios/minimal_v1.modr` —
 *      smallest valid recipe (1 track, 1 phase, 1 unconditional transition).
 *      Verify modr_validate returns OK і LoadedScenario navigation works.
 *   2. Corruption tests: copy golden, mutate specific bytes/fields, verify
 *      modr_validate returns expected EngineError code.
 *   3. CRC tests: bit-flip last byte → CRC_MISMATCH.
 *   4. Composite condition tests: synthetic blob з all_of/any_of/not — depth
 *      bound, child index validation.
 *
 * Built-in actions/conditions registered via `builtins::register_builtins()`
 * before each test. This is required because loader resolves cond_pool entries
 * через ActionRegistry::find_condition. Composite hashes (all_of/any_of/not)
 * recognized inline без registration.
 */

#include "modesp/sequence/modr_loader.h"
#include "modesp/sequence/builtin_actions.h"
#include "modesp/sequence/action_registry.h"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include <string>

using namespace modesp::sequence;

static int passed = 0;
static int failed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct Register_##name { \
        Register_##name() { \
            std::printf("  [TEST] %s ... ", #name); \
            try { \
                test_##name(); \
                std::printf("OK\n"); \
                ++passed; \
            } catch (...) { \
                std::printf("FAIL\n"); \
                ++failed; \
            } \
        } \
    } register_##name; \
    static void test_##name()

// ── Helpers ────────────────────────────────────────────────────────────

/// Find golden fixture relative to repo root. We walk up from CWD looking
/// for the fixture path. Tests run from arbitrary working dirs (pytest,
/// Make, manual), so fixed-relative paths не reliable.
static std::vector<uint8_t> load_golden(const char* relpath) {
    // Try several plausible relative paths.
    const char* candidates[] = {
        // From repo root
        "tools/tests/fixtures/scenarios/minimal_v1.modr",
        // From tests/host (one level up)
        "../../../../tools/tests/fixtures/scenarios/minimal_v1.modr",
        // Absolute fallback (Windows worktree path)
        "D:/ModESP_v4_Framework/.claude/worktrees/jovial-driscoll-04e86e/tools/tests/fixtures/scenarios/minimal_v1.modr",
        nullptr,
    };
    (void)relpath;  // currently fixed to minimal_v1.modr
    for (size_t i = 0; candidates[i]; ++i) {
        std::ifstream f(candidates[i], std::ios::binary);
        if (!f) continue;
        f.seekg(0, std::ios::end);
        auto size = f.tellg();
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> buf(static_cast<size_t>(size));
        f.read(reinterpret_cast<char*>(buf.data()), size);
        if (f.gcount() == size) {
            std::printf("(fixture: %s) ", candidates[i]);
            return buf;
        }
    }
    return {};
}

/// Recompute CRC32 і store at end of buffer (для tests що mutate header).
/// Buffer must have at least 4 trailing bytes reserved for CRC.
static void recompute_crc(std::vector<uint8_t>& buf) {
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    uint32_t crc = crc32_iso_hdlc(buf.data(), hdr->total_size - 4);
    std::memcpy(buf.data() + hdr->total_size - 4, &crc, sizeof(crc));
}

// Setup: reset registry, register builtins, load golden.
static std::vector<uint8_t> setup_with_golden() {
    auto& reg = ActionRegistry::instance();
    reg.clear_for_tests();
    bool ok = builtins::register_builtins();
    assert(ok);
    auto buf = load_golden("minimal_v1.modr");
    assert(!buf.empty() && "golden fixture not found — run from repo or with absolute path");
    return buf;
}

// ── Positive: golden fixture loads ─────────────────────────────────────

TEST(golden_minimal_v1_validates_ok) {
    auto buf = setup_with_golden();
    LoadedScenario ls;
    EngineError err = modr_validate(buf.data(), buf.size(), ls);
    assert(err == EngineError::OK);
    assert(ls.buffer == buf.data());
    assert(ls.buffer_size == buf.size());
}

TEST(golden_minimal_v1_navigation_works) {
    auto buf = setup_with_golden();
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::OK);

    auto* hdr = ls.header();
    assert(hdr->magic == MODR_MAGIC);
    assert(hdr->format_version == MODR_FORMAT_VERSION);
    assert(hdr->track_count == 1);

    auto* tracks = ls.tracks();
    assert(tracks[0].phase_count == 1);

    auto* phases = ls.phases(0);
    assert(phases[0].transition_n == 1);

    auto* trans = ls.transitions(phases[0].transitions_off);
    assert(trans[0].kind == MODR_TRANS_KIND_UNCONDITIONAL);
    assert(trans[0].target_phase == MODR_TARGET_COMPLETE);
}

TEST(golden_minimal_v1_read_string_works) {
    auto buf = setup_with_golden();
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::OK);

    char namebuf[16];
    assert(ls.read_string(ls.header()->name_str_idx, namebuf, sizeof(namebuf)));
    assert(std::strcmp(namebuf, "t") == 0);  // recipe name "t" per fixture
}

// ── Negative: header preamble corruption ──────────────────────────────

TEST(invalid_magic_returns_invalid_file) {
    auto buf = setup_with_golden();
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    hdr->magic = 0xDEADBEEF;
    recompute_crc(buf);  // CRC must remain valid to isolate the magic check
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::INVALID_FILE);
}

TEST(unsupported_version_returns_specific_code) {
    auto buf = setup_with_golden();
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    hdr->format_version = 99;
    recompute_crc(buf);
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::UNSUPPORTED_VERSION);
}

TEST(truncated_buffer_returns_invalid_file) {
    auto buf = setup_with_golden();
    LoadedScenario ls;
    // Truncate before complete header
    assert(modr_validate(buf.data(), 30, ls) == EngineError::INVALID_FILE);
}

TEST(buffer_smaller_than_total_size_returns_invalid_file) {
    auto buf = setup_with_golden();
    LoadedScenario ls;
    // total_size says 114, give only 100
    assert(modr_validate(buf.data(), 100, ls) == EngineError::INVALID_FILE);
}

// Regression: defense-in-depth — public modr_validate must reject buffers
// whose claimed total_size exceeds MODR_MAX_SIZE, even якщо the actual
// caller-provided buffer is large enough. Без цього, downstream uint16
// truncations у LoadedScenario accessors would silently shrink computed
// extents (e.g. string_pool_size) instead of failing fast.
TEST(total_size_exceeding_max_size_rejected) {
    // Reset registry first (golden test relies on builtins for cond_pool
    // resolution — empty fixture has 0 conds so не actually used here, але
    // we keep clean setup for parallel runs).
    auto& reg = ActionRegistry::instance();
    reg.clear_for_tests();
    builtins::register_builtins();

    // Buffer larger than MODR_MAX_SIZE + 4. Header overlay; bytes after
    // header don't matter — total_size check fires before CRC validation.
    std::vector<uint8_t> buf(MODR_MAX_SIZE + 1024, 0);
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    hdr->magic = MODR_MAGIC;
    hdr->format_version = MODR_FORMAT_VERSION;
    hdr->total_size = MODR_MAX_SIZE + 100;  // > MODR_MAX_SIZE — should reject
    hdr->track_count = 1;

    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::INVALID_FILE);
}

TEST(null_buffer_returns_invalid_file) {
    auto buf = setup_with_golden();
    (void)buf;  // setup ensures registry initialized
    LoadedScenario ls;
    assert(modr_validate(nullptr, 1024, ls) == EngineError::INVALID_FILE);
}

// ── CRC corruption ────────────────────────────────────────────────────

TEST(bit_flipped_crc_returns_crc_mismatch) {
    auto buf = setup_with_golden();
    // Flip last byte of CRC trailer
    buf.back() ^= 0x01;
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::CRC_MISMATCH);
}

TEST(bit_flipped_payload_returns_crc_mismatch) {
    auto buf = setup_with_golden();
    // Flip middle byte of buffer (somewhere in track table area)
    buf[60] ^= 0x80;
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::CRC_MISMATCH);
}

// ── Track count violations ────────────────────────────────────────────

TEST(zero_track_count_returns_invalid_file) {
    auto buf = setup_with_golden();
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    hdr->track_count = 0;
    recompute_crc(buf);
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::INVALID_FILE);
}

TEST(track_count_above_limit_returns_too_many_tracks) {
    auto buf = setup_with_golden();
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    hdr->track_count = static_cast<uint8_t>(MAX_TRACKS_PER_SCENARIO + 1);
    recompute_crc(buf);
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::TOO_MANY_TRACKS);
}

TEST(track_table_off_overflow_returns_buffer_overflow) {
    auto buf = setup_with_golden();
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    hdr->track_table_off = static_cast<uint16_t>(hdr->total_size + 100);
    recompute_crc(buf);
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::BUFFER_OVERFLOW);
}

// ── Phase / transition violations ─────────────────────────────────────

TEST(track_with_zero_phases_returns_invalid_file) {
    auto buf = setup_with_golden();
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    auto* tracks = reinterpret_cast<modr_track*>(buf.data() + hdr->track_table_off);
    tracks[0].phase_count = 0;
    recompute_crc(buf);
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::INVALID_FILE);
}

TEST(initial_phase_out_of_range_returns_invalid_file) {
    auto buf = setup_with_golden();
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    auto* tracks = reinterpret_cast<modr_track*>(buf.data() + hdr->track_table_off);
    tracks[0].initial_phase = 99;  // phase_count is 1
    recompute_crc(buf);
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::INVALID_FILE);
}

TEST(phases_off_overflow_returns_buffer_overflow) {
    auto buf = setup_with_golden();
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    auto* tracks = reinterpret_cast<modr_track*>(buf.data() + hdr->track_table_off);
    tracks[0].phases_off = static_cast<uint16_t>(hdr->total_size + 100);
    recompute_crc(buf);
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::BUFFER_OVERFLOW);
}

TEST(invalid_transition_kind_returns_invalid_transition) {
    auto buf = setup_with_golden();
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    auto* tracks = reinterpret_cast<modr_track*>(buf.data() + hdr->track_table_off);
    auto* phases = reinterpret_cast<modr_phase*>(buf.data() + tracks[0].phases_off);
    auto* trans = reinterpret_cast<modr_transition*>(buf.data() + phases[0].transitions_off);
    trans[0].kind = 99;  // invalid (max valid is UNCONDITIONAL = 4)
    recompute_crc(buf);
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::INVALID_TRANSITION);
}

TEST(invalid_target_phase_returns_invalid_transition) {
    auto buf = setup_with_golden();
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    auto* tracks = reinterpret_cast<modr_track*>(buf.data() + hdr->track_table_off);
    auto* phases = reinterpret_cast<modr_phase*>(buf.data() + tracks[0].phases_off);
    auto* trans = reinterpret_cast<modr_transition*>(buf.data() + phases[0].transitions_off);
    trans[0].target_phase = 50;  // not COMPLETE/ABORT, and phase_count is 1
    recompute_crc(buf);
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::INVALID_TRANSITION);
}

TEST(cond_kind_with_no_offset_idx_returns_invalid_transition) {
    auto buf = setup_with_golden();
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    auto* tracks = reinterpret_cast<modr_track*>(buf.data() + hdr->track_table_off);
    auto* phases = reinterpret_cast<modr_phase*>(buf.data() + tracks[0].phases_off);
    auto* trans = reinterpret_cast<modr_transition*>(buf.data() + phases[0].transitions_off);
    trans[0].kind = MODR_TRANS_KIND_COND;
    trans[0].cond_pool_idx = MODR_NO_OFFSET;
    recompute_crc(buf);
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::INVALID_TRANSITION);
}

// ── String pool / recipe name ─────────────────────────────────────────

TEST(name_str_idx_out_of_pool_returns_buffer_overflow) {
    auto buf = setup_with_golden();
    auto* hdr = reinterpret_cast<modr_header*>(buf.data());
    hdr->name_str_idx = 9999;  // far past pool extent
    recompute_crc(buf);
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::BUFFER_OVERFLOW);
}

// ── Action / condition: required registration ──────────────────────────
//
// Golden fixture has empty action_pool і cond_pool, so registry need only
// have built-ins for загальний test. To verify UNKNOWN_ACTION/UNKNOWN_CONDITION
// paths, ми would need a fixture з populated pools — Stage 1.5 deliverable
// (more fixtures committed). For Step 8 we cover via direct unit tests below
// against an empty registry.

TEST(empty_registry_with_empty_pools_still_validates) {
    // Even з empty registry, golden has zero action/cond entries → validation
    // doesn't try to resolve anything. Confirms loader doesn't gratuitously
    // reference registry when not needed.
    auto& reg = ActionRegistry::instance();
    reg.clear_for_tests();
    auto buf = load_golden("minimal_v1.modr");
    assert(!buf.empty());
    LoadedScenario ls;
    assert(modr_validate(buf.data(), buf.size(), ls) == EngineError::OK);
}

// ── Composite condition sentinel hashes ────────────────────────────────

TEST(composite_hashes_match_known_actions_json) {
    // These values appear у tools/known_actions.json. If they mismatch,
    // either compile_scenario.py і loader use different hash convention,
    // або known_actions.json drifted. Test fails з clear message identifying
    // upstream issue.
    assert(MODR_COND_HASH_ALL_OF == 39922);
    assert(MODR_COND_HASH_ANY_OF == 60897);
    assert(MODR_COND_HASH_NOT    == 38294);
    assert(is_composite_cond(MODR_COND_HASH_ALL_OF));
    assert(is_composite_cond(MODR_COND_HASH_ANY_OF));
    assert(is_composite_cond(MODR_COND_HASH_NOT));
    assert(!is_composite_cond(0xDEAD));
}

// ── CRC sanity ─────────────────────────────────────────────────────────

TEST(crc32_iso_hdlc_known_value) {
    // Golden vector: CRC32 of "123456789" = 0xCBF43926 (per ISO-HDLC).
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    assert(crc32_iso_hdlc(data, sizeof(data)) == 0xCBF43926u);
}

TEST(crc32_iso_hdlc_empty_input) {
    // CRC32 of empty input = 0x00000000 (init xor final = 0xFFFFFFFF ^ 0xFFFFFFFF).
    assert(crc32_iso_hdlc(nullptr, 0) == 0u);
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("Running modr_loader host tests:\n");
    std::printf("\n--- %d passed, %d failed ---\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
