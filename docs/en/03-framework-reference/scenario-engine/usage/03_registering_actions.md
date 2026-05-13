# Registering Custom Actions — Domain Module Guide

ModESP business modules що want to be controllable by scenarios add domain-
specific actions і conditions to the ActionRegistry. Built-in vocabulary
(`log`, `set_state`, `wait_ms` + 10 leaf conditions) is intentionally
hardware-agnostic; actual hardware control lives у domain modules.

## When to register

Built-in actions handle:
- Logging (`log`)
- SharedState writes (`set_state` — sets typed values that other modules
  observe via `state.get()`)
- Time delays (`wait_ms`)

Built-ins are NOT suitable for:
- Direct HAL access (set GPIO, read ADC, configure I²C)
- Domain logic (PID controller setpoint, hysteresis band, ramp profile)
- Multi-step workflows that need intra-action state

For these, your module registers custom actions.

## Registration pattern

Register actions у your module's `on_init()` BEFORE any scenarios load.
The plan calls `register_builtins()` once у `main.cpp` before module init —
your module's registrations happen після that, у `on_init()`.

```cpp
#include "modesp/scenario/action_registry.h"
#include "modesp/scenario/action_param.h"

class MulticookerModule : public modesp::BaseModule {
public:
    MulticookerModule() : BaseModule("mc", modesp::ModulePriority::NORMAL) {}

    bool on_init() override {
        using namespace modesp::scenario;
        auto& reg = ActionRegistry::instance();

        bool ok = true;
        ok &= reg.register_action({
            djb2_hash16("mc.set_target_temp"),
            "mc.set_target_temp",
            &MulticookerModule::action_set_target_temp,
            /*param_min=*/1, /*param_max=*/1
        });
        ok &= reg.register_action({
            djb2_hash16("mc.start_pid"),
            "mc.start_pid",
            &MulticookerModule::action_start_pid,
            /*param_min=*/0, /*param_max=*/0
        });
        ok &= reg.register_condition({
            djb2_hash16("mc.temp_within"),
            "mc.temp_within",
            &MulticookerModule::cond_temp_within,
            /*param_min=*/2, /*param_max=*/2
        });

        if (!ok) {
            ESP_LOGE("mc", "Action registration failed");
            return false;
        }
        return true;
    }

private:
    static modesp::scenario::ActionStatus action_set_target_temp(
        modesp::scenario::ActionContext& ctx);
    static modesp::scenario::ActionStatus action_start_pid(
        modesp::scenario::ActionContext& ctx);
    static modesp::scenario::ActionStatus cond_temp_within(
        modesp::scenario::ActionContext& ctx);
};
```

## Action function signature

All action і condition handlers share signature:

```cpp
ActionStatus (*ActionFn)(ActionContext& ctx);
```

`ActionContext` provides everything the handler needs:

```cpp
struct ActionContext {
    SharedState*    state;            // read/write SharedState
    const ActionParam* params;        // recipe-provided typed params
    uint8_t         param_count;
    const char*     string_pool;      // for STR-type params
    uint16_t        string_pool_size;
    uint32_t        scenario_elapsed_ms;
    uint32_t        phase_elapsed_ms;
    uint8_t         phase_idx;
    SequenceHandle  handle;           // owning scenario instance
    TrackIdx        track;            // owning track index
    const char*     recipe_name;      // diagnostic
    const char*     track_name;       // diagnostic
};
```

## Reading parameters

Recipe author calls action with named params:

```jsonc
{"action": "mc.set_target_temp", "params": {"temp": 85.5}}
```

Compile_scenario.py packs це into ActionParam[] with key_hash = djb2("temp").
Action handler looks up by hash:

```cpp
ActionStatus MulticookerModule::action_set_target_temp(
    modesp::scenario::ActionContext& ctx) {
    using namespace modesp::scenario;

    // Validate param count (also enforced by registry's param_min/max)
    if (ctx.param_count != 1) return ActionStatus::FAILED_ABORT;

    // Find param by name hash. Linear scan на short arrays — params typically
    // 1-3, faster than hashmap lookup для це size.
    const ActionParam* p_temp = nullptr;
    for (uint8_t i = 0; i < ctx.param_count; ++i) {
        if (ctx.params[i].key_hash == djb2_hash16("temp")) {
            p_temp = &ctx.params[i];
            break;
        }
    }
    if (!p_temp || p_temp->type != static_cast<uint8_t>(ParamType::F32)) {
        return ActionStatus::FAILED_ABORT;  // recipe author error
    }

    float temp = p_temp->v.f;
    if (temp < 0.0f || temp > 200.0f) {
        // Out-of-range value — recoverable; log + continue з transitions
        ESP_LOGW("mc", "target_temp %.1f° out of range", temp);
        return ActionStatus::FAILED_RECOVERABLE;
    }

    // Write target to SharedState — actual hardware module reads це і drives
    // PWM/relay accordingly
    ctx.state->set("mc.target_temp", temp);
    return ActionStatus::OK;
}
```

## Reading STR-type params

String params store offset into the recipe's string pool. Use the helper:

```cpp
char keybuf[32];
const auto* p_key = /* find param із hash djb2("key"), type STR */;
uint16_t off = p_key->v.s_idx;
// Manually walk pool: byte at offset = length, followed by raw bytes
if (off >= ctx.string_pool_size) return ActionStatus::FAILED_ABORT;
uint8_t len = static_cast<uint8_t>(ctx.string_pool[off]);
if (off + 1 + len > ctx.string_pool_size) return ActionStatus::FAILED_ABORT;
if (len + 1u > sizeof(keybuf)) return ActionStatus::FAILED_ABORT;
std::memcpy(keybuf, &ctx.string_pool[off + 1], len);
keybuf[len] = '\0';
// keybuf тепер null-terminated string
```

(Built-in `set_state` action's helper `copy_string` у `builtin_actions.cpp`
shows це pattern.)

## Returning ActionStatus

Per plan Q12 action failure policy machine:

| Status | Engine behavior (entry/exit actions) | Engine behavior (continuous tick) |
|---|---|---|
| `OK` | Advance to next action; transition eval after все done | Continue continuous behavior |
| `PENDING` | Re-call same action next tick (escalates after ~1s) | Re-call next tick |
| `FAILED_RECOVERABLE` | Skip remaining actions у phase, continue з transitions | Deactivate ContinuousBehavior |
| `FAILED_ABORT` | Track → TRACK_FAILED, scenario aborts якщо main_track | Track → TRACK_FAILED |

Choose carefully:
- **Programming bug** (wrong param type, missing required param): `FAILED_ABORT`
  — це is а recipe author bug, fail loudly so they catch it during development
- **Out-of-range value, transient hardware issue**: `FAILED_RECOVERABLE` —
  log, skip, let recipe transitions handle it
- **Long operation** (e.g. waiting for DS18B20 conversion ~750ms):
  `PENDING` — engine re-calls next tick. Useful for I²C/SPI sequences
- **Safety violation** (e.g. temperature unsafe): `FAILED_ABORT` — track
  enters terminal state, hardware that depended on this action's output
  must have safe defaults

## Hash collision handling

djb2_hash16 produces uint16 — birthday paradox says ~256 names before
≥50% collision chance. Most projects stay under 64 actions/conditions.
On collision:

1. ActionRegistry's `register_action` returns false (collision detected
   при insert)
2. Compile_scenario.py also detects via `tools/known_actions.json` lookup
   table (MVP: maintained manually — list all registered action names)

Resolution: rename one of the colliding actions. Convention: prefix із
module name (`mc.set_target_temp` not `set_target_temp`) reduces collision
likelihood і aids diagnostic (you immediately see which module owns an
action).

## Worked example: minimal but complete

`modules/mc_demo/manifest.json`:

```jsonc
{
  "manifest_version": 1,
  "module": "mc_demo",
  "module_type": "module",
  "version": "1.0.0",
  "priority": 5,
  "state": {
    "mc.target_temp": {"type": "float", "access": "read", "min": 0, "max": 200},
    "mc.current_temp": {"type": "float", "access": "read", "min": -20, "max": 250}
  }
}
```

`modules/mc_demo/src/mc_demo_module.cpp`:

```cpp
#include "modesp/base_module.h"
#include "modesp/shared_state.h"
#include "modesp/scenario/action_registry.h"
#include "modesp/scenario/action_param.h"
#include "esp_log.h"

class McDemoModule : public modesp::BaseModule {
public:
    McDemoModule() : BaseModule("mc_demo", modesp::ModulePriority::NORMAL) {}

    bool on_init() override {
        using namespace modesp::scenario;
        auto& reg = ActionRegistry::instance();

        bool ok = true;
        ok &= reg.register_action({
            djb2_hash16("mc.set_target_temp"),
            "mc.set_target_temp",
            &action_set_target_temp,
            1, 1
        });
        ok &= reg.register_condition({
            djb2_hash16("mc.temp_reached"),
            "mc.temp_reached",
            &cond_temp_reached,
            1, 1
        });
        return ok;
    }

private:
    using AS = modesp::scenario::ActionStatus;
    using AC = modesp::scenario::ActionContext;
    using AP = modesp::scenario::ActionParam;
    using PT = modesp::scenario::ParamType;

    static const AP* find_param(AC& ctx, uint16_t key_hash) {
        for (uint8_t i = 0; i < ctx.param_count; ++i) {
            if (ctx.params[i].key_hash == key_hash) return &ctx.params[i];
        }
        return nullptr;
    }

    static AS action_set_target_temp(AC& ctx) {
        const AP* p = find_param(ctx, modesp::scenario::djb2_hash16("temp"));
        if (!p || p->type != static_cast<uint8_t>(PT::F32)) return AS::FAILED_ABORT;
        if (p->v.f < 0.0f || p->v.f > 200.0f) {
            ESP_LOGW("mc_demo", "target_temp %.1f° out of range", p->v.f);
            return AS::FAILED_RECOVERABLE;
        }
        ctx.state->set("mc.target_temp", p->v.f);
        return AS::OK;
    }

    /// Condition: true якщо |current - target| < tolerance (param "tol")
    static AS cond_temp_reached(AC& ctx) {
        const AP* p = find_param(ctx, modesp::scenario::djb2_hash16("tol"));
        if (!p || p->type != static_cast<uint8_t>(PT::F32)) return AS::FAILED_ABORT;
        auto cur = ctx.state->get("mc.current_temp");
        auto tgt = ctx.state->get("mc.target_temp");
        if (!cur.has_value() || !tgt.has_value()) return AS::FAILED_RECOVERABLE;
        auto* cf = etl::get_if<float>(&*cur);
        auto* tf = etl::get_if<float>(&*tgt);
        if (!cf || !tf) return AS::FAILED_ABORT;
        float diff = (*cf > *tf) ? (*cf - *tf) : (*tf - *cf);
        return (diff < p->v.f) ? AS::OK : AS::FAILED_RECOVERABLE;
    }
};
```

Then у а recipe:

```jsonc
{
  "scenario": {
    "tracks": [{
      "name": "main",
      "phases": [
        {
          "name": "warmup",
          "entry": [
            {"action": "mc.set_target_temp", "params": {"temp": 85.0}}
          ],
          "transitions": [
            {"to": "soak", "when": {"mc.temp_reached": {"tol": 1.0}}}
          ]
        },
        {"name": "soak", "transitions": [{"to": "$complete"}]}
      ]
    }]
  }
}
```

## Updating known_actions.json

Add your registered names і computed hashes to `tools/known_actions.json`:

```jsonc
{
  "actions": {
    "mc.set_target_temp": {
      "hash": 12345,    // djb2_hash16("mc.set_target_temp")
      "description": "Set multicooker PID setpoint",
      "params": {"temp": "f32"}
    }
  },
  "conditions": {
    "mc.temp_reached": {
      "hash": 23456,
      "description": "Current temp within tol of target",
      "params": {"tol": "f32"}
    }
  }
}
```

Compute hash:

```bash
python -c "
def djb2_hash16(s):
    h = 5381
    for c in s.encode(): h = ((h << 5) + h + c) & 0xFFFFFFFF
    return h & 0xFFFF
print(hex(djb2_hash16('mc.set_target_temp')))"
```

Compile_scenario.py uses це table to validate recipes що reference your
action — без entry, recipe gets W0220 (unknown action) which `--strict`
elevates to error у CI.

## See also

- [02_writing_recipes.md](02_writing_recipes.md) — recipe authoring side
- `components/modesp_scenario/include/modesp/scenario/action_registry.h` —
  full API reference
- `components/modesp_scenario/src/builtin_actions.cpp` — built-in actions
  source (good template для your own implementations)
