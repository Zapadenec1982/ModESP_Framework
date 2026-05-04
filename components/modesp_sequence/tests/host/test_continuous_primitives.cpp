/**
 * @file test_continuous_primitives.cpp
 * @brief Host tests for PidController, HysteresisController, RampProfile.
 *
 * Each primitive tested standalone (no engine, no scenario):
 *   - Construct via factory (heap)
 *   - Build params з StringPool helper
 *   - on_activate з mock SharedState
 *   - Drive on_tick repeatedly, verify output trajectory
 *   - on_deactivate, delete
 *
 * Mirrors test_builtin_actions.cpp infrastructure (StringPool, ActionContext
 * builder). Uses real SharedState із tests/host/shared_state_host.cpp.
 */

#include "modesp/sequence/continuous_primitives.h"
#include "modesp/sequence/continuous_behavior.h"
#include "modesp/sequence/action_param.h"
#include "modesp/shared_state.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>

using namespace modesp::sequence;
using modesp::SharedState;
using modesp::StateKey;

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

// ── String pool builder (matches test_builtin_actions pattern) ──

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

static ActionParam param_str(const char* name, uint16_t off) {
    ActionParam p{};
    p.key_hash = djb2_hash16(name);
    p.type = static_cast<uint8_t>(ParamType::STR);
    p.v.s_idx = off;
    return p;
}

static ActionParam param_f32(const char* name, float v) {
    ActionParam p{};
    p.key_hash = djb2_hash16(name);
    p.type = static_cast<uint8_t>(ParamType::F32);
    p.v.f = v;
    return p;
}

static ActionParam param_i32(const char* name, int32_t v) {
    ActionParam p{};
    p.key_hash = djb2_hash16(name);
    p.type = static_cast<uint8_t>(ParamType::I32);
    p.v.i = v;
    return p;
}

static ActionContext make_ctx(SharedState* state, const char* pool, uint16_t pool_size) {
    ActionContext ctx{};
    ctx.state = state;
    ctx.string_pool = pool;
    ctx.string_pool_size = pool_size;
    return ctx;
}

// Helpers для reading SharedState
static float get_float(SharedState& ss, const char* key) {
    auto opt = ss.get(StateKey(key));
    if (!opt.has_value()) return NAN;
    if (auto pf = etl::get_if<float>(&*opt)) return *pf;
    if (auto pi = etl::get_if<int32_t>(&*opt)) return static_cast<float>(*pi);
    return NAN;
}

static bool get_bool(SharedState& ss, const char* key) {
    auto opt = ss.get(StateKey(key));
    if (!opt.has_value()) return false;
    if (auto pb = etl::get_if<bool>(&*opt)) return *pb;
    return false;
}

// ── Registry registration ────────────────────────────────────────────

TEST(register_primitives_registers_three) {
    auto& reg = ContinuousRegistry::instance();
    reg.clear_for_tests();
    bool ok = primitives::register_primitives();
    assert(ok);
    assert(reg.count() == primitives::PRIMITIVE_COUNT);
    // All findable
    assert(reg.find(djb2_hash16("pid")) != nullptr);
    assert(reg.find(djb2_hash16("hysteresis")) != nullptr);
    assert(reg.find(djb2_hash16("ramp")) != nullptr);
}

TEST(register_primitives_idempotent) {
    auto& reg = ContinuousRegistry::instance();
    reg.clear_for_tests();
    primitives::register_primitives();
    // Second call collisions, returns false але state stays consistent
    bool ok2 = primitives::register_primitives();
    assert(!ok2);
    assert(reg.count() == primitives::PRIMITIVE_COUNT);
}

TEST(factories_return_heap_allocated_distinct_instances) {
    // Each call returns NEW instance (per documented contract). Caller deletes.
    auto* a = primitives::pid_factory();
    auto* b = primitives::pid_factory();
    assert(a != nullptr && b != nullptr);
    assert(a != b);  // distinct instances — state не shared
    delete a;
    delete b;
}

// ── PID Controller ──────────────────────────────────────────────────

TEST(pid_proportional_only_drives_output_toward_setpoint) {
    SharedState ss;
    ss.set("ctrl.input", 18.0f);  // measured 18°C; setpoint 22°C → error 4°C → output positive
    StringPool pool;
    auto ki = pool.add("ctrl.input");
    auto ko = pool.add("ctrl.output");

    auto* pid = primitives::pid_factory();
    ActionParam p[] = {
        param_str("input_key",  ki),
        param_str("output_key", ko),
        param_f32("setpoint", 22.0f),
        param_f32("kp", 5.0f),
        param_f32("ki", 0.0f),
        param_f32("kd", 0.0f),
        param_f32("out_min", 0.0f),
        param_f32("out_max", 100.0f),
    };
    auto ctx = make_ctx(&ss, pool.buf, pool.size);
    pid->on_activate(p, 8, pool.buf, ctx);
    pid->on_tick(10, ctx);

    // P-only: output = kp * error = 5.0 * 4.0 = 20.0
    float out = get_float(ss, "ctrl.output");
    assert(std::fabs(out - 20.0f) < 0.01f);

    pid->on_deactivate(ctx);
    delete pid;
}

TEST(pid_output_clamped_to_max) {
    SharedState ss;
    ss.set("ctrl.input", 0.0f);  // huge error
    StringPool pool;
    auto ki = pool.add("ctrl.input");
    auto ko = pool.add("ctrl.output");

    auto* pid = primitives::pid_factory();
    ActionParam p[] = {
        param_str("input_key",  ki),
        param_str("output_key", ko),
        param_f32("setpoint", 100.0f),
        param_f32("kp", 50.0f),  // would produce 5000 без clamp
        param_f32("ki", 0.0f),
        param_f32("kd", 0.0f),
        param_f32("out_min", 0.0f),
        param_f32("out_max", 100.0f),
    };
    auto ctx = make_ctx(&ss, pool.buf, pool.size);
    pid->on_activate(p, 8, pool.buf, ctx);
    pid->on_tick(10, ctx);

    float out = get_float(ss, "ctrl.output");
    assert(out == 100.0f);

    delete pid;
}

TEST(pid_integral_accumulates_and_anti_windup_works) {
    SharedState ss;
    ss.set("ctrl.input", 18.0f);  // constant 4°C below setpoint
    StringPool pool;
    auto ki_off = pool.add("ctrl.input");
    auto ko_off = pool.add("ctrl.output");

    auto* pid = primitives::pid_factory();
    ActionParam p[] = {
        param_str("input_key",  ki_off),
        param_str("output_key", ko_off),
        param_f32("setpoint", 22.0f),
        param_f32("kp", 1.0f),
        param_f32("ki", 1.0f),  // 1 unit per sec per °C error
        param_f32("kd", 0.0f),
        param_f32("out_min", 0.0f),
        param_f32("out_max", 50.0f),
    };
    auto ctx = make_ctx(&ss, pool.buf, pool.size);
    pid->on_activate(p, 8, pool.buf, ctx);

    // Tick 100 times із 100 ms = 10 sec total
    // Error = 4°C constant. Без anti-windup: ki*integral = 1*4*10 = 40 →
    // output = kp*4 + 40 = 44 (still < 50, no saturation)
    for (int i = 0; i < 100; ++i) pid->on_tick(100, ctx);
    float out = get_float(ss, "ctrl.output");
    assert(out > 40.0f && out < 50.0f);  // somewhere у the middle of expected range

    // Now drive output until it saturates: continue ticking
    for (int i = 0; i < 100; ++i) pid->on_tick(100, ctx);
    out = get_float(ss, "ctrl.output");
    assert(out == 50.0f);  // saturated

    // Anti-windup: integral shouldn't grow unbounded. If we change input до above
    // setpoint, output should respond promptly (not stuck high through integral).
    ss.set("ctrl.input", 30.0f);  // 8°C ABOVE setpoint → error = -8
    pid->on_tick(100, ctx);
    out = get_float(ss, "ctrl.output");
    assert(out < 50.0f);  // lowered (would still be 50 if windup uncontrolled)

    delete pid;
}

// ── Hysteresis Controller ───────────────────────────────────────────

TEST(hysteresis_cooling_mode_basic) {
    SharedState ss;
    StringPool pool;
    auto ki = pool.add("ctrl.temp");
    auto ko = pool.add("ctrl.cool");

    auto* hys = primitives::hysteresis_factory();
    ActionParam p[] = {
        param_str("input_key",  ki),
        param_str("output_key", ko),
        param_f32("setpoint", 20.0f),
        param_f32("deadband", 1.0f),  // upper=21, lower=19
        param_i32("mode", 0),  // cooling
    };
    auto ctx = make_ctx(&ss, pool.buf, pool.size);
    hys->on_activate(p, 5, pool.buf, ctx);

    // Below lower → off
    ss.set("ctrl.temp", 18.0f);
    hys->on_tick(10, ctx);
    assert(get_bool(ss, "ctrl.cool") == false);

    // Within deadband → hold
    ss.set("ctrl.temp", 20.0f);
    hys->on_tick(10, ctx);
    assert(get_bool(ss, "ctrl.cool") == false);  // still off

    // Above upper → on
    ss.set("ctrl.temp", 22.0f);
    hys->on_tick(10, ctx);
    assert(get_bool(ss, "ctrl.cool") == true);

    // Drop back to deadband — should HOLD on (хit cool until below lower)
    ss.set("ctrl.temp", 20.0f);
    hys->on_tick(10, ctx);
    assert(get_bool(ss, "ctrl.cool") == true);  // still on (within deadband)

    // Below lower → off
    ss.set("ctrl.temp", 18.5f);
    hys->on_tick(10, ctx);
    assert(get_bool(ss, "ctrl.cool") == false);

    hys->on_deactivate(ctx);
    delete hys;
}

TEST(hysteresis_heating_mode_inverse) {
    SharedState ss;
    StringPool pool;
    auto ki = pool.add("ctrl.temp");
    auto ko = pool.add("ctrl.heat");

    auto* hys = primitives::hysteresis_factory();
    ActionParam p[] = {
        param_str("input_key",  ki),
        param_str("output_key", ko),
        param_f32("setpoint", 20.0f),
        param_f32("deadband", 1.0f),
        param_i32("mode", 1),  // heating
    };
    auto ctx = make_ctx(&ss, pool.buf, pool.size);
    hys->on_activate(p, 5, pool.buf, ctx);

    ss.set("ctrl.temp", 22.0f);  // above upper → off
    hys->on_tick(10, ctx);
    assert(get_bool(ss, "ctrl.heat") == false);

    ss.set("ctrl.temp", 18.0f);  // below lower → on
    hys->on_tick(10, ctx);
    assert(get_bool(ss, "ctrl.heat") == true);

    delete hys;
}

TEST(hysteresis_deactivate_forces_output_off) {
    SharedState ss;
    StringPool pool;
    auto ki = pool.add("ctrl.temp");
    auto ko = pool.add("ctrl.cool");

    auto* hys = primitives::hysteresis_factory();
    ActionParam p[] = {
        param_str("input_key",  ki),
        param_str("output_key", ko),
        param_f32("setpoint", 20.0f),
        param_f32("deadband", 1.0f),
        param_i32("mode", 0),
    };
    auto ctx = make_ctx(&ss, pool.buf, pool.size);
    hys->on_activate(p, 5, pool.buf, ctx);
    ss.set("ctrl.temp", 25.0f);
    hys->on_tick(10, ctx);
    assert(get_bool(ss, "ctrl.cool") == true);

    hys->on_deactivate(ctx);
    // Safety: output forced off на deactivate
    assert(get_bool(ss, "ctrl.cool") == false);

    delete hys;
}

// ── Ramp Profile ────────────────────────────────────────────────────

TEST(ramp_linear_interpolation) {
    SharedState ss;
    StringPool pool;
    auto ko = pool.add("ramp.value");

    auto* ramp = primitives::ramp_factory();
    ActionParam p[] = {
        param_str("output_key",  ko),
        param_f32("start_value", 10.0f),
        param_f32("end_value",   30.0f),
        param_i32("duration_ms", 1000),  // 1 second
    };
    auto ctx = make_ctx(&ss, pool.buf, pool.size);
    ramp->on_activate(p, 4, pool.buf, ctx);

    // Initial: written by on_activate
    assert(std::fabs(get_float(ss, "ramp.value") - 10.0f) < 0.001f);

    // Quarter-way: 250ms / 1000ms = 0.25 → value = 10 + 20*0.25 = 15
    ramp->on_tick(250, ctx);
    assert(std::fabs(get_float(ss, "ramp.value") - 15.0f) < 0.001f);

    // Half-way: cumulative 500ms → value = 20
    ramp->on_tick(250, ctx);
    assert(std::fabs(get_float(ss, "ramp.value") - 20.0f) < 0.001f);

    // Done: cumulative 1000ms → value = 30 (end)
    ramp->on_tick(500, ctx);
    assert(std::fabs(get_float(ss, "ramp.value") - 30.0f) < 0.001f);

    // Past duration: holds at end_value
    ramp->on_tick(500, ctx);
    assert(std::fabs(get_float(ss, "ramp.value") - 30.0f) < 0.001f);

    ramp->on_deactivate(ctx);
    delete ramp;
}

TEST(ramp_negative_direction_works) {
    // Cooldown-style ramp: 30 → 10 over 1 sec
    SharedState ss;
    StringPool pool;
    auto ko = pool.add("ramp.value");

    auto* ramp = primitives::ramp_factory();
    ActionParam p[] = {
        param_str("output_key",  ko),
        param_f32("start_value", 30.0f),
        param_f32("end_value",   10.0f),
        param_i32("duration_ms", 1000),
    };
    auto ctx = make_ctx(&ss, pool.buf, pool.size);
    ramp->on_activate(p, 4, pool.buf, ctx);

    ramp->on_tick(500, ctx);  // half-way → 20
    assert(std::fabs(get_float(ss, "ramp.value") - 20.0f) < 0.001f);

    ramp->on_tick(500, ctx);  // done → 10
    assert(std::fabs(get_float(ss, "ramp.value") - 10.0f) < 0.001f);

    delete ramp;
}

TEST(ramp_re_activate_resets_elapsed) {
    SharedState ss;
    StringPool pool;
    auto ko = pool.add("ramp.value");

    auto* ramp = primitives::ramp_factory();
    ActionParam p[] = {
        param_str("output_key",  ko),
        param_f32("start_value", 0.0f),
        param_f32("end_value",   100.0f),
        param_i32("duration_ms", 1000),
    };
    auto ctx = make_ctx(&ss, pool.buf, pool.size);
    ramp->on_activate(p, 4, pool.buf, ctx);
    ramp->on_tick(500, ctx);
    assert(std::fabs(get_float(ss, "ramp.value") - 50.0f) < 0.001f);

    // Re-activate: elapsed should reset to 0; output back at start_value
    ramp->on_activate(p, 4, pool.buf, ctx);
    assert(std::fabs(get_float(ss, "ramp.value") - 0.0f) < 0.001f);

    ramp->on_tick(250, ctx);
    assert(std::fabs(get_float(ss, "ramp.value") - 25.0f) < 0.001f);

    delete ramp;
}

TEST(ramp_zero_duration_clamped_to_one_ms) {
    SharedState ss;
    StringPool pool;
    auto ko = pool.add("ramp.value");

    auto* ramp = primitives::ramp_factory();
    ActionParam p[] = {
        param_str("output_key",  ko),
        param_f32("start_value", 0.0f),
        param_f32("end_value",   100.0f),
        param_i32("duration_ms", 0),  // invalid; clamped to 1
    };
    auto ctx = make_ctx(&ss, pool.buf, pool.size);
    ramp->on_activate(p, 4, pool.buf, ctx);

    // First tick of 1ms → at end immediately
    ramp->on_tick(1, ctx);
    assert(std::fabs(get_float(ss, "ramp.value") - 100.0f) < 0.001f);

    delete ramp;
}

// ── Main ─────────────────────────────────────────────────────────────

int main() {
    std::printf("Running continuous_primitives host tests:\n");
    std::printf("\n--- %d passed, %d failed ---\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
