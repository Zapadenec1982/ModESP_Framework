# Registering Custom Actions — Domain Module Guide

**Status:** placeholder. Заповнюється у Step 5 (ActionRegistry implementation).

## Заповнюється

### When to register actions

Built-in actions (`log`, `set_state`, `wait_ms`) sufficient для simple recipes але limited:
- No HAL access (engine is hardware-agnostic by design)
- No domain logic (PID, hysteresis, ramp — must come from domain modules)

Якщо ваш domain module хоче бути used у scenarios — register actions.

### How to register

Code example:

```cpp
// у multicooker_module.cpp on_init():
auto& reg = modesp::sequence::ActionRegistry::instance();
reg.register_action({
    .hash = djb2_hash16("multicooker.set_target_temp"),
    .name = "multicooker.set_target_temp",
    .fn = &MulticookerModule::action_set_target_temp,
    .param_min = 1,
    .param_max = 1
});
```

### Action function signature

```cpp
static ActionStatus action_set_target_temp(ActionContext& ctx) {
    if (ctx.param_count != 1) return ActionStatus::FAILED_ABORT;
    const auto& p = ctx.params[0];
    if (p.type != /* f32 */ 1) return ActionStatus::FAILED_ABORT;
    float temp = p.v.f;
    if (temp < 0.0f || temp > 200.0f) return ActionStatus::FAILED_RECOVERABLE;
    ctx.state->set("multicooker.req_target_temp", temp);
    return ActionStatus::OK;
}
```

### Registering conditions

Same shape, separate registry method. Returns `OK` (true) or `FAILED_*` (false).

### Hash collision handling

`tools/known_actions.json` maintained manually (MVP) listing all registered action names. Compiler validates uniqueness of djb2 low-16 hashes. On collision: rename one of the actions.

### Action context fields

- `state` — SharedState pointer (read/write)
- `params` + `param_count` — parameters from recipe
- `string_pool` + `string_pool_size` — resolve `s_idx` parameter values
- `phase_elapsed_ms` / `scenario_elapsed_ms` — timing
- `phase_idx` / `track` / `handle` — diagnostic
- `recipe_name` / `track_name` — for logging

### Returning ActionStatus

Per Q12 action failure policy machine:
- `OK` — engine continues
- `PENDING` — re-call next tick (1s default escalation)
- `FAILED_RECOVERABLE` — log, skip remaining entry/exit, continue with transitions
- `FAILED_ABORT` — track → TRACK_FAILED

### Worked example

Full multicooker module з registered actions. Skeleton C++ code that compiles.
