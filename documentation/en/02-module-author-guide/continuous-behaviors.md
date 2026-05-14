# Continuous behaviors

> 📖 **Українською:** [documentation/uk/02-module-author-guide/continuous-behaviors.md](../../uk/02-module-author-guide/continuous-behaviors.md)

A **continuous behavior** is а control loop that runs every engine tick
while а recipe phase is active — а PID controller, hysteresis bang-bang,
linear ramp generator, anything that produces а time-varying output. While
actions are one-shot (run once during entry/exit) і transitions are
discrete (fire OR not), continuous behaviors are tick-driven continuously
throughout the phase's dwell time.

The framework ships 3 standard primitives (PID, hysteresis, ramp). Domain
modules can register custom behaviors через the same pattern. This page
documents the built-ins, their parameters, і how to write your own.

## When to use continuous behaviors

| Need | Reach for |
|---|---|
| Hold а temperature at setpoint | PID (analog actuator) or hysteresis (relay) |
| Smooth setpoint ramp over time | Ramp profile |
| Trigger something exactly at phase enter / exit | Action (entry/exit) |
| Repeat а check every tick (transition test) | Condition у `when` clause |
| Continuous feedback control during phase | Continuous behavior |

Common pattern: phase activates а PID controller with а target setpoint;
PID runs every tick adjusting the actuator output until the phase
transitions out, at which point the PID deactivates.

## Built-in primitives

The framework ships three under `modesp::scenario::primitives`. Register
them at boot:

```cpp
modesp::scenario::ContinuousRegistry continuous_registry;
modesp::scenario::primitives::register_primitives(continuous_registry);
// Now "pid", "hysteresis", "ramp" are available to recipes.
```

(Already done у the framework's `main.cpp` boilerplate. Check
[main.cpp](../../../main/main.cpp) for actual wiring.)

### `pid` — closed-loop PID controller

Standard parallel-form PID із derivative-on-measurement (no derivative
kick on setpoint change) і conditional integration anti-windup.

**Parameters** (passed at activation):

| Param | Type | Notes |
|---|---|---|
| `input_key` | string | SharedState key for measured value (е.g., `"equipment.air_temp"`). |
| `output_key` | string | SharedState key for control output (e.g., `"equipment.req_heater_pwm"`). |
| `setpoint` | float | Desired value (engineering units). |
| `kp` | float | Proportional gain. |
| `ki` | float | Integral gain (1/sec). |
| `kd` | float | Derivative gain (sec). |
| `out_min` | float | Output lower clamp. |
| `out_max` | float | Output upper clamp. |

**Algorithm:**
```
error      = setpoint - measurement
P_term     = kp × error
I_term     = ki × integral
D_term     = -kd × (measurement - prev_measurement) / dt
output     = P + I + D, clamped to [out_min, out_max]
integral  += error × dt, ONLY if output not saturated against integration direction
```

Anti-windup: integration paused коли output saturated AND error would push
further into the saturation direction.

> 💡 **Tip:** for temperature loops, start із kp = а few units per
> degree, ki = kp / time_constant_seconds, kd = 0. Tune empirically. If
> unsure, hysteresis controller is more forgiving.

### `hysteresis` — bang-bang із deadband

Threshold controller — flips а binary output when measurement crosses
setpoint ± deadband. No relay chatter because output stays at last value
while inside the deadband.

**Parameters:**

| Param | Type | Notes |
|---|---|---|
| `input_key` | string | Measured value key. |
| `output_key` | string | Binary output key (`bool` у SharedState). |
| `setpoint` | float | Target value. |
| `deadband` | float | Hysteresis width (symmetric around setpoint). |
| `mode` | int | `0` = cooling (above → ON), `1` = heating (below → ON). |

**Algorithm (mode = heating):**
- Below `setpoint - deadband`: output ON.
- Above `setpoint + deadband`: output OFF.
- Inside deadband: hold last value.

Forces output OFF on `on_deactivate` (fail-safe). Recipes що want different
behavior should explicitly `set_state` після transitioning out.

### `ramp` — linear ramp generator

Writes а value that linearly interpolates from `start_value` to
`end_value` over `duration_ms`. Used для smooth setpoint transitions
(controlled heating curve, gradual valve opening).

**Parameters:**

| Param | Type | Notes |
|---|---|---|
| `output_key` | string | Key receiving the interpolated value (float). |
| `start_value` | float | Value at t = 0. |
| `end_value` | float | Value at t = duration_ms. |
| `duration_ms` | int | Total ramp duration. |

Saturating-add elapsed counter — once `elapsed_ms >= duration_ms`, output
holds at `end_value` indefinitely. State (elapsed_ms) resets on each
re-activation.

## Using continuous behaviors у recipes

> ⚠️ **Stage 1.5 wiring:** the binary format reserves `cont_mask` у each
> phase, але the engine doesn't yet activate ContinuousBehaviors from
> recipe phases. Currently the only way to drive а PID/hysteresis/ramp
> is to instantiate it C++-side у your domain module and feed parameters
> manually. Stage 1.5 wires phase-driven activation через `cont_mask`
> bits referencing registered behaviors. The interface below describes
> the planned recipe syntax.

Planned recipe syntax:

```json
{
  "name": "active_phase",
  "continuous": [
    {
      "behavior": "pid",
      "params": {
        "input_key": "equipment.air_temp",
        "output_key": "equipment.req_heater_pwm",
        "setpoint": 22.0,
        "kp": 5.0, "ki": 0.1, "kd": 0.5,
        "out_min": 0, "out_max": 100
      }
    }
  ],
  "transitions": [...]
}
```

Engine semantics:
- `on_activate(params, ctx)` called when phase entered. Behavior reads
  parameters і initialises state.
- `on_tick(dt_ms, ctx)` called every engine tick (~10 ms). Behavior reads
  inputs from SharedState, computes, writes outputs.
- `on_deactivate(ctx)` called when phase exits. Behavior may clean up,
  reset outputs, etc.

If the same behavior is referenced у consecutive phases, engine keeps the
instance alive across the boundary (PID integral carries over). If а phase
references different behaviors, engine deactivates the old і activates the
new at the transition boundary.

## Manual usage (Stage 1 reality)

Until recipe-driven activation lands, drive behaviors з your domain module:

```cpp
// In your module's on_init:
behavior_ = modesp::scenario::primitives::pid_factory();
// or: behavior_ = continuous_registry.create(djb2_hash16("pid"));

// In on_update — manually drive activation/tick/deactivation:
ActionContext ctx{};
ctx.state = &shared_state_backend;
ctx.params = build_params_array();
ctx.param_count = N;
ctx.string_pool = scenario.string_pool_data();  // or your own
ctx.string_pool_size = ...;

if (!activated_) {
    behavior_->on_activate(ctx.params, ctx.param_count, ctx.string_pool, ctx);
    activated_ = true;
}
behavior_->on_tick(dt_ms, ctx);

// When done:
behavior_->on_deactivate(ctx);
delete behavior_;
```

This works but loses the scenario-engine integration (auto activate/deactivate
on phase boundaries). Use it only as а bridge until Stage 1.5.

## Writing а custom continuous behavior

Three-step pattern: subclass ContinuousBehavior, register factory at boot,
reference у recipe (Stage 1.5).

### 1. Subclass `ContinuousBehavior`

```cpp
// modules/my_thermo/include/my_thermo_pid_variant.h
#pragma once
#include "modesp/scenario/continuous_behavior.h"
#include "modesp/scenario/modr_format.h"   // djb2_hash16

class MyPidVariant : public modesp::scenario::ContinuousBehavior {
public:
    static constexpr const char* NAME = "my_pid_variant";

    void on_activate(const modesp::scenario::ActionParam* params, uint8_t n,
                     const char* string_pool,
                     modesp::scenario::ActionContext& ctx) override;
    void on_tick(uint32_t dt_ms, modesp::scenario::ActionContext& ctx) override;
    void on_deactivate(modesp::scenario::ActionContext& ctx) override;

    uint16_t hash() const override { return modesp::scenario::djb2_hash16(NAME); }
    const char* name() const override { return NAME; }

private:
    // your state...
    char output_key_[32] = {0};
    float kp_ = 1.0f;
    // ...
};
```

### 2. Implement and register

```cpp
// modules/my_thermo/src/my_thermo_pid_variant.cpp
#include "my_thermo_pid_variant.h"

void MyPidVariant::on_activate(const modesp::scenario::ActionParam* params,
                                uint8_t n, const char* string_pool,
                                modesp::scenario::ActionContext& ctx) {
    // Read your params...
}

void MyPidVariant::on_tick(uint32_t dt_ms, modesp::scenario::ActionContext& ctx) {
    // Compute and write output_key...
}

void MyPidVariant::on_deactivate(modesp::scenario::ActionContext& ctx) {
    // Cleanup...
}

// Factory returns а new heap-allocated instance.
// Caller (engine) deletes when phase deactivates.
static modesp::scenario::ContinuousBehavior* my_pid_factory() {
    return new MyPidVariant();
}
```

```cpp
// modules/my_thermo/src/my_thermo_module.cpp — у on_init:
bool MyThermoModule::on_init() {
    extern modesp::scenario::ContinuousRegistry continuous_registry;
    continuous_registry.register_factory(
        modesp::scenario::djb2_hash16("my_pid_variant"),
        "my_pid_variant",
        &my_pid_factory
    );
    return true;
}
```

### 3. Reference у recipe (Stage 1.5)

```json
"continuous": [
  {"behavior": "my_pid_variant", "params": {...}}
]
```

Until Stage 1.5, instantiate manually via factory:

```cpp
auto* drv = continuous_registry.create(djb2_hash16("my_pid_variant"));
// use it manually as described above.
```

## ContinuousBehavior interface

```cpp
class ContinuousBehavior {
public:
    virtual ~ContinuousBehavior() = default;

    /// Called when phase із this behavior's cont_mask bit set is entered.
    virtual void on_activate(const ActionParam* params, uint8_t param_count,
                             const char* string_pool, ActionContext& ctx) = 0;

    /// Called every engine tick while active (~10 ms).
    virtual void on_tick(uint32_t dt_ms, ActionContext& ctx) = 0;

    /// Called on phase exit (next phase doesn't use це behavior).
    virtual void on_deactivate(ActionContext& ctx) = 0;

    /// Optional NVS persistence для crash recovery.
    virtual size_t serialize(uint8_t* buf, size_t cap) const { return 0; }
    virtual bool deserialize(const uint8_t* buf, size_t len) { return true; }

    /// Identity for registry matching.
    virtual uint16_t hash() const = 0;
    virtual const char* name() const = 0;
};
```

| Method | Notes |
|---|---|
| `on_activate` | Read parameters into instance state. Set up integrators / accumulators. Output may be written immediately або deferred to first tick. |
| `on_tick` | Hot path — must be fast (< 1 ms typical). Read inputs, compute, write outputs. `dt_ms` is time since last tick (typically 10 ms). |
| `on_deactivate` | Cleanup, fail-safe output, log final state. Don't free your own memory — engine owns the instance. |
| `serialize`/`deserialize` | Optional Stage 1.5 feature для recovering integrator state across power loss. Default no-op. |

## Parameter resolution

Same as actions — `ActionParam[]` array із `key_hash`/`type`/`value`.
Read pattern (lifted from `continuous_primitives.cpp`):

```cpp
namespace {
const ActionParam* find_param(const ActionParam* params, uint8_t n,
                              uint16_t key_hash) {
    for (uint8_t i = 0; i < n; ++i) {
        if (params[i].key_hash == key_hash) return &params[i];
    }
    return nullptr;
}

bool param_to_float(const ActionParam* p, float& out) {
    if (!p) return false;
    if (p->type == static_cast<uint8_t>(ParamType::F32)) { out = p->v.f; return true; }
    if (p->type == static_cast<uint8_t>(ParamType::I32)) { out = static_cast<float>(p->v.i); return true; }
    return false;
}
}

void MyBehavior::on_activate(const ActionParam* params, uint8_t n,
                              const char* sp, ActionContext& ctx) {
    param_to_float(find_param(params, n, djb2_hash16("setpoint")), setpoint_);
    param_to_float(find_param(params, n, djb2_hash16("kp")), kp_);
    // ...
}
```

## SharedState access patterns

Behaviors read input keys і write output keys через `ctx.state`. Same as
actions, but called every tick — make sure reads/writes are fast:

```cpp
void MyBehavior::on_tick(uint32_t dt_ms, ActionContext& ctx) {
    if (!ctx.state) return;
    if (input_key_[0] == '\0' || output_key_[0] == '\0') return;

    float input;
    modesp::StateValue v;
    if (!ctx.state->get_raw(input_key_, v)) return;
    if (auto pf = etl::get_if<float>(&v)) input = *pf;
    else if (auto pi = etl::get_if<int32_t>(&v)) input = static_cast<float>(*pi);
    else return;

    // Compute output...
    float output = compute(input, dt_ms);

    ctx.state->set(output_key_, output);
}
```

Save keys у your instance state at `on_activate` time, not every tick.

## Memory model

`ContinuousBehavior` instances are **heap-allocated** by their factory і
freed by the engine on `on_deactivate` (or instance unload). The framework
violates its own "no heap" rule here because:

1. Continuous behaviors are instantiated rarely — phase entry. Not а hot
   path.
2. State varies per behavior — generic preallocation у engine wastes RAM.
3. Heap budget on ESP32 (~65 KB free) easily absorbs ~10 KB of behavior
   instances simultaneously across all running scenarios.

If you need а behavior що allocates further heap (a buffer, sub-objects),
keep it bounded — defensible only for known peak sizes.

## Common mistakes

**Forgetting to set output:** behavior reads input, computes, but never
writes output. The output state key sits at its initial value forever.
Always end `on_tick` із а `ctx.state->set(output_key_, ...)`.

**Reading key not yet written:** if your `input_key` references а sensor
that boots up only after а delay, `on_activate` may run before the first
reading. Test із `ctx.state->get_raw` AND `is_healthy` checks; fall back
to safe defaults.

**Integral windup без anti-windup:** PID із bounded output but uncapped
integrator accumulates massive values when output saturates. The built-in
PID has anti-windup — your custom behavior should too. Pattern:

```cpp
if (!output_saturated_against_error_sign) {
    integral_ += error * dt;
}
```

**Heavy work у on_tick:** реальний 100 Hz tick is < 1 ms budget. Avoid
NVS writes, blocking I/O, complex parsing. Pre-compute calibration tables
у `on_activate`.

**Forgetting to handle dt_ms = 0:** first tick might come with dt_ms = 0
у some test harnesses. Guard against divide-by-zero у derivative terms.

**State carry across re-activation:** if your behavior is activated, then
deactivated, then re-activated (same instance), state persists unless you
explicitly reset у `on_activate`. Default for built-in PID is "preserve
integral". Pick consistent behavior і document it.

## Next steps

- **[recipe-authoring.md](recipe-authoring.md)** — phase syntax that
  references continuous behaviors (Stage 1.5).
- **[recipe-actions.md](recipe-actions.md)** — action і condition catalog.
- **[scenario-engine/03_api_reference.md](../03-framework-reference/scenario-engine/03_api_reference.md)** —
  ContinuousRegistry API.
- **[components/modesp_scenario.md](../03-framework-reference/components/modesp_scenario.md)**
  *(planned)* — scenario engine internals.

## Source

- [`components/modesp_scenario/include/modesp/scenario/continuous_behavior.h`](../../../components/modesp_scenario/include/modesp/scenario/continuous_behavior.h) — interface.
- [`components/modesp_scenario/include/modesp/scenario/continuous_primitives.h`](../../../components/modesp_scenario/include/modesp/scenario/continuous_primitives.h) — PID/Hysteresis/Ramp declarations.
- [`components/modesp_scenario/src/continuous/continuous_primitives.cpp`](../../../components/modesp_scenario/src/continuous/continuous_primitives.cpp) — implementations.
