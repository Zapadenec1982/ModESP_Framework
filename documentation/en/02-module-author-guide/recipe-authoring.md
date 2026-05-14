# Recipe authoring

> 📖 **Українською:** [documentation/uk/02-module-author-guide/recipe-authoring.md](../../uk/02-module-author-guide/recipe-authoring.md)

A **recipe** is а time-bounded process — а cook program, batch reactor
cycle, irrigation sequence, defrost routine — encoded as а declarative
state machine у your manifest's `scenario` section. The build pipeline
compiles it to а binary `.modr` blob; the scenario engine executes it at
runtime. No C++ required.

This page covers how to structure scenarios — tracks, phases, transitions,
completion rules — і how the recipe-as-manifest pipeline lands on the
device. For built-in і custom actions used inside phases, see
[recipe-actions.md](recipe-actions.md). For PID / hysteresis / ramp
controllers that run inside phases, see
[continuous-behaviors.md](continuous-behaviors.md).

## Recipe = manifest із а `scenario` section

A recipe module has the same `manifest.json` shape as а service module,
except:

- `"module_type": "recipe"` tells the build pipeline to skip C++ code
  generation.
- The new `"scenario"` section contains the FSM declaration.
- No `CMakeLists.txt`, no `src/`, no `include/`. Just the manifest.

```
modules/my_recipe/
└── manifest.json          ← entire module
```

At build time, `tools/compile_scenario.py` reads the `scenario` section
і emits `data/scenarios/<recipe_name>.modr` (CRC-validated binary).
LittleFS bundles it; the engine loads at runtime через
`engine.load_path("/data/scenarios/<recipe_name>.modr")`.

## Mental model: tracks і phases

```
   Scenario
   ├── Track 1 ("main")
   │   ├── Phase A → Phase B → Phase C → $complete
   │   └── (entry actions, transitions, exit actions per phase)
   ├── Track 2 ("watcher")
   │   └── Phase X → $complete
   └── (optional) global transitions to $abort
```

**Tracks** are parallel state machines that run concurrently. Each track
has its own phase і progresses independently. Tracks communicate via
SharedState (writes from one track become readable by others next tick).

**Phases** are nodes у а track's FSM. A phase has:
- `entry` actions — run when entering the phase (sequenced one per tick).
- `transitions` — conditions для moving to another phase.
- `exit` actions — run when leaving (before applying transition target).
- Optional `timeout_ms` — auto-transition to next phase після time.
- Optional `phase_resources` — claim resources for the phase duration.

**Targets** for transitions:
- Another phase name (`"to": "phase_b"`) — advance within the same track.
- `"$complete"` — track succeeds (track state → COMPLETED).
- `"$abort"` — track fails (track state → FAILED, scenario may abort).

## Minimal recipe

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

Run: load via `POST /api/scenario/load`, start via
`POST /api/scenario/start`. The engine ticks the `main` track through
`warmup` (5 s) → `soak` (10 s) → `$complete`. Mirror keys
`my_recipe.main_phase_name` etc. update live.

## Required mirror state keys

The engine writes mirror keys to SharedState each tick. These MUST be
pre-declared у the recipe's `state` section, otherwise the build fails
із а cross-validation error.

Per scenario:

| Key | Type | Description |
|---|---|---|
| `<recipe>.scenario_state` | string | `"idle"`/`"loaded"`/`"running"`/`"paused"`/`"aborting"`/`"completed"`/`"failed"` |
| `<recipe>.scenario_elapsed_s` | int | Seconds since scenario start |

Per track:

| Key | Type | Description |
|---|---|---|
| `<recipe>.<track>_state` | string | Track FSM state |
| `<recipe>.<track>_phase_name` | string | Current phase name |
| `<recipe>.<track>_phase_idx` | int | Phase index у track (0-based) |
| `<recipe>.<track>_elapsed_s` | int | Seconds у current phase |

For а recipe із tracks `main` і `watcher`, declare 2 + (2 × 4) = 10 mirror
keys у your state section.

**Naming budget:** SharedState keys ≤ 32 chars. Recipe name ≤ 12 chars,
track name ≤ 8 chars. Example: `recipe_plov.watcher_phase_name` = 30 chars
— fits.

## Scenario-level fields

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
| `default_phase_timeout_ms` | yes | Default timeout per phase (mandatory per ADR-0007). Phases override individually. |
| `scenario_timeout_max_ms` | recommended | Hard cap on total scenario duration. Engine aborts if exceeded. |
| `completion_rule` | yes | `"all_tracks_complete"` / `"any_track_complete"` / `"main_track_complete"` — when does the scenario itself finish? |
| `resources` | optional | Scenario-scope resource claims (see below). |
| `global_transitions` | optional | Conditions evaluated FIRST every tick, ahead of per-track transitions. |
| `tracks` | yes | Array of track definitions (1-6). |

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
| `"main_track"` | Marks the primary track. Used by `completion_rule: "main_track_complete"` і scenario-level failure detection. Exactly one track should have це flag. |
| `"loop_on_complete"` | When the track reaches `$complete`, re-enter the initial phase. Used для long-running monitors. |

## Phases

```json
{
  "name": "phase_a",                       // unique within track, snake_case
  "timeout_ms": 10000,                     // optional, overrides default_phase_timeout_ms
  "phase_resources": [],                   // optional
  "entry": [...],                          // optional — actions on phase entry
  "transitions": [...],                    // required — at least 1
  "exit": []                               // optional — actions on phase exit
}
```

### Entry і exit actions

Sequenced one per tick. The engine runs `entry[0]` first tick after the
phase begins, `entry[1]` next tick, etc. After all entry actions finish,
the engine starts evaluating transitions.

Exit actions run when а transition fires, before the target phase becomes
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

Full action catalog у [recipe-actions.md](recipe-actions.md).

### Transitions

```json
"transitions": [
  {"to": "phase_b", "when": {"time_elapsed_ms": 5000}},
  {"to": "$abort",  "when": {"state_key_gt": {"key": "test.fault", "value": 0}}}
]
```

Evaluated у declaration order each tick. First firing wins. Once а
transition fires, the engine runs the current phase's `exit` actions, then
applies the target.

Special targets:
- `"$complete"` — track succeeds (state COMPLETED).
- `"$abort"` — track fails (state FAILED).
- `"phase_name"` — advance to specific phase.

### Transition kinds (`when` shapes)

The `when` clause can be:

**Time only:**
```json
{"time_elapsed_ms": 5000}
```
Fires when `phase_elapsed_ms >= 5000`.

**Condition only:**
```json
{"state_key_gt": {"key": "equipment.air_temp", "value": 25}}
```
Fires when condition evaluates true. Conditions read SharedState live.

**Time AND condition** (`time_and_cond` kind, encoded as composite):
```json
{"all_of": [
  {"time_elapsed_ms": 5000},
  {"state_key_gt": {"key": "equipment.air_temp", "value": 25}}
]}
```
Both must hold.

**Time OR condition** (`time_or_cond` kind):
```json
{"any_of": [
  {"time_elapsed_ms": 30000},
  {"state_key_eq": {"key": "user.skip", "value": true}}
]}
```
Either fires it.

**Unconditional** (omit `when`):
```json
{"to": "$complete"}
```
Fires immediately. Used after entry actions complete — natural advance.

### Conditions catalog

See [recipe-actions.md → Conditions](recipe-actions.md#conditions) for the
full list. Common ones:

| Condition | Purpose |
|---|---|
| `time_elapsed_ms` | phase_elapsed_ms ≥ threshold |
| `state_key_eq` / `_ne` | exact match / not-match |
| `state_key_gt` / `_lt` / `_ge` / `_le` | numeric comparisons |
| `state_key_in_range` | inclusive bounds check |
| `state_key_changed` | edge detection (Stage 1.5) |
| `all_of` / `any_of` / `not` | composite logic |

## Completion rules

When does the scenario itself reach а terminal state?

| Rule | Trigger |
|---|---|
| `all_tracks_complete` | Every track must be COMPLETED. |
| `any_track_complete` | First track to COMPLETE wins; remaining tracks abort. |
| `main_track_complete` | Only the `main_track`-flagged track matters. |

Failure handling:
- If the main track reaches FAILED, scenario → FAILED regardless of completion_rule.
- If `completion_rule: "main_track_complete"` and main fails → scenario FAILED.
- Other tracks failing doesn't necessarily fail the scenario (depends on rule).

## Global transitions

Evaluated FIRST every tick, before per-track logic. Used for "abort if
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
| `to` | Only `"$abort"` is meaningful (other targets ignored). |
| `when` | Same syntax as track transitions. |
| `priority` | 0-255. Higher fires first if multiple match. |
| `scope` | `"abort_scenario"` (all tracks fail) or `"abort_only_main_track"` (just main fails, completion_rule decides). |

Use sparingly — global transitions are fast-path safety checks. Most logic
lives у per-track transitions.

## Resources (scenario і phase scope)

Resources prevent two scenarios from controlling the same actuator simultaneously.

**Scenario-scope** (claimed at `start`, released at scenario end):

```json
"resources": [
  {"resource_hash": "compressor", "exclusive": true}
]
```

Engine attempts to atomically claim all resources at `start`. If any is
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

If а phase resource is contended, the track enters `WAITING_FOR_RESOURCE`
state і retries each tick. Phase timeout still applies.

Full details: [scenario-engine/06_resource_arbitration.md](../03-framework-reference/scenario-engine/06_resource_arbitration.md).

## Cross-track synchronization

Tracks tick у declaration order each engine update. Within а tick:
- Track 0 reads / writes state.
- Track 1 (later) reads fresh — including changes made by track 0 this same tick.

This is the **tick-order semantics** (ADR-0003). Declare producer tracks
before consumer tracks. Don't rely on snapshot consistency across tracks.

Worked pattern: `watcher` track waits для main's `phase_name` to reach
а value, then completes.

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

Engine writes mirror key `my_recipe.main_phase_name` after main track ticks.
Watcher (declared after main) reads it і can fire transition on same tick.

## Parameters і dynamic values

Recipe authors can declare **overridable parameters** that the operator
adjusts before starting а scenario. Defined у the `scenario` section:

```json
"parameters": {
  "warm_setpoint": {"type": "float", "default": 30.0, "min": 10, "max": 50},
  "soak_duration_ms": {"type": "i32", "default": 30000, "min": 5000, "max": 300000}
}
```

Reference within actions via `@param:<name>`:

```json
{"action": "set_state", "params": {
  "key": "equipment.req_setpoint",
  "type": "f32",
  "value": "@param:warm_setpoint"
}}
```

The compiler resolves `@param:warm_setpoint` to а param table index;
the engine substitutes the value (with optional runtime override) at
phase execution time.

See [scenario-engine/02_binary_format.md](../03-framework-reference/scenario-engine/02_binary_format.md)
for the wire format.

## Building і loading

```bash
# 1. Build firmware (compile_scenario.py runs as pre-build)
idf.py build

# 2. The .modr file appears у:
#    build/data/scenarios/<recipe_name>.modr
#    і bundled into the LittleFS image

# 3. Flash:
idf.py -p COM15 flash

# 4. Load і run на device:
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/load \
     -d '{"path": "/data/scenarios/my_recipe.modr"}'
# → {"handle": 1}

curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/start \
     -d '{"handle": 1}'
```

WebUI exposes the same controls under the recipe's auto-generated page
(use `visible_when` cards to show controls only когда scenario is loaded
or running).

## Workflow для writing а new recipe

1. **Sketch tracks і phases on paper.** What runs concurrently? What's
   the linear sequence within each track?
2. **Identify mirror keys.** Recipe name + track names + scenario/track
   state keys. Fit budget (recipe ≤ 12, track ≤ 8).
3. **Write `state` section** із the mirror keys.
4. **Write `scenario.tracks`** із entry actions і transitions.
5. **Add `default_phase_timeout_ms`** і pick а completion rule.
6. **Compile** — `python tools/compile_scenario.py modules/my_recipe`. Iterate
   on errors (the compiler points at specific manifest lines).
7. **Flash і HIL test** із the `/api/scenario/*` endpoints.
8. **Iterate** — adjust timings, add conditions, refine UI.

## Common mistakes

**Missing mirror state declarations:** engine tries to write `<recipe>.main_state`
але manifest's `state` section doesn't declare it. Compile fails із а
clear "mirror key X not declared у state".

**Recipe name too long:** budget is ≤ 12 chars total. `refrigeration_master` is 20 — won't fit. Use shorter codes: `refrig_v1`.

**`$abort` from а phase transition не aborting scenario:** track-level
`$abort` puts the track у FAILED. Other tracks continue. To abort the
entire scenario use а `global_transitions` із `scope: "abort_scenario"`.

**Forgetting phase timeout:** if no transition fires і no `timeout_ms` set,
phase runs forever (well, until `scenario_timeout_max_ms`). Always set
timeouts even if conditions should always fire — defense у depth.

**Cross-track race condition assumption:** "Track A writes, track B reads —
they happen simultaneously." No. Tracks tick sequentially. Declare
producers before consumers; expect 1-tick delay if order's reversed.

**Built-in conditions written like Python:** `{"state_key_eq": "test.x == 5"}`
won't work. Conditions are structured JSON, not expressions:
`{"state_key_eq": {"key": "test.x", "value": 5}}`.

## Next steps

- **[recipe-actions.md](recipe-actions.md)** — built-in actions (`log`,
  `set_state`, `wait_ms`) і custom registration.
- **[continuous-behaviors.md](continuous-behaviors.md)** — PID, hysteresis,
  ramp controllers що run inside phases.
- **[scenario-engine/04_state_machines.md](../03-framework-reference/scenario-engine/04_state_machines.md)** —
  per-track і scenario-level FSM diagrams.
- **[scenario-engine/05_synchronization.md](../03-framework-reference/scenario-engine/05_synchronization.md)** —
  cross-track sync deep dive із ADR-0003 rationale.
- **[scenario-engine/06_resource_arbitration.md](../03-framework-reference/scenario-engine/06_resource_arbitration.md)** —
  resource claim semantics.

## Worked example: `modules/abs_test`

Reference recipe shipped із the framework. Two parallel tracks:
- `main`: phase_a → phase_b → phase_c → $complete (≈6 seconds).
- `watcher`: waits for `main.main_phase_name == "phase_c"` then completes.

Demonstrates: entry actions, conditional transitions із `all_of`
composite, cross-track sync via mirror keys, `$complete` target.

Source: [`modules/abs_test/manifest.json`](../../../modules/abs_test/manifest.json).
