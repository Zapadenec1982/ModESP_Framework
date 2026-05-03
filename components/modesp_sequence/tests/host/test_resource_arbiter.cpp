/**
 * @file test_resource_arbiter.cpp
 * @brief Host unit tests for ResourceArbiter (Step 10).
 *
 * Coverage:
 *   - Empty arbiter: nothing owned, count==0
 *   - Scenario acquire: success, atomic on conflict, idempotent re-grant
 *   - Scenario release: removes only matching ownerships
 *   - Phase acquire: success, conflict, atomic rollback
 *   - Phase release: removes only (handle, track) matches
 *   - Cross-instance contention: handle 1 holds → handle 2 contended
 *   - Mixed scenario + phase ownership coexistence
 *   - Self-claim re-grant (same (handle, track) → OK)
 *   - clear_for_tests resets state
 */

#include "modesp/sequence/resource_arbiter.h"
#include <cassert>
#include <cstdio>

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

static modr_resource_decl decl(uint16_t hash, bool exclusive) {
    modr_resource_decl d{};
    d.resource_hash = hash;
    d.exclusive = exclusive ? 1 : 0;
    d.scope = MODR_RESOURCE_SCOPE_SCENARIO;
    return d;
}

static modr_phase_resource_claim claim(uint16_t hash, bool exclusive) {
    modr_phase_resource_claim c{};
    c.resource_hash = hash;
    c.exclusive = exclusive ? 1 : 0;
    return c;
}

// ── Initial state ──────────────────────────────────────────────────────

TEST(empty_arbiter_has_no_ownerships) {
    ResourceArbiter ra;
    assert(ra.count() == 0);
    assert(!ra.is_owned(0x1234));
    assert(ra.owner_of(0x1234) == nullptr);
}

// ── Scenario acquire ────────────────────────────────────────────────────

TEST(acquire_scenario_single_succeeds) {
    ResourceArbiter ra;
    auto d = decl(0x1234, true);
    assert(ra.acquire_scenario(1, &d, 1) == EngineError::OK);
    assert(ra.is_owned(0x1234));
    auto* o = ra.owner_of(0x1234);
    assert(o && o->handle == 1 && o->track_idx == TRACK_IDX_SCENARIO && o->exclusive == 1);
}

TEST(acquire_scenario_multiple_atomically) {
    ResourceArbiter ra;
    modr_resource_decl decls[3] = { decl(0x1111, true), decl(0x2222, true), decl(0x3333, true) };
    assert(ra.acquire_scenario(1, decls, 3) == EngineError::OK);
    assert(ra.count() == 3);
    assert(ra.is_owned(0x1111) && ra.is_owned(0x2222) && ra.is_owned(0x3333));
}

TEST(acquire_scenario_zero_count_is_ok) {
    ResourceArbiter ra;
    assert(ra.acquire_scenario(1, nullptr, 0) == EngineError::OK);
    assert(ra.count() == 0);
}

TEST(acquire_scenario_contended_returns_error_no_partial_state) {
    ResourceArbiter ra;
    // Handle 1 takes 0x1111 і 0x2222
    modr_resource_decl initial[] = { decl(0x1111, true), decl(0x2222, true) };
    assert(ra.acquire_scenario(1, initial, 2) == EngineError::OK);

    // Handle 2 wants 0x3333 (free), 0x1111 (taken) — should fail з NO new state
    modr_resource_decl request[] = { decl(0x3333, true), decl(0x1111, true) };
    assert(ra.acquire_scenario(2, request, 2) == EngineError::RESOURCE_CONTENDED);
    // 0x3333 should NOT be owned (atomicity)
    assert(!ra.is_owned(0x3333));
    // 0x1111, 0x2222 still owned by handle 1
    assert(ra.owner_of(0x1111)->handle == 1);
    assert(ra.owner_of(0x2222)->handle == 1);
    assert(ra.count() == 2);
}

TEST(acquire_scenario_idempotent_for_same_handle) {
    ResourceArbiter ra;
    auto d = decl(0x1234, true);
    assert(ra.acquire_scenario(1, &d, 1) == EngineError::OK);
    // Re-grant by same handle — must succeed without duplicate insert
    assert(ra.acquire_scenario(1, &d, 1) == EngineError::OK);
    assert(ra.count() == 1);
}

// ── Scenario release ────────────────────────────────────────────────────

TEST(release_scenario_removes_owned_resources) {
    ResourceArbiter ra;
    modr_resource_decl decls[2] = { decl(0x1111, true), decl(0x2222, true) };
    ra.acquire_scenario(1, decls, 2);
    ra.release_scenario(1);
    assert(ra.count() == 0);
    assert(!ra.is_owned(0x1111));
}

TEST(release_scenario_does_not_affect_other_handles) {
    ResourceArbiter ra;
    auto d1 = decl(0x1111, true);
    auto d2 = decl(0x2222, true);
    ra.acquire_scenario(1, &d1, 1);
    ra.acquire_scenario(2, &d2, 1);
    ra.release_scenario(1);
    assert(!ra.is_owned(0x1111));
    assert(ra.is_owned(0x2222));
    assert(ra.owner_of(0x2222)->handle == 2);
}

TEST(release_scenario_does_not_affect_phase_scope) {
    ResourceArbiter ra;
    auto d = decl(0x1111, true);
    ra.acquire_scenario(1, &d, 1);
    auto pc = claim(0x2222, true);
    ra.try_acquire_phase(1, /*track=*/0, /*phase=*/0, &pc, 1);
    ra.release_scenario(1);  // removes scenario-scope only
    assert(!ra.is_owned(0x1111));
    assert(ra.is_owned(0x2222));   // phase-scope survives
    assert(ra.owner_of(0x2222)->track_idx == 0);
}

// ── Phase acquire ───────────────────────────────────────────────────────

TEST(phase_acquire_success) {
    ResourceArbiter ra;
    auto pc = claim(0x4000, true);
    assert(ra.try_acquire_phase(1, 0, 2, &pc, 1));
    auto* o = ra.owner_of(0x4000);
    assert(o && o->handle == 1 && o->track_idx == 0 && o->phase_idx == 2);
}

TEST(phase_acquire_contended_returns_false_no_state_change) {
    ResourceArbiter ra;
    auto pc1 = claim(0x4000, true);
    ra.try_acquire_phase(1, 0, 0, &pc1, 1);
    auto pc2 = claim(0x4000, true);
    assert(!ra.try_acquire_phase(2, 0, 0, &pc2, 1));
    // Original ownership preserved
    assert(ra.owner_of(0x4000)->handle == 1);
}

TEST(phase_acquire_atomic_rollback) {
    ResourceArbiter ra;
    // Handle 1 holds 0x4001
    auto pc_a = claim(0x4001, true);
    ra.try_acquire_phase(1, 0, 0, &pc_a, 1);

    // Handle 2 wants {0x4000 (free), 0x4001 (taken)} — must fail with no
    // partial state (i.e., 0x4000 not acquired)
    modr_phase_resource_claim batch[] = { claim(0x4000, true), claim(0x4001, true) };
    assert(!ra.try_acquire_phase(2, 0, 0, batch, 2));
    assert(!ra.is_owned(0x4000));  // not acquired due to rollback
    assert(ra.owner_of(0x4001)->handle == 1);  // original owner preserved
}

TEST(phase_acquire_self_idempotent) {
    ResourceArbiter ra;
    auto pc = claim(0x4000, true);
    assert(ra.try_acquire_phase(1, 0, 0, &pc, 1));
    // Same (handle, track) — re-grant OK
    assert(ra.try_acquire_phase(1, 0, 1, &pc, 1));
    assert(ra.count() == 1);
}

// ── Phase release ───────────────────────────────────────────────────────

TEST(release_phase_removes_only_matching_track) {
    ResourceArbiter ra;
    auto pc1 = claim(0x4000, true);
    auto pc2 = claim(0x4001, true);
    ra.try_acquire_phase(1, /*track=*/0, 0, &pc1, 1);
    ra.try_acquire_phase(1, /*track=*/1, 0, &pc2, 1);
    ra.release_phase(1, 0);
    assert(!ra.is_owned(0x4000));
    assert(ra.is_owned(0x4001));   // track 1 still owns
}

TEST(release_phase_does_not_affect_scenario_scope) {
    ResourceArbiter ra;
    auto d = decl(0x1111, true);
    ra.acquire_scenario(1, &d, 1);
    auto pc = claim(0x2222, true);
    ra.try_acquire_phase(1, 0, 0, &pc, 1);
    ra.release_phase(1, 0);
    assert(ra.is_owned(0x1111));   // scenario scope survives
    assert(!ra.is_owned(0x2222));  // phase scope released
}

// ── Cross-instance contention ──────────────────────────────────────────

TEST(different_instances_contend_for_same_resource) {
    ResourceArbiter ra;
    auto d = decl(0x9999, true);
    assert(ra.acquire_scenario(1, &d, 1) == EngineError::OK);
    assert(ra.acquire_scenario(2, &d, 1) == EngineError::RESOURCE_CONTENDED);
}

TEST(after_first_releases_second_can_acquire) {
    ResourceArbiter ra;
    auto d = decl(0xAAAA, true);
    ra.acquire_scenario(1, &d, 1);
    ra.release_scenario(1);
    assert(ra.acquire_scenario(2, &d, 1) == EngineError::OK);
    assert(ra.owner_of(0xAAAA)->handle == 2);
}

// ── Mixed scope ─────────────────────────────────────────────────────────

TEST(scenario_and_phase_can_coexist_for_same_handle) {
    ResourceArbiter ra;
    auto d = decl(0x0001, true);
    auto pc = claim(0x0002, true);
    assert(ra.acquire_scenario(1, &d, 1) == EngineError::OK);
    assert(ra.try_acquire_phase(1, 0, 0, &pc, 1));
    assert(ra.count() == 2);
    auto* o1 = ra.owner_of(0x0001);
    auto* o2 = ra.owner_of(0x0002);
    assert(o1->track_idx == TRACK_IDX_SCENARIO);
    assert(o2->track_idx == 0);
}

// ── Reset ───────────────────────────────────────────────────────────────

TEST(clear_for_tests_resets_all_state) {
    ResourceArbiter ra;
    auto d = decl(0xBEEF, true);
    ra.acquire_scenario(1, &d, 1);
    ra.clear_for_tests();
    assert(ra.count() == 0);
}

// Regression: rollback на capacity-exhaustion must NOT erase pre-existing
// (idempotent re-grant) ownerships. Previous bug: rollback iterated всі j<i
// і erased any matching handle, including resources що were no-op skipped
// because already owned by ця same handle.
//
// Test setup: fill arbiter to MAX_RESOURCES-1 (31 з 32) із handle 1 ownerships.
// Then attempt а 3-resource batch acquire from handle 1: [pre_existing, new_a,
// new_b]. Expected behavior:
//   - Iter 0 (pre_existing): continue (idempotent), inserted[0]=false
//   - Iter 1 (new_a): inserts, fills last slot, inserted[1]=true
//   - Iter 2 (new_b): owners_.full() → rollback
// Old code: rollback erased resources[0].hash → silent ownership loss.
// New code: skips inserted[0]==false → preserves pre-existing. Erases only new_a.
TEST(rollback_preserves_preexisting_when_capacity_exhausted) {
    ResourceArbiter ra;
    constexpr uint16_t PRE_EXISTING_HASH = 0x1000;

    // Fill arbiter to MAX_RESOURCES-1 (31 entries) — last slot kept open
    // для controlled rollback trigger.
    for (uint16_t i = 0; i < MAX_RESOURCES - 1; ++i) {
        auto d = decl(static_cast<uint16_t>(0x2000 + i), true);
        assert(ra.acquire_scenario(1, &d, 1) == EngineError::OK);
    }
    // Plus one pre-existing target hash що ми будемо re-granting through batch
    auto pre = decl(PRE_EXISTING_HASH, true);
    // Wait — це вже at capacity. Let me re-do: fill to MAX_RESOURCES-2, then
    // add pre_existing → у total MAX_RESOURCES-1 entries. One slot free.
    ra.clear_for_tests();
    for (uint16_t i = 0; i < MAX_RESOURCES - 2; ++i) {
        auto d = decl(static_cast<uint16_t>(0x2000 + i), true);
        assert(ra.acquire_scenario(1, &d, 1) == EngineError::OK);
    }
    assert(ra.acquire_scenario(1, &pre, 1) == EngineError::OK);
    assert(ra.count() == MAX_RESOURCES - 1);  // 31 of 32
    assert(ra.is_owned(PRE_EXISTING_HASH));

    // Batch acquire that triggers rollback at iter 2:
    //   iter 0: pre_existing — continue (idempotent)
    //   iter 1: new_a — fills last slot (32 of 32)
    //   iter 2: new_b — owners_.full() → rollback path
    modr_resource_decl batch[] = {
        decl(PRE_EXISTING_HASH, true),
        decl(0xBEEF, true),  // new_a
        decl(0xCAFE, true),  // new_b — triggers rollback
    };
    auto err = ra.acquire_scenario(1, batch, 3);
    assert(err == EngineError::RESOURCE_CONTENDED);

    // Critical assertion: pre_existing MUST still be owned.
    // Old buggy code erased це during rollback. New code preserves.
    assert(ra.is_owned(PRE_EXISTING_HASH));
    assert(ra.owner_of(PRE_EXISTING_HASH)->handle == 1);

    // new_a (inserted then rolled back) should NOT be owned
    assert(!ra.is_owned(0xBEEF));
    // new_b never inserted (failed at full() check)
    assert(!ra.is_owned(0xCAFE));

    // Total count must be unchanged from before the failed batch:
    // 30 originals + pre_existing = 31
    assert(ra.count() == MAX_RESOURCES - 1);
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("Running ResourceArbiter host tests:\n");
    std::printf("\n--- %d passed, %d failed ---\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
