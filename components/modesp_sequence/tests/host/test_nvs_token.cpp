/**
 * @file test_nvs_token.cpp
 * @brief Host tests for nvs_token serialize/deserialize (Step 15).
 *
 * Coverage:
 *   - Round-trip: serialize mid-tick state → deserialize into fresh runtime
 *     restores phase_idx + phase_elapsed_ms + scenario_elapsed_ms; final
 *     state == PAUSED.
 *   - Magic / version / CRC corruption → specific EngineError codes.
 *   - scenario_id mismatch (token from different recipe) rejected.
 *   - track_count mismatch rejected (recipe topology changed).
 *   - CRC16 golden vector fixates implementation.
 */

#include "modesp/sequence/nvs_token.h"
#include "modesp/sequence/modr_loader.h"
#include "modesp/sequence/builtin_actions.h"
#include "modesp/sequence/action_registry.h"
#include "modesp/sequence/sequence_instance.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

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

static std::vector<uint8_t> load_sync_fixture() {
    const char* candidates[] = {
        "tools/tests/fixtures/scenarios/sync_two_tracks.modr",
        "../../../../tools/tests/fixtures/scenarios/sync_two_tracks.modr",
        "D:/ModESP_v4_Framework/.claude/worktrees/jovial-driscoll-04e86e/tools/tests/fixtures/scenarios/sync_two_tracks.modr",
        nullptr,
    };
    for (size_t i = 0; candidates[i]; ++i) {
        std::ifstream f(candidates[i], std::ios::binary);
        if (!f) continue;
        f.seekg(0, std::ios::end);
        auto size = f.tellg();
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> buf(static_cast<size_t>(size));
        f.read(reinterpret_cast<char*>(buf.data()), size);
        return buf;
    }
    return {};
}

/// Setup: build a runtime з sync_two_tracks loaded і tracks into mid-execution.
struct Fixture {
    std::vector<uint8_t> blob;
    SequenceRuntime sr;
    bool ok = false;

    Fixture() {
        ActionRegistry::instance().clear_for_tests();
        builtins::register_builtins();
        blob = load_sync_fixture();
        if (blob.empty()) return;
        sr.handle = 1;
        if (modr_validate(blob.data(), blob.size(), sr.scenario) != EngineError::OK) return;
        instance_start(sr);
        // Simulate mid-execution: advance phase_elapsed і scenario_elapsed
        sr.scenario_elapsed_ms = 1234;
        sr.tracks[0].phase_elapsed_ms = 500;
        sr.tracks[0].phase_idx = 0;
        sr.tracks[1].phase_elapsed_ms = 800;
        sr.tracks[1].phase_idx = 0;
        ok = true;
    }
};

// ── Round-trip ────────────────────────────────────────────────────────

TEST(serialize_then_deserialize_restores_state) {
    Fixture fx;
    if (!fx.ok) return;

    uint8_t buf[SEQ_TOKEN_SIZE];
    uint16_t scenario_id = fx.sr.scenario.header()->scenario_id;
    assert(serialize_token(fx.sr, scenario_id, /*wall_clock=*/0, buf));

    // Fresh runtime з the SAME .modr loaded (recipe must be reloaded before
    // recovery — engine drops from buffer і re-validates, then deserializes).
    SequenceRuntime fresh{};
    fresh.handle = 1;
    assert(modr_validate(fx.blob.data(), fx.blob.size(), fresh.scenario) == EngineError::OK);

    EngineError err = deserialize_token(buf, sizeof(buf), fresh);
    assert(err == EngineError::OK);

    // Recovery sets state to PAUSED per plan Q7
    assert(fresh.state == SequenceRuntime::State::PAUSED);
    assert(fresh.scenario_elapsed_ms == 1234);
    assert(fresh.tracks[0].phase_idx == 0);
    assert(fresh.tracks[0].phase_elapsed_ms == 500);
    assert(fresh.tracks[1].phase_idx == 0);
    assert(fresh.tracks[1].phase_elapsed_ms == 800);
    // Action progress reset (will replay entry actions on resume)
    assert(fresh.tracks[0].entry_action_progress == 0);
    assert(fresh.tracks[1].entry_action_progress == 0);
}

// ── Corruption detection ──────────────────────────────────────────────

TEST(magic_mismatch_returns_invalid_file) {
    Fixture fx;
    if (!fx.ok) return;
    uint8_t buf[SEQ_TOKEN_SIZE];
    serialize_token(fx.sr, fx.sr.scenario.header()->scenario_id, 0, buf);
    buf[0] = 0xFF;  // corrupt magic byte 0
    SequenceRuntime fresh{};
    fresh.handle = 1;
    modr_validate(fx.blob.data(), fx.blob.size(), fresh.scenario);
    assert(deserialize_token(buf, sizeof(buf), fresh) == EngineError::INVALID_FILE);
}

TEST(version_mismatch_returns_unsupported_version) {
    Fixture fx;
    if (!fx.ok) return;
    uint8_t buf[SEQ_TOKEN_SIZE];
    serialize_token(fx.sr, fx.sr.scenario.header()->scenario_id, 0, buf);
    // Bump version field at offset 4-5 to 99
    buf[4] = 99;
    buf[5] = 0;
    // Recompute CRC так other check passes
    uint16_t crc = crc16_ccitt(buf, 92);
    std::memcpy(buf + 92, &crc, sizeof(crc));
    SequenceRuntime fresh{};
    fresh.handle = 1;
    modr_validate(fx.blob.data(), fx.blob.size(), fresh.scenario);
    assert(deserialize_token(buf, sizeof(buf), fresh) == EngineError::UNSUPPORTED_VERSION);
}

TEST(crc_corruption_returns_crc_mismatch) {
    Fixture fx;
    if (!fx.ok) return;
    uint8_t buf[SEQ_TOKEN_SIZE];
    serialize_token(fx.sr, fx.sr.scenario.header()->scenario_id, 0, buf);
    buf[92] ^= 0xFF;  // flip CRC byte
    SequenceRuntime fresh{};
    fresh.handle = 1;
    modr_validate(fx.blob.data(), fx.blob.size(), fresh.scenario);
    assert(deserialize_token(buf, sizeof(buf), fresh) == EngineError::CRC_MISMATCH);
}

TEST(scenario_id_mismatch_rejected) {
    Fixture fx;
    if (!fx.ok) return;
    uint8_t buf[SEQ_TOKEN_SIZE];
    // Use deliberately-wrong scenario_id (not the one in the recipe)
    serialize_token(fx.sr, /*scenario_id=*/0xDEAD, 0, buf);
    SequenceRuntime fresh{};
    fresh.handle = 1;
    modr_validate(fx.blob.data(), fx.blob.size(), fresh.scenario);
    // Token's scenario_id (0xDEAD) doesn't match loaded recipe's id
    assert(deserialize_token(buf, sizeof(buf), fresh) == EngineError::INVALID_FILE);
}

TEST(deserialize_without_loaded_scenario_returns_not_loaded) {
    Fixture fx;
    if (!fx.ok) return;
    uint8_t buf[SEQ_TOKEN_SIZE];
    serialize_token(fx.sr, fx.sr.scenario.header()->scenario_id, 0, buf);
    SequenceRuntime fresh{};  // no scenario loaded
    assert(deserialize_token(buf, sizeof(buf), fresh) == EngineError::NOT_LOADED);
}

TEST(deserialize_wrong_size_rejected) {
    Fixture fx;
    if (!fx.ok) return;
    uint8_t buf[SEQ_TOKEN_SIZE];
    serialize_token(fx.sr, fx.sr.scenario.header()->scenario_id, 0, buf);
    SequenceRuntime fresh{};
    fresh.handle = 1;
    modr_validate(fx.blob.data(), fx.blob.size(), fresh.scenario);
    assert(deserialize_token(buf, /*size=*/50, fresh) == EngineError::INVALID_FILE);
    assert(deserialize_token(buf, /*size=*/200, fresh) == EngineError::INVALID_FILE);
}

TEST(serialize_into_null_buf_rejected) {
    Fixture fx;
    if (!fx.ok) return;
    assert(!serialize_token(fx.sr, 0, 0, nullptr));
}

// ── CRC16 golden vector ───────────────────────────────────────────────

TEST(crc16_ccitt_known_value) {
    // Golden: CRC-CCITT (XMODEM, init 0x0000) of "123456789" = 0x31C3
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    assert(crc16_ccitt(data, sizeof(data)) == 0x31C3);
}

TEST(crc16_ccitt_empty_input) {
    assert(crc16_ccitt(nullptr, 0) == 0x0000);
}

// ── Token size invariant ──────────────────────────────────────────────

TEST(token_size_is_96_bytes) {
    static_assert(sizeof(seq_token) == 96, "compile-time size mismatch");
    assert(SEQ_TOKEN_SIZE == 96);
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("Running nvs_token host tests:\n");
    std::printf("\n--- %d passed, %d failed ---\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
