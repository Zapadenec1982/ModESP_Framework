/**
 * @file test_action_registry.cpp
 * @brief Host unit tests для ActionRegistry singleton.
 *
 * Plain assert()-based tests. No doctest dependency. Independent of project-
 * level tests/host/ infrastructure (entangled з stripped refrigeration modules).
 *
 * Run: make test  (from this directory)
 *
 * Coverage:
 *   - Singleton instance() returns стабільну reference
 *   - register_action returns true on success
 *   - register_action duplicate hash returns false (collision detection)
 *   - register_action із hash != djb2(name) returns false (consistency check)
 *   - find_action returns descriptor for registered hash
 *   - find_action returns nullptr for unknown hash
 *   - register_condition separate namespace from actions
 *   - cross-namespace name reuse OK (action name == condition name allowed
 *     поки hashes різні; same hash у обох reg-ах також OK по design)
 *   - clear_for_tests resets state
 */

#include "modesp/sequence/action_registry.h"
#include <cassert>
#include <cstdio>

using namespace modesp::sequence;

// Simple test runner з name + count
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

// ── Stub action functions для тестів ──
static ActionStatus stub_action_a(ActionContext&) { return ActionStatus::OK; }
static ActionStatus stub_action_b(ActionContext&) { return ActionStatus::OK; }
static ActionStatus stub_cond_a(ActionContext&) { return ActionStatus::OK; }

// ── Singleton ──

TEST(singleton_returns_stable_reference) {
    auto& r1 = ActionRegistry::instance();
    auto& r2 = ActionRegistry::instance();
    assert(&r1 == &r2);
    r1.clear_for_tests();
}

// ── Action registration ──

TEST(register_action_success) {
    auto& reg = ActionRegistry::instance();
    reg.clear_for_tests();
    ActionDescriptor d{djb2_hash16("test_a"), "test_a", &stub_action_a, 0, 0};
    assert(reg.register_action(d));
    assert(reg.action_count() == 1);
}

TEST(register_action_duplicate_hash_rejected) {
    auto& reg = ActionRegistry::instance();
    reg.clear_for_tests();
    ActionDescriptor d{djb2_hash16("test_a"), "test_a", &stub_action_a, 0, 0};
    assert(reg.register_action(d));
    // Second registration з same hash should fail
    assert(!reg.register_action(d));
    assert(reg.action_count() == 1);
}

TEST(register_action_hash_name_mismatch_rejected) {
    auto& reg = ActionRegistry::instance();
    reg.clear_for_tests();
    // Manually-corrupted descriptor: hash doesn't match djb2(name)
    ActionDescriptor d{0xDEAD, "test_a", &stub_action_a, 0, 0};
    assert(!reg.register_action(d));
    assert(reg.action_count() == 0);
}

TEST(register_action_collision_brute_forced_pair) {
    // 'aa' і 'gjq' both djb2_hash16 to 0x7727 (verified у Step 2a B1 fix).
    auto& reg = ActionRegistry::instance();
    reg.clear_for_tests();
    ActionDescriptor a{djb2_hash16("aa"), "aa", &stub_action_a, 0, 0};
    ActionDescriptor b{djb2_hash16("gjq"), "gjq", &stub_action_b, 0, 0};
    assert(a.hash == b.hash);  // confirm collision
    assert(reg.register_action(a));
    assert(!reg.register_action(b));  // collision rejected
    assert(reg.action_count() == 1);
}

// ── Action lookup ──

TEST(find_action_returns_descriptor) {
    auto& reg = ActionRegistry::instance();
    reg.clear_for_tests();
    ActionDescriptor d{djb2_hash16("foo"), "foo", &stub_action_a, 1, 3};
    reg.register_action(d);
    const ActionDescriptor* found = reg.find_action(djb2_hash16("foo"));
    assert(found != nullptr);
    assert(found->fn == &stub_action_a);
    assert(found->param_min == 1);
    assert(found->param_max == 3);
}

TEST(find_action_unknown_returns_nullptr) {
    auto& reg = ActionRegistry::instance();
    reg.clear_for_tests();
    assert(reg.find_action(0xBEEF) == nullptr);
}

// ── Condition registration (separate namespace) ──

TEST(register_condition_separate_from_action) {
    auto& reg = ActionRegistry::instance();
    reg.clear_for_tests();
    ActionDescriptor a{djb2_hash16("foo"), "foo", &stub_action_a, 0, 0};
    ActionDescriptor c{djb2_hash16("foo"), "foo", &stub_cond_a, 0, 0};
    // Same name "foo" allowed у both registries (different runtime methods)
    assert(reg.register_action(a));
    assert(reg.register_condition(c));
    assert(reg.action_count() == 1);
    assert(reg.condition_count() == 1);
    // Lookups стабільні: actions знаходять їхні, conditions свої
    assert(reg.find_action(djb2_hash16("foo"))->fn == &stub_action_a);
    assert(reg.find_condition(djb2_hash16("foo"))->fn == &stub_cond_a);
}

TEST(condition_collision_within_namespace_rejected) {
    auto& reg = ActionRegistry::instance();
    reg.clear_for_tests();
    ActionDescriptor a{djb2_hash16("aa"), "aa", &stub_cond_a, 0, 0};
    ActionDescriptor b{djb2_hash16("gjq"), "gjq", &stub_cond_a, 0, 0};
    assert(reg.register_condition(a));
    assert(!reg.register_condition(b));
    assert(reg.condition_count() == 1);
}

// ── Diagnostics ──

TEST(action_count_zero_after_clear) {
    auto& reg = ActionRegistry::instance();
    ActionDescriptor d{djb2_hash16("test_a"), "test_a", &stub_action_a, 0, 0};
    reg.register_action(d);
    reg.clear_for_tests();
    assert(reg.action_count() == 0);
    assert(reg.condition_count() == 0);
}

// ── djb2_hash16 invariants ──

TEST(djb2_hash16_constexpr_matches_runtime) {
    constexpr auto h1 = djb2_hash16("log");
    auto h2 = djb2_hash16("log");
    assert(h1 == h2);
    assert(h1 == 0x8d07);  // golden value from known_actions.json
}

TEST(djb2_hash16_known_collision_pair) {
    // Verified empirically у Step 2a B1: 'aa' ↔ 'gjq' both 0x7727.
    assert(djb2_hash16("aa") == djb2_hash16("gjq"));
    assert(djb2_hash16("aa") == 0x7727);
}

// ── Main ──

int main() {
    std::printf("Running ActionRegistry host tests:\n");
    // All tests run via static initializers above (TEST macro)
    std::printf("\n--- %d passed, %d failed ---\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
