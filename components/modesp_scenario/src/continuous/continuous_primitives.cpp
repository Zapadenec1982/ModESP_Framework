/**
 * @file continuous_primitives.cpp
 * @brief PID, hysteresis, ramp implementations.
 *
 * Each primitive reads one input from SharedState (configurable key) and
 * writes one output (configurable key). Per-tick algorithm runs у engine
 * update task — must be fast і non-blocking.
 *
 * Design notes:
 *  - Heap-allocated per instance (factory returns `new T()`); caller deletes
 *  - Parameters resolved у on_activate; mutated values (e.g. setpoint) NOT
 *    re-read each tick (stays из first activation of phase). Domain modules
 *    can reactivate behavior with new params if dynamic setpoint needed.
 *  - state.set() invocations are small overhead (~1 µs); fine для 100 Hz tick.
 */

#include "modesp/scenario/continuous_primitives.h"
#include "modesp/scenario/continuous_behavior.h"
#include "modesp/scenario/i_state_backend.h"
#include "modesp/types.h"

#include <cstring>
#include <algorithm>

namespace modesp::scenario {

// ─────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────

namespace {

/// Find param by name hash. Linear scan — params arrays small (3-8 typical).
const ActionParam* find_param(const ActionParam* params, uint8_t n,
                              uint16_t key_hash) {
    for (uint8_t i = 0; i < n; ++i) {
        if (params[i].key_hash == key_hash) return &params[i];
    }
    return nullptr;
}

/// Copy length-prefixed string from pool into bounded buffer. Returns true on
/// success. Mirrors helper у builtin_actions.cpp.
bool copy_string(const char* pool, uint16_t pool_size, uint16_t offset,
                 char* buf, size_t buf_size) {
    if (pool == nullptr || offset >= pool_size) return false;
    uint8_t len = static_cast<uint8_t>(pool[offset]);
    if (offset + 1u + len > pool_size) return false;
    if (static_cast<size_t>(len) + 1 > buf_size) return false;
    std::memcpy(buf, &pool[offset + 1], len);
    buf[len] = '\0';
    return true;
}

/// Read float (or int converted) from SharedState. Returns false if key missing
/// or wrong type. Used by all primitives для measured value reads.
bool read_state_float(IStateBackend* state, const char* key, float& out) {
    if (state == nullptr) return false;
    modesp::StateValue v; if (!state->get_raw(key, v)) return false;
    
    if (auto pf = etl::get_if<float>(&v))   { out = *pf; return true; }
    if (auto pi = etl::get_if<int32_t>(&v)) { out = static_cast<float>(*pi); return true; }
    return false;  // bool або string не usable as numeric input
}

/// Param resolver: prefer F32 type, fallback to I32 with cast. Returns true
/// on success. Used для setpoint, gains, deadband, etc.
bool param_to_float(const ActionParam* p, float& out) {
    if (p == nullptr) return false;
    if (p->type == static_cast<uint8_t>(ParamType::F32)) {
        out = p->v.f;
        return true;
    }
    if (p->type == static_cast<uint8_t>(ParamType::I32)) {
        out = static_cast<float>(p->v.i);
        return true;
    }
    return false;
}

bool param_to_int(const ActionParam* p, int32_t& out) {
    if (p == nullptr) return false;
    if (p->type == static_cast<uint8_t>(ParamType::I32)) {
        out = p->v.i;
        return true;
    }
    return false;
}

bool param_to_string(const ActionParam* p, const char* string_pool,
                     uint16_t pool_size, char* buf, size_t buf_size) {
    if (p == nullptr) return false;
    if (p->type != static_cast<uint8_t>(ParamType::STR)) return false;
    return copy_string(string_pool, pool_size, p->v.s_idx, buf, buf_size);
}

}  // anonymous namespace

// ─────────────────────────────────────────────────────────────────────
// PidController
// ─────────────────────────────────────────────────────────────────────

void PidController::on_activate(const ActionParam* params, uint8_t n,
                                const char* string_pool, ActionContext& ctx) {
    (void)ctx;
    uint16_t pool_size = 0;
    if (string_pool != nullptr) {
        // Pool size carried via ctx у normal usage; на direct calls without
        // ctx, caller must pass а sufficient pool_size hint. Read from ctx.
        pool_size = ctx.string_pool_size;
    }

    param_to_string(find_param(params, n, djb2_hash16("input_key")),
                    string_pool, pool_size, input_key_, sizeof(input_key_));
    param_to_string(find_param(params, n, djb2_hash16("output_key")),
                    string_pool, pool_size, output_key_, sizeof(output_key_));

    param_to_float(find_param(params, n, djb2_hash16("setpoint")), setpoint_);
    param_to_float(find_param(params, n, djb2_hash16("kp")),       kp_);
    param_to_float(find_param(params, n, djb2_hash16("ki")),       ki_);
    param_to_float(find_param(params, n, djb2_hash16("kd")),       kd_);
    param_to_float(find_param(params, n, djb2_hash16("out_min")),  out_min_);
    param_to_float(find_param(params, n, djb2_hash16("out_max")),  out_max_);

    integral_ = 0.0f;
    last_input_ = 0.0f;
    first_tick_ = true;
}

void PidController::on_tick(uint32_t dt_ms, ActionContext& ctx) {
    if (input_key_[0] == '\0' || output_key_[0] == '\0') return;
    if (dt_ms == 0) return;  // avoid divide-by-zero

    float input;
    if (!read_state_float(ctx.state, input_key_, input)) return;

    const float dt = static_cast<float>(dt_ms) * 0.001f;
    const float error = setpoint_ - input;

    // Tentative output
    float p_term = kp_ * error;
    float i_term = ki_ * integral_;
    // Derivative-on-measurement (avoids derivative kick на setpoint change).
    // Sign inverted because d(measurement)/dt has opposite sign до d(error)/dt.
    float d_term = 0.0f;
    if (!first_tick_) {
        d_term = -kd_ * (input - last_input_) / dt;
    }

    float output = p_term + i_term + d_term;

    // Anti-windup: only integrate якщо output не saturated, або якщо integration
    // would push output BACK to within range (conditional integration).
    bool saturated_high = output > out_max_;
    bool saturated_low  = output < out_min_;
    bool windup_blocked = (saturated_high && error > 0.0f)
                       || (saturated_low  && error < 0.0f);
    if (!windup_blocked) {
        integral_ += error * dt;
        // Recompute output із new integral
        output = kp_ * error + ki_ * integral_ + d_term;
    }

    // Clamp і write
    if (output > out_max_) output = out_max_;
    if (output < out_min_) output = out_min_;

    if (ctx.state != nullptr) {
        ctx.state->set(output_key_, output);
    }

    last_input_ = input;
    first_tick_ = false;
}

void PidController::on_deactivate(ActionContext& ctx) {
    (void)ctx;
    // Don't reset integral — preserved для potential re-activation у same scenario
}

ContinuousBehavior* primitives::pid_factory() {
    return new PidController();
}

// ─────────────────────────────────────────────────────────────────────
// HysteresisController
// ─────────────────────────────────────────────────────────────────────

void HysteresisController::on_activate(const ActionParam* params, uint8_t n,
                                       const char* string_pool, ActionContext& ctx) {
    uint16_t pool_size = ctx.string_pool_size;

    param_to_string(find_param(params, n, djb2_hash16("input_key")),
                    string_pool, pool_size, input_key_, sizeof(input_key_));
    param_to_string(find_param(params, n, djb2_hash16("output_key")),
                    string_pool, pool_size, output_key_, sizeof(output_key_));

    param_to_float(find_param(params, n, djb2_hash16("setpoint")), setpoint_);
    param_to_float(find_param(params, n, djb2_hash16("deadband")), deadband_);
    if (deadband_ < 0.0f) deadband_ = -deadband_;  // negative deadband nonsensical
    param_to_int(find_param(params, n, djb2_hash16("mode")), mode_);
    if (mode_ != COOLING && mode_ != HEATING) mode_ = COOLING;

    last_output_ = false;
}

void HysteresisController::on_tick(uint32_t dt_ms, ActionContext& ctx) {
    (void)dt_ms;
    if (input_key_[0] == '\0' || output_key_[0] == '\0') return;

    float input;
    if (!read_state_float(ctx.state, input_key_, input)) return;

    const float upper = setpoint_ + deadband_;
    const float lower = setpoint_ - deadband_;
    bool new_output = last_output_;

    if (mode_ == COOLING) {
        // Cooling: hot → on, cold → off
        if (input > upper) new_output = true;
        else if (input < lower) new_output = false;
        // Within deadband: hold last
    } else {  // HEATING
        // Heating: cold → on, hot → off
        if (input < lower) new_output = true;
        else if (input > upper) new_output = false;
    }

    if (new_output != last_output_ || !ctx.state) {
        if (ctx.state != nullptr) {
            ctx.state->set(output_key_, new_output);
        }
        last_output_ = new_output;
    }
}

void HysteresisController::on_deactivate(ActionContext& ctx) {
    // On deactivate, force output OFF — safer fail-state. Recipe author
    // can override через explicit set_state if "freeze last" desired.
    if (ctx.state != nullptr && output_key_[0] != '\0') {
        ctx.state->set(output_key_, false);
    }
    last_output_ = false;
}

ContinuousBehavior* primitives::hysteresis_factory() {
    return new HysteresisController();
}

// ─────────────────────────────────────────────────────────────────────
// RampProfile
// ─────────────────────────────────────────────────────────────────────

void RampProfile::on_activate(const ActionParam* params, uint8_t n,
                              const char* string_pool, ActionContext& ctx) {
    uint16_t pool_size = ctx.string_pool_size;

    param_to_string(find_param(params, n, djb2_hash16("output_key")),
                    string_pool, pool_size, output_key_, sizeof(output_key_));
    param_to_float(find_param(params, n, djb2_hash16("start_value")), start_value_);
    param_to_float(find_param(params, n, djb2_hash16("end_value")),   end_value_);

    int32_t dur = 1;
    param_to_int(find_param(params, n, djb2_hash16("duration_ms")), dur);
    if (dur < 1) dur = 1;
    duration_ms_ = static_cast<uint32_t>(dur);

    elapsed_ms_ = 0;

    // Write initial value (so output keys reflects start_value before any tick).
    if (ctx.state != nullptr && output_key_[0] != '\0') {
        ctx.state->set(output_key_, start_value_);
    }
}

void RampProfile::on_tick(uint32_t dt_ms, ActionContext& ctx) {
    if (output_key_[0] == '\0') return;

    // Saturating add (matches engine convention)
    if (UINT32_MAX - dt_ms < elapsed_ms_) {
        elapsed_ms_ = UINT32_MAX;
    } else {
        elapsed_ms_ += dt_ms;
    }

    float value;
    if (elapsed_ms_ >= duration_ms_) {
        value = end_value_;
    } else {
        // Linear interpolation: t = elapsed/duration ∈ [0, 1]
        const float t = static_cast<float>(elapsed_ms_) / static_cast<float>(duration_ms_);
        value = start_value_ + (end_value_ - start_value_) * t;
    }

    if (ctx.state != nullptr) {
        ctx.state->set(output_key_, value);
    }
}

void RampProfile::on_deactivate(ActionContext& ctx) {
    (void)ctx;
    // Don't auto-reset output. Final value persists у SharedState for downstream
    // modules. Recipe author writes а new value on phase exit якщо desired.
}

ContinuousBehavior* primitives::ramp_factory() {
    return new RampProfile();
}

// ─────────────────────────────────────────────────────────────────────
// Registry registration
// ─────────────────────────────────────────────────────────────────────

bool primitives::register_primitives(ContinuousRegistry& registry) {
    auto& reg = registry;
    bool ok = true;
    ok &= reg.register_factory(djb2_hash16(PidController::NAME),
                                PidController::NAME, &pid_factory);
    ok &= reg.register_factory(djb2_hash16(HysteresisController::NAME),
                                HysteresisController::NAME, &hysteresis_factory);
    ok &= reg.register_factory(djb2_hash16(RampProfile::NAME),
                                RampProfile::NAME, &ramp_factory);
    return ok;
}

}  // namespace modesp::scenario
