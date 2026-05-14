# Writing Recipes — Authoring Guide

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/usage/02_writing_recipes.md](../../../../uk/03-framework-reference/scenario-engine/usage/02_writing_recipes.md)

**Status:** Skeleton (Q4 in the Step 2b cleanup). Full content together with examples
will be filled in alongside the Stage 1 final integration (Step 16) and Stage 2 (WebUI editor).

---

## Audience

For C++ business module authors and domain integrators. Recipes describe
time-dependent algorithms (multi-track, conditional, with parameter overrides)
that are executed by the `SequenceEngine`.

## Quick start

A recipe is a ModESP module with `module_type: "recipe"` plus a scenario section.

```jsonc
{
  "manifest_version": 1,
  "module": "recipe_xxx",         // ≤ 12 chars (32-char SharedState budget)
  "module_type": "recipe",
  "version": "1.0.0",
  "priority": 5,
  "state": { ... },               // mirror keys the engine writes at runtime
  "scenario": { ... }             // NEW section — compile_scenario.py emits .modr
}
```

## Scenario structure

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

## Tracks and phases

Each track has its own phase sequence:

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

| Action | Parameters | Description |
|---|---|---|
| `log` | `msg` (string) | Log diagnostic message via ESP_LOG_INFO |
| `set_state` | `key` (string), `type` (`i32`/`f32`/`bool`), `value` | Write SharedState key |
| `wait_ms` | `ms` (i32) | Wait the specified ms (returns PENDING until elapsed) |

## Built-in conditions

Composite (nested):
- `all_of: [<cond>, ...]` — all true
- `any_of: [<cond>, ...]` — at least one true
- `not: <cond>` — inversion

Scalar:
- `time_elapsed_ms: int` — ms since phase entry
- `state_key_eq` / `_ne` / `_lt` / `_gt` / `_le` / `_ge` (`{key, value}`)
- `state_key_in_range: {key, min, max}`
- `state_key_changed: {key}` — edge detect
- `time_of_day_eq: {hh, mm}` — wall-clock match (requires SNTP)

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
- `$complete` — complete this track
- `$abort` — abort the entire scenario (via completion_rule)

## Recipe parameters (`@param:`)

Parameters are declared at the scenario level with default values. The WebUI editor
(Stage 2) will use this for form rendering. Compile-time substitution —
the engine sees only literals.

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

`overridable: true` is a hint for the WebUI editor (show it in the edit form).
The engine does not see parameters separately — only literal values after substitution.

## Mirror state keys (cross-validation)

The engine automatically writes the following state keys (for a recipe with 2 tracks "a", "b"):

```
recipe_X.scenario_state    (string)    "idle" | "running" | "completed" | ...
recipe_X.scenario_elapsed_s (int)
recipe_X.last_error        (int)
recipe_X.a_state           (string)
recipe_X.a_phase_name      (string)
recipe_X.a_phase_idx       (int)
recipe_X.a_elapsed_s       (int)
recipe_X.b_state           (string)
... (similar for b)
```

**All of these keys MUST be declared in `manifest.state`** with the correct type.
Mismatch → compile error E0401 (missing) or E0403 (wrong type).

```jsonc
"state": {
  "recipe_X.scenario_state":     {"type": "string", "access": "read"},
  "recipe_X.scenario_elapsed_s": {"type": "int",    "access": "read"},
  "recipe_X.last_error":         {"type": "int",    "access": "read"},
  "recipe_X.a_state":            {"type": "string", "access": "read"},
  "recipe_X.a_phase_name":       {"type": "string", "access": "read"},
  "recipe_X.a_phase_idx":        {"type": "int",    "access": "read"},
  "recipe_X.a_elapsed_s":        {"type": "int",    "access": "read"}
  // ... likewise for all tracks
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

Evaluated each tick across all tracks before per-phase evaluation. Sorted by priority
descending. Always abort-target (the binary format has no target_phase field).

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

`--strict` elevates W0220 (unknown action) and W0230 (unknown ContinuousBehavior)
to errors. Use in CI.

## Inspect compiled binary

```bash
python tools/dump_modr.py data/scenarios/recipe_X.modr
python tools/dump_modr.py --hex recipe_X.modr   # +raw bytes
```

## Cross-references

- [`02_binary_format.md`](../02_binary_format.md) — `.modr` byte layout
- [`05_synchronization.md`](../05_synchronization.md) — tick-order cross-track sync
- [`09_manifest_integration.md`](../09_manifest_integration.md) — full error code catalog, build pipeline
- [`10_error_model.md`](../10_error_model.md) — error code descriptions
- ADR-0004 — recipe-as-manifest rationale
- ADR-0005 — ISA-88 §5.3 resource arbitration

---

**TODO for the full content (to be filled in alongside Stage 1 Step 16 and Stage 2):**
- Worked examples (3-phase minimal, dual-track sync, parameterized greenhouse)
- Common patterns (debounce, hysteresis, timeout watchdog)
- Anti-patterns (circular waits, racing tracks, leaky resources)
- Troubleshooting common compiler errors
- Testing recipes (unit tests, HIL test setup)
- Migration from a manual state machine to a recipe
