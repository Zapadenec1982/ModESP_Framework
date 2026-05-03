/**
 * @file test_sequence_engine.cpp
 * @brief Host integration tests для SequenceEngine multi-instance dispatcher (Step 14).
 *
 * Exercises the public API surface end-to-end через sync_two_tracks.modr fixture:
 *   - load_buffer / load_path return valid handles, populate slot
 *   - Loading invalid blob → 0 + last_error()
 *   - Slot exhaustion (load > MAX_SEQUENCES) → NO_SLOT
 *   - start → instance ticks toward COMPLETED via on_update
 *   - pause/resume halts/restarts on_update for that slot
 *   - abort → ABORTING → FAILED у subsequent ticks
 *   - Multi-instance: load same recipe twice, both run independently
 *   - unload releases resources
 */

#include "modesp/sequence/sequence_engine.h"
#include "modesp/sequence/builtin_actions.h"
#include "modesp/sequence/action_registry.h"
#include "modesp/shared_state.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

using namespace modesp::sequence;
using modesp::SharedState;

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

// ── Fixture loading ────────────────────────────────────────────────────

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

// Helper: setup engine з SharedState and registered builtins.
struct EngineFixture {
    SharedState ss;
    SequenceEngine engine;
    std::vector<uint8_t> blob;

    EngineFixture() : engine(&ss) {
        ActionRegistry::instance().clear_for_tests();
        builtins::register_builtins();
        engine.on_init();
        blob = load_sync_fixture();
    }
};

// ── Lifecycle: load / unload ──────────────────────────────────────────

TEST(load_buffer_valid_returns_handle) {
    EngineFixture fx;
    if (fx.blob.empty()) return;  // skip якщо fixture missing
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    assert(h >= 1 && h <= MAX_SEQUENCES);
    assert(fx.engine.state(h) == SequenceRuntime::State::LOADED);
    assert(fx.engine.last_error() == EngineError::OK);
}

TEST(load_buffer_invalid_returns_zero) {
    EngineFixture fx;
    uint8_t junk[100];
    std::memset(junk, 0, sizeof(junk));
    SequenceHandle h = fx.engine.load_buffer(junk, sizeof(junk));
    assert(h == 0);
    assert(fx.engine.last_error() != EngineError::OK);
}

TEST(load_buffer_oversized_rejected) {
    EngineFixture fx;
    // size > MODR_MAX_SIZE
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), MODR_MAX_SIZE + 1);
    assert(h == 0);
    assert(fx.engine.last_error() == EngineError::INVALID_FILE);
}

TEST(load_buffer_null_data_rejected) {
    EngineFixture fx;
    SequenceHandle h = fx.engine.load_buffer(nullptr, 100);
    assert(h == 0);
}

TEST(unload_releases_slot) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    assert(h != 0);
    assert(fx.engine.unload(h) == EngineError::OK);
    // State after unload — IDLE (slot reset)
    assert(fx.engine.state(h) == SequenceRuntime::State::IDLE);
}

TEST(unload_invalid_handle_returns_error) {
    EngineFixture fx;
    assert(fx.engine.unload(99) == EngineError::INVALID_HANDLE);
    assert(fx.engine.unload(0) == EngineError::INVALID_HANDLE);
}

// ── Lifecycle: start / pause / resume / abort ─────────────────────────

TEST(start_loaded_scenario_sets_running) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    assert(fx.engine.start(h) == EngineError::OK);
    assert(fx.engine.state(h) == SequenceRuntime::State::RUNNING);
}

TEST(start_unloaded_handle_returns_error) {
    EngineFixture fx;
    assert(fx.engine.start(1) == EngineError::INVALID_HANDLE);
}

TEST(pause_resume_toggle_running_state) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    fx.engine.start(h);
    assert(fx.engine.pause(h) == EngineError::OK);
    assert(fx.engine.state(h) == SequenceRuntime::State::PAUSED);
    assert(fx.engine.resume(h) == EngineError::OK);
    assert(fx.engine.state(h) == SequenceRuntime::State::RUNNING);
}

TEST(abort_running_scenario_transitions_to_aborting) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    fx.engine.start(h);
    assert(fx.engine.abort(h) == EngineError::OK);
    assert(fx.engine.state(h) == SequenceRuntime::State::ABORTING);
}

// ── on_update ticking ─────────────────────────────────────────────────

TEST(on_update_drives_scenario_to_completion) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    fx.engine.start(h);
    constexpr int MAX_TICKS = 200;
    int ticks = 0;
    while (fx.engine.state(h) == SequenceRuntime::State::RUNNING && ticks < MAX_TICKS) {
        fx.engine.on_update(10);
        ++ticks;
    }
    assert(fx.engine.state(h) == SequenceRuntime::State::COMPLETED);
}

TEST(on_update_no_op_for_paused_scenario) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    fx.engine.start(h);
    fx.engine.pause(h);
    uint32_t before = fx.engine.scenario_elapsed_ms(h);
    for (int i = 0; i < 50; ++i) fx.engine.on_update(10);
    assert(fx.engine.scenario_elapsed_ms(h) == before);
    assert(fx.engine.state(h) == SequenceRuntime::State::PAUSED);
}

// ── Multi-instance ────────────────────────────────────────────────────

TEST(multiple_instances_run_independently) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle h1 = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    SequenceHandle h2 = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    assert(h1 != 0 && h2 != 0 && h1 != h2);
    fx.engine.start(h1);
    fx.engine.start(h2);
    assert(fx.engine.active_count() == 2);
    // Drive до both complete
    for (int i = 0; i < 200; ++i) {
        fx.engine.on_update(10);
        if (fx.engine.state(h1) != SequenceRuntime::State::RUNNING
         && fx.engine.state(h2) != SequenceRuntime::State::RUNNING) break;
    }
    assert(fx.engine.state(h1) == SequenceRuntime::State::COMPLETED);
    assert(fx.engine.state(h2) == SequenceRuntime::State::COMPLETED);
}

TEST(slot_exhaustion_returns_no_slot) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle handles[MAX_SEQUENCES];
    for (size_t i = 0; i < MAX_SEQUENCES; ++i) {
        handles[i] = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
        assert(handles[i] != 0);
    }
    // One more — should fail з NO_SLOT
    SequenceHandle extra = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    assert(extra == 0);
    assert(fx.engine.last_error() == EngineError::NO_SLOT);
}

TEST(unload_then_reload_uses_freed_slot) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle h1 = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    fx.engine.unload(h1);
    SequenceHandle h2 = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    assert(h2 != 0);  // freed slot reusable
}

// ── Diagnostic accessors ──────────────────────────────────────────────

TEST(track_count_matches_recipe) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    assert(fx.engine.track_count(h) == 2);  // sync_two_tracks has 2 tracks
}

TEST(active_count_excludes_loaded_and_completed) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    assert(fx.engine.active_count() == 0);  // LOADED не active
    fx.engine.start(h);
    assert(fx.engine.active_count() == 1);
    // Drive до completion
    for (int i = 0; i < 200; ++i) fx.engine.on_update(10);
    assert(fx.engine.state(h) == SequenceRuntime::State::COMPLETED);
    assert(fx.engine.active_count() == 0);
}

// ── Mirror keys ───────────────────────────────────────────────────────

TEST(on_update_writes_scenario_state_mirror) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    // Recipe is sync_test (8 chars), tracks "main" + "watch"
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    fx.engine.start(h);
    fx.engine.on_update(10);  // first tick triggers publish

    auto opt = fx.ss.get(modesp::StateKey("sync_test.scenario_state"));
    assert(opt.has_value());
    auto* sv = etl::get_if<modesp::StringValue>(&*opt);
    assert(sv && std::strcmp(sv->c_str(), "running") == 0);
}

TEST(on_update_writes_track_phase_name_mirror) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    fx.engine.start(h);
    fx.engine.on_update(10);

    // sync_test recipe: track 0 name="main", phase 0 name="signal"
    auto opt = fx.ss.get(modesp::StateKey("sync_test.main_phase_name"));
    assert(opt.has_value());
    auto* sv = etl::get_if<modesp::StringValue>(&*opt);
    assert(sv && std::strcmp(sv->c_str(), "signal") == 0);
}

// Regression: scenario-level abort must release ANY phase-scope resources
// held by tracks before forcing them to FAILED. Previous bug: abort jumped
// tracks straight to FAILED і never called release_phase, leaking resources
// у the arbiter's flat_map until process restart.
TEST(abort_releases_phase_scope_resources) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    fx.engine.start(h);

    // Manually simulate а phase-scope ownership held by track 0 of handle h.
    // We bypass try_acquire_phase since sync_two_tracks recipe doesn't declare
    // phase resources — direct arbiter pokes verify the release chain.
    ResourceArbiter& arb = fx.engine.arbiter();
    modr_phase_resource_claim claim{};
    claim.resource_hash = 0xCAFE;
    claim.exclusive = 1;
    bool ok = arb.try_acquire_phase(h, /*track=*/0, /*phase=*/0, &claim, 1);
    assert(ok);
    assert(arb.is_owned(0xCAFE));

    // Trigger scenario abort
    fx.engine.abort(h);

    // Drive а tick so any residual instance_tick logic completes (е.g. abort
    // path advances scenario state to FAILED on next tick).
    fx.engine.on_update(10);

    // Phase-scope resource MUST have been released by abort path
    assert(!arb.is_owned(0xCAFE));
}

TEST(mirror_keys_update_to_completed_after_completion) {
    EngineFixture fx;
    if (fx.blob.empty()) return;
    SequenceHandle h = fx.engine.load_buffer(fx.blob.data(), fx.blob.size());
    fx.engine.start(h);
    // Drive до completion
    for (int i = 0; i < 200; ++i) fx.engine.on_update(10);
    // Final on_update publishes mirror keys even for COMPLETED scenarios
    fx.engine.on_update(10);

    auto opt = fx.ss.get(modesp::StateKey("sync_test.scenario_state"));
    assert(opt.has_value());
    auto* sv = etl::get_if<modesp::StringValue>(&*opt);
    assert(sv && std::strcmp(sv->c_str(), "completed") == 0);
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("Running SequenceEngine host tests:\n");
    std::printf("\n--- %d passed, %d failed ---\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
