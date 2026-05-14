# Recipe actions і conditions

> 📖 **In English:** [documentation/en/02-module-author-guide/recipe-actions.md](../../en/02-module-author-guide/recipe-actions.md)

Phases у scenario рецептах invoke **actions** (зробити щось) і evaluate
**conditions** (перевірити щось). Фреймворк ships 3 built-in actions і 10
built-in conditions; domain модулі register custom при boot. Ця сторінка
— повний каталог plus recipe для додавання власних.

## Actions vs. conditions

| | Action | Condition |
|---|---|---|
| Призначення | Side effect (log, set state, wait) | Boolean test для transition firing |
| Return | `ActionStatus` (OK / PENDING / FAILED_*) | Reuse-ить ActionStatus — OK = true, FAILED_RECOVERABLE = false, FAILED_ABORT = malformed |
| Used у | `entry` / `exit` phase action arrays | `when` clauses у transitions і global_transitions |
| Registry | `ActionRegistry::register_action` | `ActionRegistry::register_condition` (separate namespace) |

`ActionRegistry` фреймворку тримає дві flat maps (actions, conditions)
keyed by 16-bit djb2 хеш імені. Cross-namespace collisions allowed by
design (`time_elapsed_ms` міг би бути і дією і умовою, хоч зараз — лише
condition).

## Built-in actions (3)

### `log` — записати diagnostic повідомлення

```json
{"action": "log", "params": {"msg": "Phase A entered"}}
```

| Param | Type | Notes |
|---|---|---|
| `msg` | string | Повідомлення до ~64 chars. Logged at INFO level з recipe name як ESP_LOG tag. |

Завжди повертає OK. Корисно для milestone markers і debugging recipe flow.

### `set_state` — записати SharedState key

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
| `type` | enum | `"i32"` / `"f32"` / `"bool"`. Strings не supported (use built-in helpers АБО custom action). |
| `value` | scalar | Match `type`. JSON literals working (`true`, `42`, `3.14`). |

Returns OK при success, FAILED_RECOVERABLE якщо write rejected (capacity
exhausted, key length over limit).

Найпоширеніша action для рецептів що drive equipment — write
`equipment.req_compressor = true`, `simple_thermo.setpoint = 30.0`,
fault flag, progress counter, тощо.

### `wait_ms` — pure time delay

```json
{"action": "wait_ms", "params": {"ms": 5000}}
```

| Param | Type | Notes |
|---|---|---|
| `ms` | int | 0 до 86,400,000 (один день). |

Returns PENDING поки `phase_elapsed_ms >= ms`, потім OK.

> 💡 **Tip:** prefer transition `{"when": {"time_elapsed_ms": 5000}}`
> над `wait_ms` action. Transitions більш efficient (engine не має
> re-invoke handler кожен tick) і more readable. `wait_ms` існує для
> cases де потрібна pure delay між двома іншими actions у тому ж phase
> entry block.

## Built-in conditions (10 leaf + 3 composite)

### Leaf conditions

#### `time_elapsed_ms`
```json
{"time_elapsed_ms": 5000}
```
True коли `phase_elapsed_ms >= 5000`. Used скрізь для timed transitions.

#### `state_key_eq` / `_ne`
```json
{"state_key_eq": {"key": "test.fault", "value": true}}
{"state_key_ne": {"key": "mode", "value": "off"}}
```
Equality / inequality. Type-aware — compares `int` vs. `int`, `string`
vs. `string`. Type mismatch returns FAILED_ABORT (malformed).

#### `state_key_gt` / `_lt` / `_ge` / `_le`
```json
{"state_key_gt": {"key": "equipment.air_temp", "value": 25.0}}
{"state_key_ge": {"key": "test.counter", "value": 10}}
```
Numeric comparisons. Mixes int↔float автоматично (compares як float
якщо будь-який operand — float).

#### `state_key_in_range`
```json
{"state_key_in_range": {"key": "equipment.air_temp", "min": 20, "max": 25}}
```
Inclusive: true якщо `min <= key_value <= max`.

#### `state_key_changed`
```json
{"state_key_changed": {"key": "test.input"}}
```
Edge detection — true при першому eval після зміни value key. **MVP
placeholder** — зараз завжди повертає false (FAILED_RECOVERABLE). Stage
1.5 wires engine-side edge tracking. Use sparingly.

#### `time_of_day_eq`
```json
{"time_of_day_eq": {"hh": 14, "mm": 30}}
```
Wall-clock match (хвилинна granularity). Потребує SNTP synced; повертає
false якщо epoch < 86400 (time не yet set).

### Composite conditions

#### `all_of` — boolean AND
```json
{"all_of": [
  {"time_elapsed_ms": 1000},
  {"state_key_gt": {"key": "test.x", "value": 10}}
]}
```
Усі children повинні hold. Short-circuits при першому false. Children
можуть бути leaf або composite.

#### `any_of` — boolean OR
```json
{"any_of": [
  {"time_elapsed_ms": 30000},
  {"state_key_eq": {"key": "user.skip", "value": true}}
]}
```
Перший child що hold wins. Short-circuits.

#### `not` — boolean NOT
```json
{"not": {"state_key_eq": {"key": "test.x", "value": 0}}}
```
Negates single child. (Для multi-child negation use `not` із `any_of`
або combine із `all_of`.)

Composites можуть nest до **16 рівнів** (`MAX_CONDITION_DEPTH`). Compiler
і loader обидва rejects deeper trees.

## Action status семантика

Actions return одну з чотирьох статусів (`ActionStatus` enum):

| Status | Meaning | Engine response |
|---|---|---|
| `OK` | Action complete | Advance до next entry/exit action; or після all done, evaluate transitions. |
| `PENDING` | Re-call next tick | Stay на цій action; engine retries. Used by `wait_ms`. |
| `FAILED_RECOVERABLE` | Action could не proceed | Skip remaining entry/exit actions у цій phase; transition fires anyway або timeout takes over. |
| `FAILED_ABORT` | Malformed args / fatal | Track → FAILED. Якщо main track, scenario aborts. |

Conditions reuse той самий enum:
- `OK` = true (condition holds)
- `FAILED_RECOVERABLE` = false (condition не holds)
- `FAILED_ABORT` = malformed args (compile-time bug)

## Додавання custom actions і conditions

Domain модулі register custom actions / conditions при boot — типово у
`on_init` модуля. Після registration, рецепти можуть reference їх ім'ям
(як built-ins).

### 1. Написати handler функцію

```cpp
// modules/my_thermo/src/my_thermo_module.cpp
#include "modesp/scenario/action_registry.h"
#include "modesp/scenario/action_param.h"

using namespace modesp::scenario;

static ActionStatus do_set_thermo_target(ActionContext& ctx) {
    // Validate param count
    if (ctx.param_count != 1) return ActionStatus::FAILED_ABORT;

    // Lookup "target" param
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

### 2. Register при boot

```cpp
bool MyThermoModule::on_init() {
    // Get registry з engine — passed through main.cpp wiring.
    // У typical setup, registry — file-static reference що ваш модуль бачить.
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

Compiler валідує recipe actions проти цього allowlist при build time.
Додайте свій entry:

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

Запустіть `python tools/known_actions.py --verify` щоб compute і check
що hash matches `djb2_hash16("set_thermo_target")`.

### 4. Use у рецепті

```json
{"action": "set_thermo_target", "params": {"target": 24.5}}
```

`compile_scenario.py` валідує action name і param shape проти
`known_actions.json`. `engine.load()` валідує що action hash існує у
registered ActionRegistry при runtime.

## Conditions register-ються similarly

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

Use у `when` рецепта:

```json
{"to": "$complete", "when": {"thermo_at_target": {"tolerance": 0.5}}}
```

## ActionContext поля

Що ваш handler отримує:

```cpp
struct ActionContext {
    IStateBackend*       state;             // R/W SharedState через backend
    const ActionParam*   params;            // Масив (param_count) params
    uint8_t              param_count;       // # params declared у рецепті
    uint16_t             string_pool_size;  // Для resolving STR params
    const char*          string_pool;       // String pool з .modr
    uint32_t             scenario_elapsed_ms;
    uint32_t             phase_elapsed_ms;
    uint8_t              phase_idx;
    SequenceHandle       handle;            // Scenario instance handle (1..MAX)
    TrackIdx             track;             // 0-based track index
    const char*          recipe_name;       // Для diagnostics
    const char*          track_name;
};
```

Не writting у `params` (вони const). Read state через `state->get_raw`
або templated `state->get<T>(key, out)`. Write через `state->set(key, value)`.

## String parameters

Якщо ваш action takes string param:

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

`builtin_actions.cpp` фреймворку (helper `copy_string`) показує цей
pattern verbatim. Stage 1.5 може wrap це у helper inline у action_param
header.

## Errors і diagnostics

- **`compile_scenario.py` rejects unknown action:** додайте entry у
  `known_actions.json` AND register у вашому модулі.
- **`engine.load_buffer` returns `UNKNOWN_ACTION` / `UNKNOWN_CONDITION`:**
  `.modr` file references name що не у runtime ActionRegistry. Module's
  `on_init` не ran або не registered, or `known_actions.json` out of
  sync із actual register calls.
- **Action returns FAILED_ABORT:** check action's власний ESP_LOG output —
  більшість built-ins логують reason. Найпоширеніше — wrong param count
  або type.

## Коли writeing custom action

**Good fit:**
- Domain-specific writes до multiple state keys atomically.
- Hardware operations що scenario engine повинен orchestrate (defrost
  cycle start, OTA trigger).
- Stateful логіка що needs across-tick state (counter, debouncer).
- Reading complex values (NTC raw → temperature через calibration table).

**Don't bother:**
- Simple state writes — use `set_state` з єдиним value.
- Time delays — use transition `time_elapsed_ms`, не `wait_ms` action.
- "Print value if condition" — combine `state_key_*` condition AND `log`
  action у phase.

## Поширені помилки

**Забутий update `known_actions.json`:** module registers fine, recipe
compiles але з warning, `.modr` rejected при runtime. Завжди оновлюйте
обидва місця коли додаєте actions.

**Hash collision:** якщо два action імена hash-уються у той самий uint16,
registry rejects другий. djb2_hash16 має хорошу distribution, але з 65535
buckets і ~hundreds of actions, collisions залишаються rare. Audit
`known_actions.json` catches them при PR review time.

**Забутий `param_min` / `param_max`:** registry accepts але recipe
validation by compile_scenario.py might pass навіть із too many чи too
few params. Set realistic bounds.

**Side effects у conditions:** conditions повинні бути **pure reads**.
Якщо ваша condition mutates state, engine evaluates її multiple times per
phase (раз per transition check per tick) — side effects accumulate
unpredictably. Use actions для mutations.

**Long-running work у action:** actions tick на 100 Hz engine task. Doing
> 5 мс роботи blocks engine. Якщо потрібна slow work, write service
module що робить це asynchronously, AND triggers state key change що
recipe conditions can observe.

## Що далі

- **[recipe-authoring.md](recipe-authoring.md)** — using actions і
  conditions у phases і transitions.
- **[continuous-behaviors.md](continuous-behaviors.md)** — PID /
  hysteresis / ramp controllers що run alongside phases (different from
  actions).
- **[scenario-engine/03_api_reference.md](../03-framework-reference/scenario-engine/03_api_reference.md)** —
  ActionRegistry і Engine APIs.
- **[scenario-engine/10_error_model.md](../03-framework-reference/scenario-engine/10_error_model.md)** —
  повна ActionStatus таксономія і engine response table.

## Source

- [`components/modesp_scenario/include/modesp/scenario/action_registry.h`](../../../components/modesp_scenario/include/modesp/scenario/action_registry.h) — registry API.
- [`components/modesp_scenario/src/actions/builtin_actions.cpp`](../../../components/modesp_scenario/src/actions/builtin_actions.cpp) — реалізація built-in actions і conditions.
- [`tools/known_actions.json`](../../../tools/known_actions.json) — audit catalog для compile-time validation.
