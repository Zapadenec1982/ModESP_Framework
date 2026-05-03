/**
 * @file test_continuous_registry.cpp
 * @brief Host unit tests для ContinuousRegistry singleton.
 *
 * Plain assert()-based, паралельний test_action_registry.cpp pattern.
 *
 * Coverage:
 *   - Singleton stability
 *   - register_factory success / collision rejection
 *   - hash/name consistency check
 *   - find returns factory pointer
 *   - find unknown returns nullptr
 *   - create() invokes factory і returns behavior
 *   - clear_for_tests resets state
 *   - Null factory rejected
 */

#include "modesp/sequence/continuous_behavior.h"
#include "modesp/sequence/action_registry.h"  // for djb2_hash16
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

// ── Stub ContinuousBehavior implementations ──

class StubPid : public ContinuousBehavior {
public:
    void on_activate(const ActionParam*, uint8_t, const char*, ActionContext&) override {}
    void on_tick(uint32_t, ActionContext&) override {}
    void on_deactivate(ActionContext&) override {}
    uint16_t hash() const override { return djb2_hash16("stub_pid"); }
    const char* name() const override { return "stub_pid"; }
};

class StubMonitor : public ContinuousBehavior {
public:
    void on_activate(const ActionParam*, uint8_t, const char*, ActionContext&) override {}
    void on_tick(uint32_t, ActionContext&) override {}
    void on_deactivate(ActionContext&) override {}
    uint16_t hash() const override { return djb2_hash16("stub_monitor"); }
    const char* name() const override { return "stub_monitor"; }
};

static StubPid g_stub_pid;
static StubMonitor g_stub_monitor;

static ContinuousBehavior* factory_pid() { return &g_stub_pid; }
static ContinuousBehavior* factory_monitor() { return &g_stub_monitor; }

// ── Tests ──

TEST(singleton_stable) {
    auto& r1 = ContinuousRegistry::instance();
    auto& r2 = ContinuousRegistry::instance();
    assert(&r1 == &r2);
    r1.clear_for_tests();
}

TEST(register_factory_success) {
    auto& reg = ContinuousRegistry::instance();
    reg.clear_for_tests();
    assert(reg.register_factory(djb2_hash16("stub_pid"), "stub_pid", &factory_pid));
    assert(reg.count() == 1);
}

TEST(register_factory_duplicate_rejected) {
    auto& reg = ContinuousRegistry::instance();
    reg.clear_for_tests();
    reg.register_factory(djb2_hash16("stub_pid"), "stub_pid", &factory_pid);
    assert(!reg.register_factory(djb2_hash16("stub_pid"), "stub_pid", &factory_pid));
    assert(reg.count() == 1);
}

TEST(register_factory_hash_name_mismatch_rejected) {
    auto& reg = ContinuousRegistry::instance();
    reg.clear_for_tests();
    assert(!reg.register_factory(0xDEAD, "stub_pid", &factory_pid));
}

TEST(register_null_factory_rejected) {
    auto& reg = ContinuousRegistry::instance();
    reg.clear_for_tests();
    assert(!reg.register_factory(djb2_hash16("stub_pid"), "stub_pid", nullptr));
}

TEST(register_collision_rejected) {
    // 'aa' і 'gjq' both 0x7727 (verified empirically Step 2a B1).
    auto& reg = ContinuousRegistry::instance();
    reg.clear_for_tests();
    // Note: для simplicity ми reuse stub factories — names differ but hashes match
    // (the registry will reject second based on hash).
    assert(reg.register_factory(djb2_hash16("aa"), "aa", &factory_pid));
    assert(!reg.register_factory(djb2_hash16("gjq"), "gjq", &factory_monitor));
    assert(reg.count() == 1);
}

TEST(find_returns_factory) {
    auto& reg = ContinuousRegistry::instance();
    reg.clear_for_tests();
    reg.register_factory(djb2_hash16("stub_pid"), "stub_pid", &factory_pid);
    auto factory = reg.find(djb2_hash16("stub_pid"));
    assert(factory != nullptr);
    assert(factory == &factory_pid);
}

TEST(find_unknown_returns_nullptr) {
    auto& reg = ContinuousRegistry::instance();
    reg.clear_for_tests();
    assert(reg.find(0xBEEF) == nullptr);
}

TEST(create_invokes_factory) {
    auto& reg = ContinuousRegistry::instance();
    reg.clear_for_tests();
    reg.register_factory(djb2_hash16("stub_pid"), "stub_pid", &factory_pid);
    auto* behavior = reg.create(djb2_hash16("stub_pid"));
    assert(behavior == &g_stub_pid);
    // Verify it really IS the registered behavior
    assert(behavior->hash() == djb2_hash16("stub_pid"));
}

TEST(create_unknown_returns_nullptr) {
    auto& reg = ContinuousRegistry::instance();
    reg.clear_for_tests();
    assert(reg.create(0xBEEF) == nullptr);
}

TEST(register_two_distinct_factories) {
    auto& reg = ContinuousRegistry::instance();
    reg.clear_for_tests();
    assert(reg.register_factory(djb2_hash16("stub_pid"), "stub_pid", &factory_pid));
    assert(reg.register_factory(djb2_hash16("stub_monitor"), "stub_monitor", &factory_monitor));
    assert(reg.count() == 2);
    assert(reg.create(djb2_hash16("stub_pid")) == &g_stub_pid);
    assert(reg.create(djb2_hash16("stub_monitor")) == &g_stub_monitor);
}

TEST(clear_resets_count) {
    auto& reg = ContinuousRegistry::instance();
    reg.register_factory(djb2_hash16("stub_pid"), "stub_pid", &factory_pid);
    reg.clear_for_tests();
    assert(reg.count() == 0);
}

int main() {
    std::printf("Running ContinuousRegistry host tests:\n");
    std::printf("\n--- %d passed, %d failed ---\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
