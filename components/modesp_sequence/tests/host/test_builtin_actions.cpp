/**
 * @file test_builtin_actions.cpp
 * @brief Host unit tests для built-in actions і conditions (Step 7).
 *
 * Plain assert()-based; matches test_action_registry.cpp pattern.
 *
 * Coverage:
 *   - register_builtins() registers 3 actions + 10 conditions, idempotent on second call
 *   - Each registered descriptor has hash matching djb2(name) і correct param_min/max
 *   - log: validates param presence/type, returns OK on valid input
 *   - set_state: writes I32/F32/BOOL to SharedState, validates type code
 *   - wait_ms: PENDING until phase_elapsed_ms reached, OK після
 *   - time_elapsed_ms: false → FAILED_RECOVERABLE, true → OK
 *   - state_key_eq/ne/lt/gt/le/ge: comparison semantics over I32/F32/BOOL
 *   - state_key_in_range: bounds check
 *   - state_key_changed: missing key і present-key paths (placeholder until Step 14)
 *   - time_of_day_eq: param validation (negative/out-of-range)
 *
 * Tests requiring SharedState use a real local instance (mocked FreeRTOS
 * semaphores via freertos_mock.h). String params are encoded into a local
 * "string pool" buffer matching the wire format (u8 len-prefixed).
 */

#include "modesp/sequence/builtin_actions.h"
#include "modesp/sequence/action_registry.h"
#include "modesp/sequence/action_param.h"
#include "modesp/shared_state.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>

using namespace modesp::sequence;
using modesp::SharedState;
using modesp::StateKey;
using modesp::StateValue;

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

// ── Helpers ─────────────────────────────────────────────────────────────

/// Build a string pool у wire format: каждый entry має u8 length prefix
/// followed by raw bytes. Returns offset of newly-appended entry.
struct StringPool {
    char buf[256];
    uint16_t size = 0;

    uint16_t add(const char* s) {
        uint16_t off = size;
        uint8_t len = static_cast<uint8_t>(std::strlen(s));
        assert(size + 1u + len <= sizeof(buf));
        buf[size++] = static_cast<char>(len);
        std::memcpy(&buf[size], s, len);
        size += len;
        return off;
    }
};

/// Construct ActionContext з provided params, default-initialising irrelevant fields.
static ActionContext make_ctx(SharedState* state,
                              const ActionParam* params, uint8_t n,
                              const char* string_pool, uint16_t pool_size,
                              uint32_t phase_elapsed_ms = 0) {
    ActionContext ctx{};
    ctx.state = state;
    ctx.params = params;
    ctx.param_count = n;
    ctx.string_pool = string_pool;
    ctx.string_pool_size = pool_size;
    ctx.scenario_elapsed_ms = phase_elapsed_ms;
    ctx.phase_elapsed_ms = phase_elapsed_ms;
    ctx.phase_idx = 0;
    ctx.handle = 1;
    ctx.track = 0;
    ctx.recipe_name = "test_recipe";
    ctx.track_name = "main";
    return ctx;
}

static ActionParam param_i32(const char* name, int32_t v) {
    ActionParam p{};
    p.key_hash = djb2_hash16(name);
    p.type = static_cast<uint8_t>(ParamType::I32);
    p.flags = 0;
    p.v.i = v;
    return p;
}

static ActionParam param_f32(const char* name, float v) {
    ActionParam p{};
    p.key_hash = djb2_hash16(name);
    p.type = static_cast<uint8_t>(ParamType::F32);
    p.flags = 0;
    p.v.f = v;
    return p;
}

static ActionParam param_bool(const char* name, bool v) {
    ActionParam p{};
    p.key_hash = djb2_hash16(name);
    p.type = static_cast<uint8_t>(ParamType::BOOL);
    p.flags = 0;
    p.v.b = v;
    return p;
}

static ActionParam param_str(const char* name, uint16_t pool_offset) {
    ActionParam p{};
    p.key_hash = djb2_hash16(name);
    p.type = static_cast<uint8_t>(ParamType::STR);
    p.flags = 0;
    p.v.s_idx = pool_offset;
    return p;
}

/// Look up registered action descriptor and invoke its function with ctx.
static ActionStatus invoke_action(const char* name, ActionContext& ctx) {
    auto* d = ActionRegistry::instance().find_action(djb2_hash16(name));
    assert(d != nullptr && "action not registered");
    return d->fn(ctx);
}

static ActionStatus invoke_cond(const char* name, ActionContext& ctx) {
    auto* d = ActionRegistry::instance().find_condition(djb2_hash16(name));
    assert(d != nullptr && "condition not registered");
    return d->fn(ctx);
}

// Reset registry і re-register builtins. Each test starts clean.
static void setup() {
    auto& reg = ActionRegistry::instance();
    reg.clear_for_tests();
    bool ok = builtins::register_builtins();
    assert(ok);
}

// ── Registration ────────────────────────────────────────────────────────

TEST(register_builtins_registers_expected_counts) {
    setup();
    auto& reg = ActionRegistry::instance();
    assert(reg.action_count() == builtins::BUILTIN_ACTION_COUNT);
    assert(reg.condition_count() == builtins::BUILTIN_CONDITION_COUNT);
}

TEST(register_builtins_idempotent_second_call_returns_false) {
    setup();
    // Second call: all registrations collide → register_action returns false → ok&=false.
    bool ok2 = builtins::register_builtins();
    assert(!ok2);
    // Counts unchanged
    auto& reg = ActionRegistry::instance();
    assert(reg.action_count() == builtins::BUILTIN_ACTION_COUNT);
    assert(reg.condition_count() == builtins::BUILTIN_CONDITION_COUNT);
}

TEST(builtin_actions_have_consistent_param_specs) {
    setup();
    auto& reg = ActionRegistry::instance();
    auto* log_d = reg.find_action(djb2_hash16("log"));
    auto* set_d = reg.find_action(djb2_hash16("set_state"));
    auto* wait_d = reg.find_action(djb2_hash16("wait_ms"));
    assert(log_d && set_d && wait_d);
    assert(log_d->param_min == 1 && log_d->param_max == 1);
    assert(set_d->param_min == 3 && set_d->param_max == 3);
    assert(wait_d->param_min == 1 && wait_d->param_max == 1);
}

// ── log action ──────────────────────────────────────────────────────────

TEST(log_with_valid_string_returns_ok) {
    setup();
    StringPool pool;
    uint16_t off = pool.add("hello");
    ActionParam p[] = { param_str("msg", off) };
    auto ctx = make_ctx(nullptr, p, 1, pool.buf, pool.size);
    assert(invoke_action("log", ctx) == ActionStatus::OK);
}

TEST(log_missing_param_returns_failed_abort) {
    setup();
    auto ctx = make_ctx(nullptr, nullptr, 0, nullptr, 0);
    assert(invoke_action("log", ctx) == ActionStatus::FAILED_ABORT);
}

TEST(log_wrong_param_type_returns_failed_abort) {
    setup();
    ActionParam p[] = { param_i32("msg", 42) };  // wrong type
    auto ctx = make_ctx(nullptr, p, 1, nullptr, 0);
    assert(invoke_action("log", ctx) == ActionStatus::FAILED_ABORT);
}

// ── set_state action ────────────────────────────────────────────────────

TEST(set_state_writes_i32_to_shared_state) {
    setup();
    SharedState ss;
    StringPool pool;
    uint16_t key_off = pool.add("test.counter");
    ActionParam p[] = {
        param_str("key", key_off),
        param_i32("type", 0),  // I32
        param_i32("value", 42),
    };
    auto ctx = make_ctx(&ss, p, 3, pool.buf, pool.size);
    assert(invoke_action("set_state", ctx) == ActionStatus::OK);
    auto opt = ss.get(StateKey("test.counter"));
    assert(opt.has_value());
    auto* iv = etl::get_if<int32_t>(&*opt);
    assert(iv && *iv == 42);
}

TEST(set_state_writes_f32_to_shared_state) {
    setup();
    SharedState ss;
    StringPool pool;
    uint16_t key_off = pool.add("test.temp");
    ActionParam p[] = {
        param_str("key", key_off),
        param_i32("type", 1),  // F32
        param_f32("value", 23.5f),
    };
    auto ctx = make_ctx(&ss, p, 3, pool.buf, pool.size);
    assert(invoke_action("set_state", ctx) == ActionStatus::OK);
    auto opt = ss.get(StateKey("test.temp"));
    assert(opt.has_value());
    auto* fv = etl::get_if<float>(&*opt);
    assert(fv && *fv == 23.5f);
}

TEST(set_state_writes_bool_to_shared_state) {
    setup();
    SharedState ss;
    StringPool pool;
    uint16_t key_off = pool.add("test.flag");
    ActionParam p[] = {
        param_str("key", key_off),
        param_i32("type", 2),  // BOOL
        param_bool("value", true),
    };
    auto ctx = make_ctx(&ss, p, 3, pool.buf, pool.size);
    assert(invoke_action("set_state", ctx) == ActionStatus::OK);
    auto opt = ss.get(StateKey("test.flag"));
    assert(opt.has_value());
    auto* bv = etl::get_if<bool>(&*opt);
    assert(bv && *bv == true);
}

TEST(set_state_invalid_type_code_returns_failed_abort) {
    setup();
    SharedState ss;
    StringPool pool;
    uint16_t key_off = pool.add("test.x");
    ActionParam p[] = {
        param_str("key", key_off),
        param_i32("type", 99),  // bogus
        param_i32("value", 0),
    };
    auto ctx = make_ctx(&ss, p, 3, pool.buf, pool.size);
    assert(invoke_action("set_state", ctx) == ActionStatus::FAILED_ABORT);
}

TEST(set_state_null_state_returns_failed_abort) {
    setup();
    StringPool pool;
    uint16_t key_off = pool.add("test.x");
    ActionParam p[] = {
        param_str("key", key_off),
        param_i32("type", 0),
        param_i32("value", 1),
    };
    auto ctx = make_ctx(nullptr, p, 3, pool.buf, pool.size);
    assert(invoke_action("set_state", ctx) == ActionStatus::FAILED_ABORT);
}

// ── wait_ms action ──────────────────────────────────────────────────────

TEST(wait_ms_pending_before_threshold) {
    setup();
    ActionParam p[] = { param_i32("ms", 500) };
    auto ctx = make_ctx(nullptr, p, 1, nullptr, 0, /*phase_elapsed_ms=*/100);
    assert(invoke_action("wait_ms", ctx) == ActionStatus::PENDING);
}

TEST(wait_ms_ok_at_or_after_threshold) {
    setup();
    ActionParam p[] = { param_i32("ms", 500) };
    auto ctx500 = make_ctx(nullptr, p, 1, nullptr, 0, /*phase_elapsed_ms=*/500);
    assert(invoke_action("wait_ms", ctx500) == ActionStatus::OK);
    auto ctx600 = make_ctx(nullptr, p, 1, nullptr, 0, /*phase_elapsed_ms=*/600);
    assert(invoke_action("wait_ms", ctx600) == ActionStatus::OK);
}

TEST(wait_ms_negative_threshold_returns_failed_abort) {
    setup();
    ActionParam p[] = { param_i32("ms", -1) };
    auto ctx = make_ctx(nullptr, p, 1, nullptr, 0);
    assert(invoke_action("wait_ms", ctx) == ActionStatus::FAILED_ABORT);
}

// ── time_elapsed_ms condition ───────────────────────────────────────────

TEST(time_elapsed_ms_false_before_threshold) {
    setup();
    ActionParam p[] = { param_i32("ms", 1000) };
    auto ctx = make_ctx(nullptr, p, 1, nullptr, 0, /*phase_elapsed_ms=*/500);
    assert(invoke_cond("time_elapsed_ms", ctx) == ActionStatus::FAILED_RECOVERABLE);
}

TEST(time_elapsed_ms_true_at_threshold) {
    setup();
    ActionParam p[] = { param_i32("ms", 1000) };
    auto ctx = make_ctx(nullptr, p, 1, nullptr, 0, /*phase_elapsed_ms=*/1000);
    assert(invoke_cond("time_elapsed_ms", ctx) == ActionStatus::OK);
}

// ── state_key_eq/ne/lt/gt/le/ge conditions ──────────────────────────────

TEST(state_key_eq_matches_i32) {
    setup();
    SharedState ss;
    ss.set("a.x", static_cast<int32_t>(5));
    StringPool pool;
    uint16_t key_off = pool.add("a.x");
    ActionParam p[] = { param_str("key", key_off), param_i32("value", 5) };
    auto ctx = make_ctx(&ss, p, 2, pool.buf, pool.size);
    assert(invoke_cond("state_key_eq", ctx) == ActionStatus::OK);
}

TEST(state_key_eq_mismatch_returns_failed_recoverable) {
    setup();
    SharedState ss;
    ss.set("a.x", static_cast<int32_t>(5));
    StringPool pool;
    uint16_t key_off = pool.add("a.x");
    ActionParam p[] = { param_str("key", key_off), param_i32("value", 6) };
    auto ctx = make_ctx(&ss, p, 2, pool.buf, pool.size);
    assert(invoke_cond("state_key_eq", ctx) == ActionStatus::FAILED_RECOVERABLE);
}

TEST(state_key_ne_inverse_of_eq) {
    setup();
    SharedState ss;
    ss.set("a.x", static_cast<int32_t>(5));
    StringPool pool;
    uint16_t key_off = pool.add("a.x");
    ActionParam p_diff[] = { param_str("key", key_off), param_i32("value", 99) };
    auto ctx_diff = make_ctx(&ss, p_diff, 2, pool.buf, pool.size);
    assert(invoke_cond("state_key_ne", ctx_diff) == ActionStatus::OK);

    ActionParam p_same[] = { param_str("key", key_off), param_i32("value", 5) };
    auto ctx_same = make_ctx(&ss, p_same, 2, pool.buf, pool.size);
    assert(invoke_cond("state_key_ne", ctx_same) == ActionStatus::FAILED_RECOVERABLE);
}

TEST(state_key_lt_gt_le_ge_semantics) {
    setup();
    SharedState ss;
    ss.set("a.x", static_cast<int32_t>(5));
    StringPool pool;
    uint16_t key_off = pool.add("a.x");

    auto cond = [&](const char* name, int32_t v) {
        ActionParam p[] = { param_str("key", key_off), param_i32("value", v) };
        auto ctx = make_ctx(&ss, p, 2, pool.buf, pool.size);
        return invoke_cond(name, ctx);
    };

    // x=5 vs 10
    assert(cond("state_key_lt", 10) == ActionStatus::OK);
    assert(cond("state_key_gt", 10) == ActionStatus::FAILED_RECOVERABLE);
    assert(cond("state_key_le", 10) == ActionStatus::OK);
    assert(cond("state_key_ge", 10) == ActionStatus::FAILED_RECOVERABLE);
    // x=5 vs 5 (equality boundary)
    assert(cond("state_key_lt", 5) == ActionStatus::FAILED_RECOVERABLE);
    assert(cond("state_key_le", 5) == ActionStatus::OK);
    assert(cond("state_key_ge", 5) == ActionStatus::OK);
    assert(cond("state_key_gt", 5) == ActionStatus::FAILED_RECOVERABLE);
}

TEST(state_key_eq_works_for_float) {
    setup();
    SharedState ss;
    ss.set("a.t", 23.5f);
    StringPool pool;
    uint16_t key_off = pool.add("a.t");
    ActionParam p[] = { param_str("key", key_off), param_f32("value", 23.5f) };
    auto ctx = make_ctx(&ss, p, 2, pool.buf, pool.size);
    assert(invoke_cond("state_key_eq", ctx) == ActionStatus::OK);
}

// Regression: state_key_eq must compare strings. Real-hardware HIL revealed
// що recipes reading engine's string mirror keys (e.g. <recipe>.main_phase_name)
// silently failed because compare_state_to_param treated string-vs-string as
// type mismatch (INT32_MIN sentinel) → FAILED_ABORT → condition false forever.
TEST(state_key_eq_works_for_string) {
    setup();
    SharedState ss;
    // Engine writes mirror keys через SharedState::set(const char*) overload
    // which stores etl::string<32> у the variant. Mirror this.
    ss.set("a.phase", "phase_c");
    StringPool pool;
    uint16_t key_off = pool.add("a.phase");
    uint16_t val_off = pool.add("phase_c");
    ActionParam p_match[] = {
        param_str("key", key_off),
        param_str("value", val_off),
    };
    auto ctx_match = make_ctx(&ss, p_match, 2, pool.buf, pool.size);
    assert(invoke_cond("state_key_eq", ctx_match) == ActionStatus::OK);

    // Different value should NOT match
    uint16_t val_diff = pool.add("phase_a");
    ActionParam p_diff[] = {
        param_str("key", key_off),
        param_str("value", val_diff),
    };
    auto ctx_diff = make_ctx(&ss, p_diff, 2, pool.buf, pool.size);
    assert(invoke_cond("state_key_eq", ctx_diff) == ActionStatus::FAILED_RECOVERABLE);
}

TEST(state_key_eq_works_for_bool) {
    setup();
    SharedState ss;
    ss.set("a.b", true);
    StringPool pool;
    uint16_t key_off = pool.add("a.b");
    ActionParam p[] = { param_str("key", key_off), param_bool("value", true) };
    auto ctx = make_ctx(&ss, p, 2, pool.buf, pool.size);
    assert(invoke_cond("state_key_eq", ctx) == ActionStatus::OK);

    ActionParam p_false[] = { param_str("key", key_off), param_bool("value", false) };
    auto ctx_false = make_ctx(&ss, p_false, 2, pool.buf, pool.size);
    assert(invoke_cond("state_key_eq", ctx_false) == ActionStatus::FAILED_RECOVERABLE);
}

TEST(state_key_eq_missing_key_returns_failed_recoverable) {
    setup();
    SharedState ss;  // empty
    StringPool pool;
    uint16_t key_off = pool.add("a.missing");
    ActionParam p[] = { param_str("key", key_off), param_i32("value", 1) };
    auto ctx = make_ctx(&ss, p, 2, pool.buf, pool.size);
    assert(invoke_cond("state_key_eq", ctx) == ActionStatus::FAILED_RECOVERABLE);
}

// ── state_key_in_range condition ────────────────────────────────────────

TEST(state_key_in_range_inside_bounds) {
    setup();
    SharedState ss;
    ss.set("a.x", static_cast<int32_t>(5));
    StringPool pool;
    uint16_t key_off = pool.add("a.x");
    ActionParam p[] = {
        param_str("key", key_off),
        param_i32("min", 0),
        param_i32("max", 10),
    };
    auto ctx = make_ctx(&ss, p, 3, pool.buf, pool.size);
    assert(invoke_cond("state_key_in_range", ctx) == ActionStatus::OK);
}

TEST(state_key_in_range_outside_bounds) {
    setup();
    SharedState ss;
    ss.set("a.x", static_cast<int32_t>(15));
    StringPool pool;
    uint16_t key_off = pool.add("a.x");
    ActionParam p[] = {
        param_str("key", key_off),
        param_i32("min", 0),
        param_i32("max", 10),
    };
    auto ctx = make_ctx(&ss, p, 3, pool.buf, pool.size);
    assert(invoke_cond("state_key_in_range", ctx) == ActionStatus::FAILED_RECOVERABLE);
}

TEST(state_key_in_range_at_boundary_inclusive) {
    setup();
    SharedState ss;
    ss.set("a.x", static_cast<int32_t>(10));  // = max
    StringPool pool;
    uint16_t key_off = pool.add("a.x");
    ActionParam p[] = {
        param_str("key", key_off),
        param_i32("min", 0),
        param_i32("max", 10),
    };
    auto ctx = make_ctx(&ss, p, 3, pool.buf, pool.size);
    assert(invoke_cond("state_key_in_range", ctx) == ActionStatus::OK);
}

// ── state_key_changed (placeholder в Step 7) ────────────────────────────
//
// Step 14 wires this до engine's edge-detection state. До того часу
// implementation повертає FAILED_RECOVERABLE незалежно від key state — safe
// "no edge detected" default. Тест fixates this behavior so Step 14 буде
// updating both impl AND test разом.

TEST(state_key_changed_returns_failed_recoverable_when_present) {
    setup();
    SharedState ss;
    ss.set("a.x", static_cast<int32_t>(1));
    StringPool pool;
    uint16_t key_off = pool.add("a.x");
    ActionParam p[] = { param_str("key", key_off) };
    auto ctx = make_ctx(&ss, p, 1, pool.buf, pool.size);
    assert(invoke_cond("state_key_changed", ctx) == ActionStatus::FAILED_RECOVERABLE);
}

TEST(state_key_changed_returns_failed_recoverable_when_missing) {
    setup();
    SharedState ss;  // empty
    StringPool pool;
    uint16_t key_off = pool.add("a.missing");
    ActionParam p[] = { param_str("key", key_off) };
    auto ctx = make_ctx(&ss, p, 1, pool.buf, pool.size);
    assert(invoke_cond("state_key_changed", ctx) == ActionStatus::FAILED_RECOVERABLE);
}

// ── time_of_day_eq (param validation only) ──────────────────────────────
//
// Wall-clock testing вимагає system-time mock; для Step 7 ми verifyуємо
// тільки param-validation paths. Functional matching test deferred до
// integration test stage (Step 16+).

TEST(time_of_day_eq_invalid_hour_returns_failed_abort) {
    setup();
    ActionParam p[] = { param_i32("hh", 24), param_i32("mm", 0) };
    auto ctx = make_ctx(nullptr, p, 2, nullptr, 0);
    assert(invoke_cond("time_of_day_eq", ctx) == ActionStatus::FAILED_ABORT);
}

TEST(time_of_day_eq_invalid_minute_returns_failed_abort) {
    setup();
    ActionParam p[] = { param_i32("hh", 12), param_i32("mm", 60) };
    auto ctx = make_ctx(nullptr, p, 2, nullptr, 0);
    assert(invoke_cond("time_of_day_eq", ctx) == ActionStatus::FAILED_ABORT);
}

TEST(time_of_day_eq_negative_hour_returns_failed_abort) {
    setup();
    ActionParam p[] = { param_i32("hh", -1), param_i32("mm", 30) };
    auto ctx = make_ctx(nullptr, p, 2, nullptr, 0);
    assert(invoke_cond("time_of_day_eq", ctx) == ActionStatus::FAILED_ABORT);
}

// ── Main ────────────────────────────────────────────────────────────────

int main() {
    std::printf("Running builtin_actions host tests:\n");
    std::printf("\n--- %d passed, %d failed ---\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
