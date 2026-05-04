/**
 * @file continuous_primitives.h
 * @brief Standard continuous control primitives — PID, hysteresis, ramp.
 *
 * Per ADR-0006: ModESP framework ships 0 built-in ContinuousBehaviors.
 * Це file provides OPTIONAL standard primitives що domain modules may
 * register if they want them. Default: NOT registered. Caller must
 * explicitly call `primitives::register_primitives()` (analogous до
 * `builtins::register_builtins()` для actions).
 *
 * Each primitive is а standalone class inheriting `ContinuousBehavior`.
 * Heap-allocated per instance — factory returns `new T()`. Engine (або
 * domain module) responsible для `delete` on unload/deactivate-permanent.
 *
 * State preserved across phase transitions WHILE behavior held active
 * (per ContinuousBehavior contract). Reset implicit на on_deactivate;
 * primitives override це if needed для re-init semantics.
 *
 * ## Usage patterns
 *
 * ### Direct (no engine wiring needed)
 *
 * ```cpp
 * auto* pid = new modesp::sequence::PidController();
 * modesp::sequence::ActionContext ctx{};
 * ctx.state = &shared_state;
 * // build params: setpoint=22, kp=2.0, ki=0.1, kd=0.5, input_key, output_key
 * pid->on_activate(params, n, string_pool, ctx);
 *
 * // ... в loop:
 * pid->on_tick(10, ctx);  // every 10ms
 *
 * // when done:
 * pid->on_deactivate(ctx);
 * delete pid;
 * ```
 *
 * ### Via registry (for future engine integration)
 *
 * ```cpp
 * modesp::sequence::primitives::register_primitives();
 * auto* pid = ContinuousRegistry::instance().create(djb2_hash16("pid"));
 * // ... use pid as above
 * ```
 *
 * Reference: docs/sequence_engine/usage/04_custom_continuous.md (Stage 1.5).
 */

#pragma once

#include "modesp/sequence/continuous_behavior.h"
#include "modesp/sequence/action_param.h"
#include "modesp/sequence/modr_format.h"   // djb2_hash16

namespace modesp::sequence {

/**
 * Closed-loop PID controller. Reads measured value from SharedState,
 * writes computed control output to SharedState.
 *
 * Parameters (passed to on_activate via ActionParam[]):
 *   "input_key"   STR  SharedState key для measured value (e.g. "equipment.air_temp")
 *   "output_key"  STR  SharedState key для control output (0..100% або actuator command)
 *   "setpoint"    F32  desired value (engineering units)
 *   "kp"          F32  proportional gain
 *   "ki"          F32  integral gain (1/sec)
 *   "kd"          F32  derivative gain (sec)
 *   "out_min"     F32  output lower clamp
 *   "out_max"     F32  output upper clamp
 *
 * Algorithm: standard parallel-form PID із derivative-on-measurement
 * (avoids derivative kick on setpoint change). Anti-windup через
 * conditional integration (clamp integral if output saturated).
 *
 * NOT thread-safe. Engine guarantees serialized access from update task.
 */
class PidController : public ContinuousBehavior {
public:
    static constexpr const char* NAME = "pid";

    void on_activate(const ActionParam* params, uint8_t param_count,
                     const char* string_pool, ActionContext& ctx) override;
    void on_tick(uint32_t dt_ms, ActionContext& ctx) override;
    void on_deactivate(ActionContext& ctx) override;

    uint16_t hash() const override { return djb2_hash16(NAME); }
    const char* name() const override { return NAME; }

private:
    // Configuration (set by on_activate)
    char     input_key_[32]  = {0};
    char     output_key_[32] = {0};
    float    setpoint_ = 0.0f;
    float    kp_       = 1.0f;
    float    ki_       = 0.0f;
    float    kd_       = 0.0f;
    float    out_min_  = 0.0f;
    float    out_max_  = 100.0f;

    // Runtime state
    float    integral_   = 0.0f;
    float    last_input_ = 0.0f;
    bool     first_tick_ = true;
};

/**
 * Bang-bang controller із hysteresis (deadband). Reads measured value
 * from SharedState, writes bool actuator command.
 *
 * Parameters:
 *   "input_key"   STR  SharedState key для measured value
 *   "output_key"  STR  SharedState key для bool output
 *   "setpoint"    F32  target value
 *   "deadband"    F32  deadband size (symmetric around setpoint)
 *   "mode"        I32  0 = cooling (output ON when above setpoint+db, OFF below setpoint-db)
 *                      1 = heating (output ON when below setpoint-db, OFF above setpoint+db)
 *
 * Output stays at last value while input within deadband (preserve state
 * to avoid relay chatter). Initial output OFF until first reading clears
 * deadband.
 */
class HysteresisController : public ContinuousBehavior {
public:
    static constexpr const char* NAME = "hysteresis";

    enum Mode : int32_t { COOLING = 0, HEATING = 1 };

    void on_activate(const ActionParam* params, uint8_t param_count,
                     const char* string_pool, ActionContext& ctx) override;
    void on_tick(uint32_t dt_ms, ActionContext& ctx) override;
    void on_deactivate(ActionContext& ctx) override;

    uint16_t hash() const override { return djb2_hash16(NAME); }
    const char* name() const override { return NAME; }

private:
    char     input_key_[32]  = {0};
    char     output_key_[32] = {0};
    float    setpoint_ = 0.0f;
    float    deadband_ = 1.0f;
    int32_t  mode_     = COOLING;

    bool     last_output_ = false;
};

/**
 * Linear ramp generator. Writes а value що linearly interpolates from
 * `start_value` to `end_value` over `duration_ms`. Useful для smooth
 * setpoint transitions (e.g. controlled heating curve).
 *
 * Parameters:
 *   "output_key"   STR  SharedState key для ramp output (float)
 *   "start_value"  F32  value at time 0
 *   "end_value"    F32  value at duration_ms
 *   "duration_ms"  I32  ramp duration; clamped to ≥1ms
 *
 * Once duration elapsed, holds at `end_value` until on_deactivate.
 * State (elapsed_ms) reset on each on_activate.
 */
class RampProfile : public ContinuousBehavior {
public:
    static constexpr const char* NAME = "ramp";

    void on_activate(const ActionParam* params, uint8_t param_count,
                     const char* string_pool, ActionContext& ctx) override;
    void on_tick(uint32_t dt_ms, ActionContext& ctx) override;
    void on_deactivate(ActionContext& ctx) override;

    uint16_t hash() const override { return djb2_hash16(NAME); }
    const char* name() const override { return NAME; }

private:
    char     output_key_[32] = {0};
    float    start_value_ = 0.0f;
    float    end_value_   = 0.0f;
    uint32_t duration_ms_ = 1;

    uint32_t elapsed_ms_ = 0;
};

namespace primitives {

/// Factory creating heap-allocated PidController. Engine deletes на cleanup.
ContinuousBehavior* pid_factory();

/// Factory creating heap-allocated HysteresisController.
ContinuousBehavior* hysteresis_factory();

/// Factory creating heap-allocated RampProfile.
ContinuousBehavior* ramp_factory();

/// Register all standard primitives із ContinuousRegistry. Idempotent
/// (returns false on second call із collision rejections, але state OK).
/// Call ONCE before any scenario uses these behavior names.
bool register_primitives();

/// Diagnostic — number of expected primitives (currently 3).
constexpr int PRIMITIVE_COUNT = 3;

}  // namespace primitives

}  // namespace modesp::sequence
