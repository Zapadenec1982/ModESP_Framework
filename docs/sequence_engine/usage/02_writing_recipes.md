# Writing Recipes — Authoring Guide

**Status:** Skeleton (Q4 у Step 2b cleanup). Повний content разом з examples
буде заповнено разом із Stage 1 final integration (Step 16) та Stage 2 (WebUI editor).

---

## Audience

Для C++ business module authors і domain integrators. Recipes описують
часозалежні алгоритми (multi-track, conditional, з parameter overrides)
що виконуються двигуном `SequenceEngine`.

## Quick start

Recipe = ModESP module з `module_type: "recipe"` + scenario секцією.

```jsonc
{
  "manifest_version": 1,
  "module": "recipe_xxx",         // ≤ 12 chars (32-char SharedState budget)
  "module_type": "recipe",
  "version": "1.0.0",
  "priority": 5,
  "state": { ... },               // mirror keys engine writes runtime
  "scenario": { ... }             // NEW section — compile_scenario.py emits .modr
}
```

## Scenario структура

```jsonc
"scenario": {
  "default_phase_timeout_ms": 60000,
  "scenario_timeout_max_ms": 0,        // 0 = unlimited
  "completion_rule": "all_tracks_complete",  // | "any_track_complete" | "main_track_complete"
  "params": { ... },                   // optional: recipe-level configurable values
  "resources": [ ... ],                // optional: scenario-scope resources (claim at start)
  "global_transitions": [ ... ],       // optional: priority-sorted abort triggers
  "tracks": [ ... ]                    // 1..6 parallel tracks
}
```

## Tracks і phases

Кожен track має власну послідовність phases:

```jsonc
"tracks": [
  {
    "name": "main",
    "flags": ["main_track"],            // optional: ["main_track", "loop_on_complete"]
    "phases": [
      {
        "name": "warmup",
        "timeout_ms": 10000,             // 0 → use default_phase_timeout_ms
        "entry": [ ...actions... ],
        "exit": [ ...actions... ],
        "transitions": [ ...transitions... ],
        "continuous": [],                // ContinuousBehaviors active during phase
        "phase_resources": []            // phase-scope resource claims
      }
    ]
  }
]
```

## Built-in actions

| Дія | Параметри | Опис |
|---|---|---|
| `log` | `msg` (string) | Log diagnostic message via ESP_LOG_INFO |
| `set_state` | `key` (string), `type` (`i32`/`f32`/`bool`), `value` | Write SharedState key |
| `wait_ms` | `ms` (i32) | Wait specified ms (returns PENDING до елapsed) |

## Built-in conditions

Композиційні (вкладені):
- `all_of: [<cond>, ...]` — всі правдиві
- `any_of: [<cond>, ...]` — хоча б один правдивий
- `not: <cond>` — інверсія

Скалярні:
- `time_elapsed_ms: int` — ms з phase entry
- `state_key_eq` / `_ne` / `_lt` / `_gt` / `_le` / `_ge` (`{key, value}`)
- `state_key_in_range: {key, min, max}`
- `state_key_changed: {key}` — edge detect
- `time_of_day_eq: {hh, mm}` — wall-clock match (potreba SNTP)

Maximum nesting depth: 16 (DoS guard).

## Transitions

```jsonc
"transitions": [
  {"to": "$complete"},                                 // unconditional
  {"to": "next_phase", "when": {"time_elapsed_ms": 5000}},
  {"to": "$abort", "when": {"all_of": [
      {"state_key_eq": {"key": "safety.fault", "value": true}},
      {"time_elapsed_ms": 100}
  ]}}
]
```

Special targets:
- `$complete` — завершити цей track
- `$abort` — abort усього scenario (через completion_rule)

## Recipe parameters (`@param:`)

Параметри декларуються на scenario level з default values. WebUI editor
(Stage 2) використає це для form rendering. Compile-time substitution —
двигун бачить тільки літерали.

```jsonc
"scenario": {
  "params": {
    "moisture_low": {"type": "f32", "default": 40.0, "min": 0, "max": 100, "overridable": true},
    "watering_duration_ms": {"type": "i32", "default": 600000, "min": 60000, "max": 3600000}
  },
  "tracks": [{
    "phases": [{
      "transitions": [
        {"to": "watering", "when": {
          "state_key_lt": {"key": "sensor.moisture", "value": "@param:moisture_low"}
        }}
      ]
    }]
  }]
}
```

`overridable: true` — підказка для WebUI editor (показати в формі редагування).
Engine не бачить параметрів окремо — лише literal values після substitution.

## Mirror state keys (cross-validation)

Двигун автоматично пише такі state keys (per recipe з 2 tracks "a", "b"):

```
recipe_X.scenario_state    (string)    "idle" | "running" | "completed" | ...
recipe_X.scenario_elapsed_s (int)
recipe_X.last_error        (int)
recipe_X.a_state           (string)
recipe_X.a_phase_name      (string)
recipe_X.a_phase_idx       (int)
recipe_X.a_elapsed_s       (int)
recipe_X.b_state           (string)
... (similar для b)
```

**Усі ці keys MUST бути declared у `manifest.state`** з правильним type.
Mismatch → compile error E0401 (missing) або E0403 (wrong type).

```jsonc
"state": {
  "recipe_X.scenario_state":     {"type": "string", "access": "read"},
  "recipe_X.scenario_elapsed_s": {"type": "int",    "access": "read"},
  "recipe_X.last_error":         {"type": "int",    "access": "read"},
  "recipe_X.a_state":            {"type": "string", "access": "read"},
  "recipe_X.a_phase_name":       {"type": "string", "access": "read"},
  "recipe_X.a_phase_idx":        {"type": "int",    "access": "read"},
  "recipe_X.a_elapsed_s":        {"type": "int",    "access": "read"}
  // ... аналогічно для всіх tracks
}
```

## Naming budget

- Recipe (module) name ≤ 12 chars
- Track name ≤ 8 chars
- Phase name ≤ 16 chars
- Total mirror key ≤ 32 chars (SharedState `MODESP_MAX_KEY_LENGTH`)

## Resources (ISA-88 §5.3)

Scenario-scope (claimed at start):
```jsonc
"resources": [
  {"resource": "equipment.heater", "exclusive": true}
]
```

Phase-scope (claimed at phase entry, released at exit):
```jsonc
"phases": [{
  "name": "watering",
  "phase_resources": [{"resource": "equipment.pump", "exclusive": true}]
}]
```

## Global transitions

Evaluated each tick across усіх tracks before per-phase. Sorted by priority
descending. Always abort-target (binary format не has target_phase field).

```jsonc
"global_transitions": [
  {"when": {"state_key_eq": {"key": "safety.fault", "value": true}},
   "priority": 255, "scope": "abort_scenario"},
  {"when": {"state_key_eq": {"key": "ui.user_abort", "value": true}},
   "priority": 200}
]
```

## Compile and validate

```bash
# Single recipe
python tools/compile_scenario.py --recipe modules/recipe_X/manifest.json --output recipe_X.modr

# All recipe modules
python tools/compile_scenario.py --modules-dir modules --output-dir data/scenarios

# Strict mode (CI)
python tools/compile_scenario.py --modules-dir modules --output-dir data/scenarios --strict
```

`--strict` elevates W0220 (unknown action) і W0230 (unknown ContinuousBehavior)
to errors. Use у CI.

## Inspect compiled binary

```bash
python tools/dump_modr.py data/scenarios/recipe_X.modr
python tools/dump_modr.py --hex recipe_X.modr   # +raw bytes
```

## Cross-references

- [`02_binary_format.md`](../02_binary_format.md) — `.modr` byte layout
- [`05_synchronization.md`](../05_synchronization.md) — tick-order cross-track sync
- [`09_manifest_integration.md`](../09_manifest_integration.md) — повна error code catalog, build pipeline
- [`10_error_model.md`](../10_error_model.md) — error code descriptions
- ADR-0004 — recipe-as-manifest rationale
- ADR-0005 — ISA-88 §5.3 resource arbitration

---

**TODO для повного content (заповнюється разом з Stage 1 Step 16 і Stage 2):**
- Worked examples (3-phase minimal, dual-track sync, parameterized greenhouse)
- Common patterns (debounce, hysteresis, timeout watchdog)
- Anti-patterns (circular waits, racing tracks, leaky resources)
- Troubleshooting common compiler errors
- Testing recipes (unit tests, HIL test setup)
- Migration з manual state machine to recipe
