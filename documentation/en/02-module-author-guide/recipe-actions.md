# Recipe actions і conditions

> 📖 **Українською:** [documentation/uk/02-module-author-guide/recipe-actions.md](../../uk/02-module-author-guide/recipe-actions.md)

Phases у scenario recipes invoke **actions** (do something) і evaluate
**conditions** (test something). The framework ships 3 built-in actions і
10 built-in conditions; domain modules register custom ones at boot. This
page is the complete catalog plus the recipe для adding your own.

## Actions vs. conditions

| | Action | Condition |
|---|---|---|
| Purpose | Side effect (log, set state, wait) | Boolean test для transition firing |
| Return | `ActionStatus` (OK / PENDING / FAILED_*) | Reuses ActionStatus — OK = true, FAILED_RECOVERABLE = false, FAILED_ABORT = malformed |
| Used у | `entry` / `exit` phase action arrays | `when` clauses у transitions і global_transitions |
| Registry | `ActionRegistry::register_action` | `ActionRegistry::register_condition` (separate namespace) |

The framework's `ActionRegistry` keeps two flat maps (actions, conditions)
keyed by 16-bit djb2 hash of the name. Cross-namespace collisions are
allowed by design (`time_elapsed_ms` could be both, although currently
only а condition).

## Built-in actions (3)

### `log` — write diagnostic message

```json
{"action": "log", "params": {"msg": "Phase A entered"}}
```

| Param | Type | Notes |
|---|---|---|
| `msg` | string | Message up to ~64 chars. Logged at INFO level із the recipe name as the ESP_LOG tag. |

Always returns OK. Useful for milestone markers і debugging recipe flow.

### `set_state` — write а SharedState key

```json
{"action": "set_state", "params": {
  "key": "test.output_a",
  "type": "bool",
  "value": true
}}
```

| Param | Type | Notes |
|---|---|---|
| `key` | string | SharedState key, ≤ 32 chars. |
| `type` | enum | `"i32"` / `"f32"` / `"bool"`. Strings not supported (use built-in helpers OR custom action). |
| `value` | scalar | Match the `type`. JSON literals work (`true`, `42`, `3.14`). |

Returns OK on success, FAILED_RECOVERABLE if the write fails (capacity
exhausted, key length over limit).

Most common action для recipes that drive equipment — write
`equipment.req_compressor = true`, `simple_thermo.setpoint = 30.0`,
а fault flag, а progress counter, etc.

### `wait_ms` — pure time delay

```json
{"action": "wait_ms", "params": {"ms": 5000}}
```

| Param | Type | Notes |
|---|---|---|
| `ms` | int | 0 to 86,400,000 (one day). |

Returns PENDING until `phase_elapsed_ms >= ms`, then OK.

> 💡 **Tip:** prefer а transition `{"when": {"time_elapsed_ms": 5000}}`
> over `wait_ms` action. Transitions are more efficient (engine doesn't
> have to re-invoke а handler each tick) і more readable. `wait_ms` exists
> для cases where you need а pure delay between two other actions у the
> same phase entry block.

## Built-in conditions (10 leaf + 3 composite)

### Leaf conditions

#### `time_elapsed_ms`
```json
{"time_elapsed_ms": 5000}
```
True when `phase_elapsed_ms >= 5000`. Used everywhere for timed transitions.

#### `state_key_eq` / `_ne`
```json
{"state_key_eq": {"key": "test.fault", "value": true}}
{"state_key_ne": {"key": "mode", "value": "off"}}
```
Equality / inequality. Type-aware — compares `int` vs. `int`, `string` vs.
`string`. Type mismatch returns FAILED_ABORT (malformed).

#### `state_key_gt` / `_lt` / `_ge` / `_le`
```json
{"state_key_gt": {"key": "equipment.air_temp", "value": 25.0}}
{"state_key_ge": {"key": "test.counter", "value": 10}}
```
Numeric comparisons. Mixes int↔float automatically (compares as float
if either operand is float).

#### `state_key_in_range`
```json
{"state_key_in_range": {"key": "equipment.air_temp", "min": 20, "max": 25}}
```
Inclusive: true if `min <= key_value <= max`.

#### `state_key_changed`
```json
{"state_key_changed": {"key": "test.input"}}
```
Edge detection — true on first eval after key changes value. **MVP
placeholder** — currently always returns false (FAILED_RECOVERABLE).
Stage 1.5 wires engine-side edge tracking. Use sparingly.

#### `time_of_day_eq`
```json
{"time_of_day_eq": {"hh": 14, "mm": 30}}
```
Wall-clock match (minute granularity). Requires SNTP synced;
returns false if epoch < 86400 (time not yet set).

### Composite conditions

#### `all_of` — boolean AND
```json
{"all_of": [
  {"time_elapsed_ms": 1000},
  {"state_key_gt": {"key": "test.x", "value": 10}}
]}
```
All children must hold. Short-circuits on first false. Children can be
leaf або composite.

#### `any_of` — boolean OR
```json
{"any_of": [
  {"time_elapsed_ms": 30000},
  {"state_key_eq": {"key": "user.skip", "value": true}}
]}
```
First child to hold wins. Short-circuits.

#### `not` — boolean NOT
```json
{"not": {"state_key_eq": {"key": "test.x", "value": 0}}}
```
Negates а single child. (For multi-child negation use `not` із `any_of` или
combine із `all_of`.)

Composites can nest up to **16 levels deep** (`MAX_CONDITION_DEPTH`). The
compiler and loader both reject deeper trees.

## Action status semantics

Actions return one of four statuses (`ActionStatus` enum):

| Status | Meaning | Engine response |
|---|---|---|
| `OK` | Action complete | Advance to next entry/exit action; or after all done, evaluate transitions. |
| `PENDING` | Re-call next tick | Stay on this action; engine retries. Used by `wait_ms`. |
| `FAILED_RECOVERABLE` | Action couldn't proceed | Skip remaining entry/exit actions у this phase; transition fires anyway або timeout takes over. |
| `FAILED_ABORT` | Malformed args / fatal | Track → FAILED. If main track, scenario aborts. |

Conditions reuse the same enum:
- `OK` = true (condition holds)
- `FAILED_RECOVERABLE` = false (condition doesn't hold)
- `FAILED_ABORT` = malformed args (compile-time bug)

## Adding custom actions і conditions

Domain modules register custom actions / conditions at boot — typically
у the module's `on_init`. After registration, recipes can reference them
by name (just like built-ins).

### 1. Write the handler function

```cpp
// modules/my_thermo/src/my_thermo_module.cpp
#include "modesp/scenario/action_registry.h"
#include "modesp/scenario/action_param.h"

using namespace modesp::scenario;

static ActionStatus do_set_thermo_target(ActionContext& ctx) {
    // Validate param count
    if (ctx.param_count != 1) return ActionStatus::FAILED_ABORT;

    // Look up "target" param
    const ActionParam* p = nullptr;
    for (uint8_t i = 0; i < ctx.param_count; ++i) {
        if (ctx.params[i].key_hash == djb2_hash16("target")) {
            p = &ctx.params[i];
            break;
        }
    }
    if (!p || p->type != static_cast<uint8_t>(ParamType::F32)) {
        return ActionStatus::FAILED_ABORT;
    }

    // Do the work
    if (ctx.state) {
        ctx.state->set("my_thermo.target", p->v.f);
    }
    return ActionStatus::OK;
}
```

### 2. Register at boot

```cpp
bool MyThermoModule::on_init() {
    // Get registry from engine — passed through main.cpp wiring.
    // In typical setup, registry is а file-static reference your module sees.
    extern modesp::scenario::ActionRegistry scenario_actions;

    scenario_actions.register_action({
        djb2_hash16("set_thermo_target"),
        "set_thermo_target",
        &do_set_thermo_target,
        /*param_min=*/1, /*param_max=*/1
    });

    return true;
}
```

### 3. Declare у `tools/known_actions.json`

The compiler validates recipe actions against this allowlist at build
time. Add your entry:

```json
{
  "actions": {
    "set_thermo_target": {
      "hash": 21337,
      "param_min": 1,
      "param_max": 1,
      "params": {
        "target": {"type": "f32", "required": true}
      },
      "description": "Sets thermostat target temperature (custom)."
    }
  }
}
```

Run `python tools/known_actions.py --verify` to compute і check the hash
matches `djb2_hash16("set_thermo_target")`.

### 4. Use у а recipe

```json
{"action": "set_thermo_target", "params": {"target": 24.5}}
```

`compile_scenario.py` validates the action name і param shape against
`known_actions.json`. `engine.load()` validates the action hash exists у
the registered ActionRegistry at runtime.

## Conditions are registered similarly

```cpp
static ActionStatus cond_thermo_at_target(ActionContext& ctx) {
    if (ctx.param_count != 1) return ActionStatus::FAILED_ABORT;
    // ... read state, compare, return OK / FAILED_RECOVERABLE ...
}

scenario_actions.register_condition({
    djb2_hash16("thermo_at_target"),
    "thermo_at_target",
    &cond_thermo_at_target,
    1, 1
});
```

Use у а recipe's `when`:

```json
{"to": "$complete", "when": {"thermo_at_target": {"tolerance": 0.5}}}
```

## ActionContext fields

What your handler receives:

```cpp
struct ActionContext {
    IStateBackend*       state;             // R/W SharedState via backend
    const ActionParam*   params;            // Array of (param_count) params
    uint8_t              param_count;       // # of params declared у recipe
    uint16_t             string_pool_size;  // For resolving STR params
    const char*          string_pool;       // String pool from .modr
    uint32_t             scenario_elapsed_ms;
    uint32_t             phase_elapsed_ms;
    uint8_t              phase_idx;
    SequenceHandle       handle;            // Scenario instance handle (1..MAX)
    TrackIdx             track;             // 0-based track index
    const char*          recipe_name;       // For diagnostics
    const char*          track_name;
};
```

Don't write to `params` (they're const). Read state through `state->get_raw`
or the templated `state->get<T>(key, out)`. Write via `state->set(key, value)`.

## String parameters

If your action takes а string param:

```cpp
const ActionParam* key_p = /* lookup "key" param */;
if (key_p->type != static_cast<uint8_t>(ParamType::STR)) return FAILED_ABORT;

char buf[64];
uint16_t offset = key_p->v.s_idx;          // string pool offset
if (offset >= ctx.string_pool_size) return FAILED_RECOVERABLE;
uint8_t len = ctx.string_pool[offset];     // length-prefixed
if (offset + 1u + len > ctx.string_pool_size) return FAILED_RECOVERABLE;
std::memcpy(buf, &ctx.string_pool[offset + 1], len);
buf[len] = '\0';
// use `buf` як C-string.
```

The framework's built-in `builtin_actions.cpp` (`copy_string` helper) shows
this pattern verbatim. Stage 1.5 may wrap це у а helper inline у the
action_param header.

## Errors і diagnostics

- **`compile_scenario.py` rejects unknown action:** add an entry to
  `known_actions.json` AND register у your module.
- **`engine.load_buffer` returns `UNKNOWN_ACTION` / `UNKNOWN_CONDITION`:**
  the .modr file references а name that's not у the runtime ActionRegistry.
  Module's `on_init` didn't run або didn't register, or `known_actions.json`
  is out of sync із actual register calls.
- **Action returns FAILED_ABORT:** check the action's own ESP_LOG output —
  most built-ins log а reason. Most common is wrong param count or type.

## When to write а custom action

**Good fit:**
- Domain-specific writes to multiple state keys atomically.
- Hardware operations that scenario engine should orchestrate (defrost
  cycle start, OTA trigger).
- Stateful logic that needs across-tick state (counter, debouncer).
- Reading complex values (NTC raw → temperature через а calibration table).

**Don't bother:**
- Simple state writes — use `set_state` із а single value.
- Time delays — use transition `time_elapsed_ms`, not `wait_ms` action.
- "Print value if condition" — combine `state_key_*` condition AND
  `log` action у the phase.

## Common mistakes

**Forgetting to update `known_actions.json`:** module registers fine,
recipe compiles but with а warning, `.modr` rejected at runtime. Always
update both places when adding actions.

**Hash collision:** if two action names hash to the same uint16, registry
rejects the second. djb2_hash16 has good distribution, but with 65535
buckets і ~hundreds of actions, collisions remain rare. The
`known_actions.json` audit catches them at PR review time.

**Forgetting `param_min` / `param_max`:** registry accepts but recipe
validation by compile_scenario.py might pass even with too many or too
few params. Set realistic bounds.

**Side effects у conditions:** conditions should be **pure reads**. If
your condition mutates state, the engine evaluates it multiple times per
phase (once per transition check per tick) — side effects accumulate
unpredictably. Use actions for mutations.

**Long-running work у an action:** actions tick on the 100 Hz engine task.
Doing > 5 ms of work blocks the engine. If you need slow work, write а
service module that does it asynchronously, AND triggers а state key change
that recipe conditions can observe.

## Next steps

- **[recipe-authoring.md](recipe-authoring.md)** — using actions і
  conditions у phases і transitions.
- **[continuous-behaviors.md](continuous-behaviors.md)** — PID / hysteresis
  / ramp controllers що run alongside phases (different from actions).
- **[scenario-engine/03_api_reference.md](../03-framework-reference/scenario-engine/03_api_reference.md)** —
  ActionRegistry і Engine APIs.
- **[scenario-engine/10_error_model.md](../03-framework-reference/scenario-engine/10_error_model.md)** —
  full ActionStatus taxonomy і engine response table.

## Source

- [`components/modesp_scenario/include/modesp/scenario/action_registry.h`](../../../components/modesp_scenario/include/modesp/scenario/action_registry.h) — registry API.
- [`components/modesp_scenario/src/actions/builtin_actions.cpp`](../../../components/modesp_scenario/src/actions/builtin_actions.cpp) — реалізація built-in actions і conditions.
- [`tools/known_actions.json`](../../../tools/known_actions.json) — audit catalog для compile-time validation.
