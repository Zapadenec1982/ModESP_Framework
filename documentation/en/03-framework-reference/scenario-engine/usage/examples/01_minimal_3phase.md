# Example 01: Minimal 3-Phase Single-Track Recipe

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/usage/examples/01_minimal_3phase.md](../../../../../uk/03-framework-reference/scenario-engine/usage/examples/01_minimal_3phase.md)

The simplest non-trivial recipe — a single track marching through three phases
with timed transitions. Demonstrates the core authoring pattern without cross-track
complexity.

## Recipe (`modules/min_3p/manifest.json`)

```jsonc
{
  "manifest_version": 1,
  "module": "min_3p",
  "module_type": "recipe",
  "version": "1.0.0",
  "priority": 5,
  "description": "Three-phase warmup → soak → cooldown demo",

  "state": {
    "min_3p.scenario_state":     {"type": "string", "access": "read"},
    "min_3p.scenario_elapsed_s": {"type": "int",    "access": "read"},
    "min_3p.last_error":         {"type": "int",    "access": "read"},
    "min_3p.main_state":         {"type": "string", "access": "read"},
    "min_3p.main_phase_name":    {"type": "string", "access": "read"},
    "min_3p.main_phase_idx":     {"type": "int",    "access": "read"},
    "min_3p.main_elapsed_s":     {"type": "int",    "access": "read"}
  },

  "ui": {
    "page": "Demo",
    "icon": "play",
    "cards": [{
      "title": "Mini 3-phase demo",
      "layout": "single",
      "visible_when": {"min_3p.scenario_state": ["running", "paused", "completed"]},
      "widgets": [
        {"key": "min_3p.scenario_state",  "widget": "value"},
        {"key": "min_3p.main_phase_name", "widget": "value"}
      ]
    }]
  },

  "scenario": {
    "default_phase_timeout_ms": 60000,
    "completion_rule": "all_tracks_complete",
    "tracks": [{
      "name": "main",
      "flags": ["main_track"],
      "phases": [
        {
          "name": "warmup",
          "timeout_ms": 5000,
          "entry": [
            {"action": "log",       "params": {"msg": "warmup begins"}},
            {"action": "set_state", "params": {"key": "demo.heater", "type": "bool", "value": true}}
          ],
          "transitions": [
            {"to": "soak", "when": {"time_elapsed_ms": 2000}}
          ]
        },
        {
          "name": "soak",
          "timeout_ms": 10000,
          "entry": [
            {"action": "log",       "params": {"msg": "soak phase"}},
            {"action": "set_state", "params": {"key": "demo.fan", "type": "bool", "value": true}}
          ],
          "transitions": [
            {"to": "cooldown", "when": {"time_elapsed_ms": 3000}}
          ]
        },
        {
          "name": "cooldown",
          "timeout_ms": 5000,
          "entry": [
            {"action": "log",       "params": {"msg": "cooldown — heater off"}},
            {"action": "set_state", "params": {"key": "demo.heater", "type": "bool", "value": false}},
            {"action": "set_state", "params": {"key": "demo.fan",    "type": "bool", "value": false}}
          ],
          "transitions": [
            {"to": "$complete", "when": {"time_elapsed_ms": 2000}}
          ]
        }
      ]
    }]
  }
}
```

## Walkthrough

### Phase progression

```
warmup (2s) ─time_elapsed_ms→ soak (3s) ─time_elapsed_ms→ cooldown (2s) ─→ $complete
```

Total scenario duration: ~7 seconds. The scenario `default_phase_timeout_ms`
(60s) is a safety upper bound; the per-phase `timeout_ms` (5s / 10s / 5s)
are tighter limits — if transitions don't fire by then, the phase auto-fails.

### What the engine writes to SharedState

The engine writes mirror keys after each phase entry:

| Key | Value during warmup | during soak | during cooldown |
|-----|-------|------|-------|
| `min_3p.scenario_state` | "running" | "running" | "running" |
| `min_3p.main_phase_name` | "warmup" | "soak" | "cooldown" |
| `min_3p.main_phase_idx` | 0 | 1 | 2 |
| `min_3p.main_elapsed_s` | 0..2 | 0..3 | 0..2 |

The recipe's own `entry` actions write `demo.heater`, `demo.fan` — these are
NOT engine mirror keys; the recipe author declares them in other modules
(or omits them, accepting last-write-wins with business modules).

### Compile

```bash
python tools/compile_scenario.py --recipe modules/min_3p/manifest.json \
                                 --output data/scenarios/min_3p.modr
```

Result: a small `.modr` (~250 bytes for this recipe). Verified by
`tools/tests/test_compile_scenario.py` golden round-trip.

### Run

```cpp
auto h = engine.load_path("/data/scenarios/min_3p.modr");
engine.start(h);
// ~7 seconds later: engine.state(h) == COMPLETED
```

## Common variations

**Manual trigger instead of timer:** replace `time_elapsed_ms` with `state_key_eq`
that reads a UI button state:

```jsonc
{"to": "soak", "when": {"state_key_eq": {"key": "ui.start_btn", "value": true}}}
```

**Both timer AND button:** use `all_of`:

```jsonc
{"to": "soak", "when": {"all_of": [
  {"time_elapsed_ms": 2000},
  {"state_key_eq": {"key": "ui.confirm", "value": true}}
]}}
```

**Either timer OR button:** use `any_of` — returns true when either fires:

```jsonc
{"to": "soak", "when": {"any_of": [
  {"time_elapsed_ms": 30000},
  {"state_key_eq": {"key": "ui.skip_btn", "value": true}}
]}}
```

## See also

- [02_dual_track_sync.md](02_dual_track_sync.md) — multi-track example with
  cross-track synchronization
- [02_writing_recipes.md](../02_writing_recipes.md) — full action and
  condition vocabulary
