# modesp_scenario — public API headers

This directory holds the public C++ API surface for the scenario engine.
During Phase 0 (scaffold), it is intentionally empty.

**Phase 1+ planned headers:**

| Header | Purpose |
|---|---|
| `engine.h` | `class Engine : public BaseModule` — multi-instance scenario runtime. |
| `engine_error.h` | `enum class EngineError`. |
| `runtime_types.h` | `SequenceRuntime`, `TrackRuntime`, state enums. |
| `action_param.h` | POD: `ActionParam`, `ActionStatus`, `ActionContext`. |
| `action_registry.h` | `class ActionRegistry` (engine-injected, no singleton). |
| `action_context.h` | Engine-passed call context. |
| `continuous_behavior.h` | `class ContinuousBehavior` interface. |
| `continuous_registry.h` | `class ContinuousRegistry`. |
| `i_state_backend.h` | 2-virtual interface: `get_raw`/`set_raw` + templated wrappers. |
| `i_engine_observer.h` | 3-hook observer: `on_scenario_started`, `on_phase_entered`, `on_scenario_terminal`. |
| `builtin_actions.h` | `register_builtin_actions(ActionRegistry&)`. |
| `continuous_primitives.h` | `register_primitives(ContinuousRegistry&)`. |
| `nvs_observer.h` | `class NvsObserver : public IEngineObserver` з throttle policy. |
| `modr_format.h` | Wire format definitions (unchanged from old `modesp_sequence`). |
