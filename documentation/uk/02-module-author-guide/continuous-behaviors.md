# Continuous behaviors

> 📖 **In English:** [documentation/en/02-module-author-guide/continuous-behaviors.md](../../en/02-module-author-guide/continuous-behaviors.md)

**Continuous behavior** — це control loop що runs every engine tick поки
recipe phase active — PID controller, hysteresis bang-bang, linear ramp
generator, будь-що що produces time-varying output. Якщо actions —
one-shot (run once during entry/exit) і transitions — discrete (fire OR
not), continuous behaviors — tick-driven continuously protягом dwell time
phase.

Фреймворк ships 3 standard primitives (PID, hysteresis, ramp). Domain
модулі можуть register custom behaviors через той самий pattern. Ця
сторінка документує built-ins, їхні параметри, і як написати власний.

## Коли use continuous behaviors

| Потреба | Що взяти |
|---|---|
| Hold температуру при setpoint | PID (analog actuator) або hysteresis (relay) |
| Smooth setpoint ramp over time | Ramp profile |
| Trigger щось exactly при phase enter / exit | Action (entry/exit) |
| Repeat check кожен tick (transition test) | Condition у `when` clause |
| Continuous feedback control during phase | Continuous behavior |

Common pattern: phase activates PID controller з target setpoint; PID
runs every tick adjusting actuator output поки phase transitions out,
у цей момент PID deactivates.

## Built-in primitives

Фреймворк ships three під `modesp::scenario::primitives`. Register them
при boot:

```cpp
modesp::scenario::ContinuousRegistry continuous_registry;
modesp::scenario::primitives::register_primitives(continuous_registry);
// Тепер "pid", "hysteresis", "ramp" available для рецептів.
```

(Вже done у `main.cpp` boilerplate фреймворку. Check
[main.cpp](../../../main/main.cpp) для actual wiring.)

### `pid` — closed-loop PID controller

Standard parallel-form PID з derivative-on-measurement (без derivative
kick при setpoint change) і conditional integration anti-windup.

**Параметри** (passed при activation):

| Param | Type | Notes |
|---|---|---|
| `input_key` | string | SharedState key для measured value (наприклад, `"equipment.air_temp"`). |
| `output_key` | string | SharedState key для control output (наприклад, `"equipment.req_heater_pwm"`). |
| `setpoint` | float | Desired value (engineering units). |
| `kp` | float | Proportional gain. |
| `ki` | float | Integral gain (1/sec). |
| `kd` | float | Derivative gain (sec). |
| `out_min` | float | Output lower clamp. |
| `out_max` | float | Output upper clamp. |

**Алгоритм:**
```
error      = setpoint - measurement
P_term     = kp × error
I_term     = ki × integral
D_term     = -kd × (measurement - prev_measurement) / dt
output     = P + I + D, clamped до [out_min, out_max]
integral  += error × dt, ТІЛЬКИ якщо output не saturated проти integration direction
```

Anti-windup: integration paused коли output saturated AND error would
push further у saturation direction.

> 💡 **Tip:** для temperature loops, start з kp = декілька units на
> degree, ki = kp / time_constant_seconds, kd = 0. Tune empirically.
> Якщо unsure, hysteresis controller більш forgiving.

### `hysteresis` — bang-bang з deadband

Threshold controller — flips binary output коли measurement crosses
setpoint ± deadband. Без relay chatter бо output stays at last value
коли inside deadband.

**Параметри:**

| Param | Type | Notes |
|---|---|---|
| `input_key` | string | Key measured value. |
| `output_key` | string | Binary output key (`bool` у SharedState). |
| `setpoint` | float | Target value. |
| `deadband` | float | Hysteresis width (symmetric around setpoint). |
| `mode` | int | `0` = cooling (above → ON), `1` = heating (below → ON). |

**Алгоритм (mode = heating):**
- Below `setpoint - deadband`: output ON.
- Above `setpoint + deadband`: output OFF.
- Inside deadband: hold last value.

Forces output OFF при `on_deactivate` (fail-safe). Рецепти що хочуть
іншу поведінку повинні explicitly `set_state` після transitioning out.

### `ramp` — linear ramp generator

Writes value що linearly interpolates from `start_value` до `end_value`
over `duration_ms`. Used для smooth setpoint transitions (controlled
heating curve, gradual valve opening).

**Параметри:**

| Param | Type | Notes |
|---|---|---|
| `output_key` | string | Key receiving interpolated value (float). |
| `start_value` | float | Value при t = 0. |
| `end_value` | float | Value при t = duration_ms. |
| `duration_ms` | int | Total ramp duration. |

Saturating-add elapsed counter — раз `elapsed_ms >= duration_ms`, output
holds на `end_value` indefinitely. State (elapsed_ms) resets при кожному
re-activation.

## Використання continuous behaviors у рецептах

> ⚠️ **Stage 1.5 wiring:** binary format reserves `cont_mask` у кожній
> phase, але engine ще не activates ContinuousBehaviors з recipe phases.
> Зараз єдиний спосіб drive PID/hysteresis/ramp — instantiate його
> C++-side у вашому domain module і feed parameters manually. Stage 1.5
> wires phase-driven activation через `cont_mask` bits referencing
> registered behaviors. Інтерфейс нижче описує planned recipe syntax.

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
- `on_activate(params, ctx)` called коли phase entered. Behavior reads
  parameters і initialises state.
- `on_tick(dt_ms, ctx)` called кожен engine tick (~10 мс). Behavior
  reads inputs з SharedState, computes, writes outputs.
- `on_deactivate(ctx)` called коли phase exits. Behavior може clean up,
  reset outputs, тощо.

Якщо same behavior referenced у consecutive phases, engine keeps instance
alive across boundary (PID integral carries over). Якщо phase references
different behaviors, engine deactivates old і activates new при transition
boundary.

## Manual usage (Stage 1 reality)

Поки recipe-driven activation не landed, drive behaviors з вашого domain
module:

```cpp
// У module's on_init:
behavior_ = modesp::scenario::primitives::pid_factory();
// or: behavior_ = continuous_registry.create(djb2_hash16("pid"));

// У on_update — manually drive activation/tick/deactivation:
ActionContext ctx{};
ctx.state = &shared_state_backend;
ctx.params = build_params_array();
ctx.param_count = N;
ctx.string_pool = scenario.string_pool_data();  // або власний
ctx.string_pool_size = ...;

if (!activated_) {
    behavior_->on_activate(ctx.params, ctx.param_count, ctx.string_pool, ctx);
    activated_ = true;
}
behavior_->on_tick(dt_ms, ctx);

// Коли done:
behavior_->on_deactivate(ctx);
delete behavior_;
```

Це працює але втрачає scenario-engine integration (auto activate/deactivate
при phase boundaries). Use лише як bridge поки Stage 1.5.

## Написання custom continuous behavior

Three-step pattern: subclass ContinuousBehavior, register factory при
boot, reference у рецепті (Stage 1.5).

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
    // ваш state...
    char output_key_[32] = {0};
    float kp_ = 1.0f;
    // ...
};
```

### 2. Implement і register

```cpp
// modules/my_thermo/src/my_thermo_pid_variant.cpp
#include "my_thermo_pid_variant.h"

void MyPidVariant::on_activate(const modesp::scenario::ActionParam* params,
                                uint8_t n, const char* string_pool,
                                modesp::scenario::ActionContext& ctx) {
    // Read ваші params...
}

void MyPidVariant::on_tick(uint32_t dt_ms, modesp::scenario::ActionContext& ctx) {
    // Compute і write output_key...
}

void MyPidVariant::on_deactivate(modesp::scenario::ActionContext& ctx) {
    // Cleanup...
}

// Factory повертає нову heap-allocated instance.
// Caller (engine) deletes коли phase deactivates.
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

### 3. Reference у рецепті (Stage 1.5)

```json
"continuous": [
  {"behavior": "my_pid_variant", "params": {...}}
]
```

Поки Stage 1.5, instantiate manually через factory:

```cpp
auto* drv = continuous_registry.create(djb2_hash16("my_pid_variant"));
// use it manually як описано вище.
```

## ContinuousBehavior interface

```cpp
class ContinuousBehavior {
public:
    virtual ~ContinuousBehavior() = default;

    /// Called коли phase із цей behavior's cont_mask bit set is entered.
    virtual void on_activate(const ActionParam* params, uint8_t param_count,
                             const char* string_pool, ActionContext& ctx) = 0;

    /// Called кожен engine tick поки active (~10 мс).
    virtual void on_tick(uint32_t dt_ms, ActionContext& ctx) = 0;

    /// Called при phase exit (next phase не use це behavior).
    virtual void on_deactivate(ActionContext& ctx) = 0;

    /// Optional NVS persistence для crash recovery.
    virtual size_t serialize(uint8_t* buf, size_t cap) const { return 0; }
    virtual bool deserialize(const uint8_t* buf, size_t len) { return true; }

    /// Identity для registry matching.
    virtual uint16_t hash() const = 0;
    virtual const char* name() const = 0;
};
```

| Метод | Notes |
|---|---|
| `on_activate` | Read parameters у instance state. Set up integrators / accumulators. Output може writeти immediately або deferred до first tick. |
| `on_tick` | Hot path — must be fast (< 1 мс типово). Read inputs, compute, write outputs. `dt_ms` — time since last tick (типово 10 мс). |
| `on_deactivate` | Cleanup, fail-safe output, log final state. Не free власну пам'ять — engine owns instance. |
| `serialize`/`deserialize` | Optional Stage 1.5 feature для recovering integrator state across power loss. Default no-op. |

## Parameter resolution

Same як actions — `ActionParam[]` array із `key_hash`/`type`/`value`.
Read pattern (lifted з `continuous_primitives.cpp`):

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

Behaviors read input keys і write output keys через `ctx.state`. Same як
actions, але called every tick — make sure reads/writes fast:

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

Save keys у instance state при `on_activate` time, не every tick.

## Memory model

`ContinuousBehavior` instances **heap-allocated** by factory і freed by
engine при `on_deactivate` (or instance unload). Фреймворк violates
власне "no heap" rule тут бо:

1. Continuous behaviors instantiated rarely — phase entry. Не hot path.
2. State varies per behavior — generic preallocation у engine wastes RAM.
3. Heap budget на ESP32 (~65 KB free) easily absorbs ~10 KB behavior
   instances simultaneously across всі running scenarios.

Якщо вам треба behavior що allocates further heap (buffer, sub-objects),
keep it bounded — defensible лише для known peak sizes.

## Поширені помилки

**Забутий set output:** behavior reads input, computes, але ніколи не
writes output. Output state key sits на initial value forever. Завжди end
`on_tick` із `ctx.state->set(output_key_, ...)`.

**Reading key not yet written:** якщо ваш `input_key` references sensor
що boots up лише після delay, `on_activate` may run before first reading.
Test із `ctx.state->get_raw` AND `is_healthy` checks; fall back to safe
defaults.

**Integral windup без anti-windup:** PID з bounded output але uncapped
integrator accumulates massive values коли output saturates. Built-in PID
має anti-windup — ваш custom behavior повинен теж. Pattern:

```cpp
if (!output_saturated_against_error_sign) {
    integral_ += error * dt;
}
```

**Heavy work у on_tick:** real 100 Hz tick — < 1 мс budget. Avoid NVS
writes, blocking I/O, complex parsing. Pre-compute calibration tables у
`on_activate`.

**Забутий handle dt_ms = 0:** перший tick might come із dt_ms = 0 у деяких
test harnesses. Guard проти divide-by-zero у derivative terms.

**State carry across re-activation:** якщо ваш behavior activated, потім
deactivated, потім re-activated (same instance), state persists unless ви
explicitly reset у `on_activate`. Default для built-in PID — "preserve
integral". Pick consistent behavior і document it.

## Що далі

- **[recipe-authoring.md](recipe-authoring.md)** — phase syntax що
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
