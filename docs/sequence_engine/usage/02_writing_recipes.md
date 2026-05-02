# Writing Recipes — Authoring Guide

**Status:** placeholder. Заповнюється incrementally на steps 2 (compile_scenario), 7 (built-in actions), 13 (sync semantics).

## Заповнюється

### Recipe structure

Manifest з `module_type: "recipe"` і опціональною `scenario` section. Standard sections (state, ui, mqtt) — як завжди для modules.

### Scenario section

- `default_phase_timeout_ms` (mandatory)
- `scenario_timeout_max_ms` (optional hard cap)
- `completion_rule` (`all_tracks_complete` | `any_track_complete` | `main_track_complete`)
- `resources` (declared exclusive/shared, by hash)
- `global_transitions` (applied to all tracks each tick, sorted by priority)
- `tracks` (1..6 track entries з phases, transitions)

### Phase structure

- `name`, `timeout_ms` (mandatory or fallback to default)
- `entry` actions (run once on phase entry)
- `exit` actions (run once before transition fires)
- `transitions` array (evaluated each tick after entry actions complete)
- `continuous` array (active behaviors during phase, MVP: 0 built-ins)

### Built-in actions

- `log` — log diagnostic message
- `set_state` — write SharedState key
- `wait_ms` — pending until time elapsed (returns PENDING)

### Built-in conditions

- `time_elapsed_ms` — phase-relative
- `state_key_eq` / `_ne` / `_lt` / `_gt` / `_le` / `_ge`
- `state_key_in_range`
- `state_key_changed` (edge detect)
- `time_of_day_eq` (HH:MM matching, requires SNTP)
- `all_of` / `any_of` / `not` (boolean composition)

### Cross-track sync (tick-order)

- Engine ticks tracks у declaration order
- Producer track has lower index than consumer track
- All writes from prior tracks visible на same tick
- Worked examples з two-track and three-track recipes

### Parameters і overrides

- Recipe params з `flags.overridable_at_start = true` accept runtime override через `engine.start(h, overrides)`
- Min/max constraints validated at compile time AND start time

### Mirror state keys

Engine writes `recipe_<name>.scenario_state`, `recipe_<name>.<track>_phase_name`, etc. Recipe manifest MUST declare each key engine will write — compile-time cross-validation.

### Resource arbitration

If recipe controls hardware actuators, declare resources у scenario.resources. Use `set_state` actions to disable conflicting business modules on phase 0 entry; re-enable у abort handler exit. Engine не automatically restores.

### Anti-patterns

- Same-tick mutual writes (race)
- Circular cross-track waits
- Forgetting abort handler re-enable
- Phases без timeout (now compile error — defaults applied)
