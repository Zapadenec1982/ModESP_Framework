# Написання рецепту

> 📖 **In English:** [documentation/en/02-module-author-guide/recipe-authoring.md](../../en/02-module-author-guide/recipe-authoring.md)

**Рецепт** — це time-bounded процес — програма приготування, цикл batch
reactor, irrigation sequence, defrost routine — закодований як декларативна
state machine у `scenario` секції вашого маніфесту. Build pipeline компілює
це у бінарний `.modr` blob; scenario engine виконує його у runtime. C++ не
потрібен.

Ця сторінка покриває як структурувати сценарії — tracks, phases, transitions,
completion rules — і як recipe-as-manifest pipeline land-иться на пристрій.
Для built-in і custom actions всередині фаз, дивіться
[recipe-actions.md](recipe-actions.md). Для PID / hysteresis / ramp
controllers що run-яться всередині фаз, дивіться
[continuous-behaviors.md](continuous-behaviors.md).

## Recipe = manifest з `scenario` секцією

Recipe модуль має ту саму shape маніфесту що і service module, окрім:

- `"module_type": "recipe"` каже build pipeline пропустити C++ генерацію.
- Нова `"scenario"` секція містить декларацію FSM.
- Без `CMakeLists.txt`, `src/`, `include/`. Просто manifest.

```
modules/my_recipe/
└── manifest.json          ← entire module
```

При build time, `tools/compile_scenario.py` читає `scenario` секцію і
emit-ить `data/scenarios/<recipe_name>.modr` (CRC-validated binary).
LittleFS bundle-ить це; engine завантажує у runtime через
`engine.load_path("/data/scenarios/<recipe_name>.modr")`.

## Ментальна модель: tracks і phases

```
   Scenario
   ├── Track 1 ("main")
   │   ├── Phase A → Phase B → Phase C → $complete
   │   └── (entry actions, transitions, exit actions per phase)
   ├── Track 2 ("watcher")
   │   └── Phase X → $complete
   └── (optional) global transitions to $abort
```

**Tracks** — паралельні state machines що працюють concurrently. Кожен
track має власну phase і progress-ить independently. Tracks комунікують
через SharedState (writes від одного track стають readable для інших
наступний tick).

**Phases** — nodes у track's FSM. Phase має:
- `entry` actions — runned коли entering phase (sequenced одна на tick).
- `transitions` — умови для moving до іншої phase.
- `exit` actions — runned коли leaving (перед applying transition target).
- Optional `timeout_ms` — auto-transition до next phase після time.
- Optional `phase_resources` — claim resources на тривалість phase.

**Targets** для transitions:
- Інше phase name (`"to": "phase_b"`) — advance у тому ж track.
- `"$complete"` — track succeeds (track state → COMPLETED).
- `"$abort"` — track fails (track state → FAILED, scenario може abort).

## Мінімальний рецепт

```json
{
  "manifest_version": 1,
  "module": "my_recipe",
  "module_type": "recipe",
  "version": "1.0.0",
  "description": "Demo recipe",

  "state": {
    "my_recipe.scenario_state":   {"type": "string", "access": "read"},
    "my_recipe.scenario_elapsed_s": {"type": "int",  "access": "read"},
    "my_recipe.main_state":       {"type": "string", "access": "read"},
    "my_recipe.main_phase_name":  {"type": "string", "access": "read"},
    "my_recipe.main_phase_idx":   {"type": "int",    "access": "read"},
    "my_recipe.main_elapsed_s":   {"type": "int",    "access": "read"}
  },

  "scenario": {
    "default_phase_timeout_ms": 30000,
    "completion_rule": "all_tracks_complete",
    "tracks": [
      {
        "name": "main",
        "flags": ["main_track"],
        "phases": [
          {
            "name": "warmup",
            "entry": [
              {"action": "log", "params": {"msg": "warming up"}}
            ],
            "transitions": [
              {"to": "soak", "when": {"time_elapsed_ms": 5000}}
            ]
          },
          {
            "name": "soak",
            "entry": [
              {"action": "log", "params": {"msg": "soaking"}}
            ],
            "transitions": [
              {"to": "$complete", "when": {"time_elapsed_ms": 10000}}
            ]
          }
        ]
      }
    ]
  }
}
```

Run: load через `POST /api/scenario/load`, start через
`POST /api/scenario/start`. Engine тикає `main` track через `warmup` (5 с)
→ `soak` (10 с) → `$complete`. Mirror keys `my_recipe.main_phase_name`
тощо оновлюються live.

## Обов'язкові mirror state keys

Engine пише mirror keys у SharedState кожен tick. Ці МУСЯТЬ бути
pre-declared у `state` секції рецепту, інакше build fails з
cross-validation помилкою.

На scenario:

| Key | Type | Description |
|---|---|---|
| `<recipe>.scenario_state` | string | `"idle"`/`"loaded"`/`"running"`/`"paused"`/`"aborting"`/`"completed"`/`"failed"` |
| `<recipe>.scenario_elapsed_s` | int | Секунди з моменту scenario start |

На track:

| Key | Type | Description |
|---|---|---|
| `<recipe>.<track>_state` | string | Track FSM state |
| `<recipe>.<track>_phase_name` | string | Current phase name |
| `<recipe>.<track>_phase_idx` | int | Phase index у track (0-based) |
| `<recipe>.<track>_elapsed_s` | int | Секунди у current phase |

Для рецепту з tracks `main` і `watcher`, declare 2 + (2 × 4) = 10 mirror
keys у state секції.

**Naming budget:** SharedState keys ≤ 32 chars. Recipe name ≤ 12 chars,
track name ≤ 8 chars. Приклад: `recipe_plov.watcher_phase_name` = 30
символів — fit-иться.

## Поля scenario-level

```json
"scenario": {
  "default_phase_timeout_ms": 30000,
  "scenario_timeout_max_ms": 120000,
  "completion_rule": "all_tracks_complete",
  "resources": [],
  "global_transitions": [],
  "tracks": [...]
}
```

| Field | Required | Notes |
|---|---|---|
| `default_phase_timeout_ms` | yes | Default timeout per phase (mandatory per ADR-0007). Phases можуть override індивідуально. |
| `scenario_timeout_max_ms` | recommended | Hard cap на total scenario duration. Engine aborts якщо exceeded. |
| `completion_rule` | yes | `"all_tracks_complete"` / `"any_track_complete"` / `"main_track_complete"` — коли scenario сам finish-иться? |
| `resources` | optional | Scenario-scope resource claims (see below). |
| `global_transitions` | optional | Conditions evaluated FIRST кожен tick, перед per-track transitions. |
| `tracks` | yes | Масив track definitions (1-6). |

## Tracks

```json
{
  "name": "main",                          // ≤ 8 chars, snake_case
  "flags": ["main_track"],                 // optional flags
  "initial_phase": 0,                      // default 0 (first phase)
  "phases": [...]                          // array of phase definitions
}
```

| Flag | Effect |
|---|---|
| `"main_track"` | Marks primary track. Used by `completion_rule: "main_track_complete"` і scenario-level failure detection. Точно один track повинен мати цей flag. |
| `"loop_on_complete"` | Коли track reaches `$complete`, re-enter initial phase. Used для long-running monitors. |

## Phases

```json
{
  "name": "phase_a",                       // unique у track, snake_case
  "timeout_ms": 10000,                     // optional, overrides default_phase_timeout_ms
  "phase_resources": [],                   // optional
  "entry": [...],                          // optional — actions on phase entry
  "transitions": [...],                    // required — at least 1
  "exit": []                               // optional — actions on phase exit
}
```

### Entry і exit actions

Sequenced одна на tick. Engine runs `entry[0]` першим tick після phase
begins, `entry[1]` наступний tick, тощо. Після того як всі entry actions
finish, engine starts evaluating transitions.

Exit actions run коли transition fires, перед тим як target phase стає
active. Same per-tick sequencing.

```json
"entry": [
  {"action": "log",       "params": {"msg": "Phase A started"}},
  {"action": "set_state", "params": {"key": "test.led", "type": "bool", "value": true}}
],
"exit": [
  {"action": "set_state", "params": {"key": "test.led", "type": "bool", "value": false}}
]
```

Повний action catalog у [recipe-actions.md](recipe-actions.md).

### Transitions

```json
"transitions": [
  {"to": "phase_b", "when": {"time_elapsed_ms": 5000}},
  {"to": "$abort",  "when": {"state_key_gt": {"key": "test.fault", "value": 0}}}
]
```

Evaluated у declaration order кожен tick. Перша firing wins. Як тільки
transition fires, engine runs `exit` actions поточної phase, потім applies
target.

Special targets:
- `"$complete"` — track succeeds (state COMPLETED).
- `"$abort"` — track fails (state FAILED).
- `"phase_name"` — advance до specific phase.

### Transition kinds (`when` shapes)

`when` clause може бути:

**Time only:**
```json
{"time_elapsed_ms": 5000}
```
Fires коли `phase_elapsed_ms >= 5000`.

**Condition only:**
```json
{"state_key_gt": {"key": "equipment.air_temp", "value": 25}}
```
Fires коли condition evaluates true. Conditions read SharedState live.

**Time AND condition** (`time_and_cond` kind, encoded as composite):
```json
{"all_of": [
  {"time_elapsed_ms": 5000},
  {"state_key_gt": {"key": "equipment.air_temp", "value": 25}}
]}
```
Обидва повинні hold.

**Time OR condition** (`time_or_cond` kind):
```json
{"any_of": [
  {"time_elapsed_ms": 30000},
  {"state_key_eq": {"key": "user.skip", "value": true}}
]}
```
Будь-яке fire-ить.

**Unconditional** (omit `when`):
```json
{"to": "$complete"}
```
Fires immediately. Used після того як entry actions complete — natural
advance.

### Каталог conditions

Дивіться [recipe-actions.md → Conditions](recipe-actions.md#conditions)
для повного списку. Поширені:

| Condition | Purpose |
|---|---|
| `time_elapsed_ms` | phase_elapsed_ms ≥ threshold |
| `state_key_eq` / `_ne` | exact match / not-match |
| `state_key_gt` / `_lt` / `_ge` / `_le` | numeric comparisons |
| `state_key_in_range` | inclusive bounds check |
| `state_key_changed` | edge detection (Stage 1.5) |
| `all_of` / `any_of` / `not` | composite logic |

## Completion rules

Коли scenario сам reaches terminal state?

| Rule | Trigger |
|---|---|
| `all_tracks_complete` | Кожен track повинен бути COMPLETED. |
| `any_track_complete` | Перший track що COMPLETE wins; remaining tracks abort. |
| `main_track_complete` | Лише `main_track`-flagged track matters. |

Failure handling:
- Якщо main track reaches FAILED, scenario → FAILED regardless of
  completion_rule.
- Якщо `completion_rule: "main_track_complete"` і main fails →
  scenario FAILED.
- Інші tracks failing не обов'язково fail scenario (depends on rule).

## Global transitions

Evaluated FIRST кожен tick, перед per-track логікою. Used для "abort if
fault detected" patterns.

```json
"global_transitions": [
  {
    "to": "$abort",
    "when": {"state_key_eq": {"key": "test.fault", "value": true}},
    "priority": 255,
    "scope": "abort_scenario"
  }
]
```

| Field | Notes |
|---|---|
| `to` | Лише `"$abort"` meaningful (інші targets ignored). |
| `when` | Той самий syntax як у track transitions. |
| `priority` | 0-255. Higher fires first якщо multiple match. |
| `scope` | `"abort_scenario"` (all tracks fail) або `"abort_only_main_track"` (лише main fails, completion_rule decides). |

Use sparingly — global transitions — fast-path safety checks. Більшість
логіки живе у per-track transitions.

## Resources (scenario і phase scope)

Resources запобігають двом scenarios contolling той самий actuator
simultaneously.

**Scenario-scope** (claimed at `start`, released at scenario end):

```json
"resources": [
  {"resource_hash": "compressor", "exclusive": true}
]
```

Engine attempts atomically claim all resources при `start`. Якщо будь-який
held, `start` returns `RESOURCE_CONTENDED` і scenario stays LOADED.

**Phase-scope** (claimed at phase entry, released at exit):

```json
{
  "name": "active_phase",
  "phase_resources": [
    {"resource_hash": "compressor", "exclusive": true}
  ],
  "transitions": [...]
}
```

Якщо phase resource contended, track enters `WAITING_FOR_RESOURCE` state і
retries each tick. Phase timeout still applies.

Повні деталі: [scenario-engine/06_resource_arbitration.md](../03-framework-reference/scenario-engine/06_resource_arbitration.md).

## Cross-track синхронізація

Tracks тикають у declaration order кожен engine update. У межах одного
tick:
- Track 0 reads / writes state.
- Track 1 (later) reads fresh — including changes made by track 0 цей
  самий tick.

Це **tick-order semantics** (ADR-0003). Declare producer tracks before
consumer tracks. Не rely on snapshot consistency across tracks.

Worked pattern: `watcher` track waits для main's `phase_name` дійти до
value, потім completes.

```json
{
  "name": "watcher",
  "phases": [{
    "name": "watching",
    "transitions": [
      {"to": "$complete", "when": {"state_key_eq": {
        "key": "my_recipe.main_phase_name", "value": "phase_c"
      }}}
    ]
  }]
}
```

Engine writes mirror key `my_recipe.main_phase_name` після того як main
track ticks. Watcher (declared після main) reads це і може fire transition
on same tick.

## Параметри і dynamic values

Recipe authors можуть declare **overridable parameters** що оператор
adjust-ить перед starting scenario. Defined у `scenario` секції:

```json
"parameters": {
  "warm_setpoint": {"type": "float", "default": 30.0, "min": 10, "max": 50},
  "soak_duration_ms": {"type": "i32", "default": 30000, "min": 5000, "max": 300000}
}
```

Reference у actions через `@param:<name>`:

```json
{"action": "set_state", "params": {
  "key": "equipment.req_setpoint",
  "type": "f32",
  "value": "@param:warm_setpoint"
}}
```

Compiler resolves `@param:warm_setpoint` у param table index; engine
substitutes value (with optional runtime override) при phase execution
time.

Дивіться [scenario-engine/02_binary_format.md](../03-framework-reference/scenario-engine/02_binary_format.md)
для wire format.

## Build і loading

```bash
# 1. Build firmware (compile_scenario.py runs як pre-build)
idf.py build

# 2. .modr file appears у:
#    build/data/scenarios/<recipe_name>.modr
#    і bundled у LittleFS image

# 3. Flash:
idf.py -p COM15 flash

# 4. Load і run на device:
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/load \
     -d '{"path": "/data/scenarios/my_recipe.modr"}'
# → {"handle": 1}

curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/start \
     -d '{"handle": 1}'
```

WebUI exposes ті ж controls під auto-generated сторінкою рецепта
(використовуйте `visible_when` cards щоб показати controls лише коли
scenario loaded чи running).

## Workflow для написання нового recipe

1. **Sketch tracks і phases на папері.** Що runs concurrently? Який linear
   sequence у межах кожного track?
2. **Identify mirror keys.** Recipe name + track names + scenario/track
   state keys. Fit budget (recipe ≤ 12, track ≤ 8).
3. **Write `state` section** з mirror keys.
4. **Write `scenario.tracks`** з entry actions і transitions.
5. **Add `default_phase_timeout_ms`** і pick completion rule.
6. **Compile** — `python tools/compile_scenario.py modules/my_recipe`.
   Iterate на errors (compiler points на specific manifest lines).
7. **Flash і HIL test** через `/api/scenario/*` endpoints.
8. **Iterate** — adjust timings, add conditions, refine UI.

## Поширені помилки

**Missing mirror state declarations:** engine tries write `<recipe>.main_state`
але manifest's `state` section не declare it. Compile fails з clear
"mirror key X not declared in state".

**Recipe name too long:** budget — ≤ 12 chars total. `refrigeration_master`
— 20 — не fit-иться. Use shorter codes: `refrig_v1`.

**`$abort` з phase transition не aborting scenario:** track-level `$abort`
puts track у FAILED. Інші tracks continue. Щоб abort entire scenario, use
`global_transitions` з `scope: "abort_scenario"`.

**Забутий phase timeout:** якщо no transition fires і no `timeout_ms` set,
phase runs forever (well, until `scenario_timeout_max_ms`). Завжди ставте
timeouts навіть якщо conditions завжди повинні fire — defense у depth.

**Cross-track race condition assumption:** "Track A пише, track B читає —
вони happen simultaneously." Ні. Tracks тикають sequentially. Declare
producers before consumers; expect 1-tick delay якщо order reversed.

**Built-in conditions written like Python:** `{"state_key_eq": "test.x == 5"}`
не працює. Conditions — structured JSON, не expressions:
`{"state_key_eq": {"key": "test.x", "value": 5}}`.

## Що далі

- **[recipe-actions.md](recipe-actions.md)** — built-in actions (`log`,
  `set_state`, `wait_ms`) і custom registration.
- **[continuous-behaviors.md](continuous-behaviors.md)** — PID, hysteresis,
  ramp controllers що run-яться всередині phases.
- **[scenario-engine/04_state_machines.md](../03-framework-reference/scenario-engine/04_state_machines.md)** —
  per-track і scenario-level FSM diagrams.
- **[scenario-engine/05_synchronization.md](../03-framework-reference/scenario-engine/05_synchronization.md)** —
  cross-track sync deep dive з ADR-0003 rationale.
- **[scenario-engine/06_resource_arbitration.md](../03-framework-reference/scenario-engine/06_resource_arbitration.md)** —
  resource claim semantics.

## Worked example: `modules/abs_test`

Reference recipe shipped з фреймворком. Два паралельні tracks:
- `main`: phase_a → phase_b → phase_c → $complete (≈6 секунд).
- `watcher`: waits для `main.main_phase_name == "phase_c"` потім completes.

Demonstrates: entry actions, conditional transitions з `all_of` composite,
cross-track sync через mirror keys, `$complete` target.

Source: [`modules/abs_test/manifest.json`](../../../modules/abs_test/manifest.json).
