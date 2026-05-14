# `modesp_scenario` — scenario engine

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_scenario.md](../../../en/03-framework-reference/components/modesp_scenario.md)

`modesp_scenario` — runtime engine що executes scenario recipes. Бере
бінарний `.modr` blob (compiled by `tools/compile_scenario.py` з
`scenario` секції маніфесту), валідує і ticks resulting phase machines
кожен engine tick. Supports до 4 concurrent independent recipes, кожен
з до 6 parallel tracks, із built-in actions, conditions, і continuous
behaviors.

Ця сторінка — high-level summary; full deep-dive у migrated
[scenario-engine/](../scenario-engine/) subdirectory (10 architectural
docs + 8 ADRs + usage guides).

REQUIRES: `modesp_core`, `marcel-cd__etlcpp`. Optional `nvs_flash` для
persistence observer.

## Public API surface

```cpp
#include "modesp/scenario/engine.h"

namespace modesp::scenario {

class Engine : public modesp::BaseModule {
public:
    Engine(IStateBackend& state,
           ActionRegistry& actions,
           ContinuousRegistry& continuous,
           etl::span<IEngineObserver*> observers = {});

    SequenceHandle load_buffer(const uint8_t* data, size_t size);
    SequenceHandle load_path(const char* path);
    EngineError    unload(SequenceHandle h);

    EngineError start(SequenceHandle h);
    EngineError pause(SequenceHandle h);
    EngineError resume(SequenceHandle h);
    EngineError abort(SequenceHandle h, uint8_t reason_code = 0);
    EngineError try_recover(SequenceHandle h, NvsObserver& nvs);

    // Diagnostic accessors
    SequenceRuntime::State state(SequenceHandle h) const;
    uint32_t scenario_elapsed_ms(SequenceHandle h) const;
    uint8_t  track_count(SequenceHandle h) const;
    TrackRuntime::State track_state(SequenceHandle h, TrackIdx t) const;
    uint8_t  track_phase_idx(SequenceHandle h, TrackIdx t) const;
    uint32_t track_phase_elapsed_ms(SequenceHandle h, TrackIdx t) const;
    uint8_t  active_count() const;
};

}
```

Engine constructed з dependency injection: state backend (типово
`SharedStateBackend` adapter wrapping global SharedState), action і
continuous registries (caller-owned, populated із built-ins + custom
registrations), і span observers (`NvsObserver` для persistence, плюс
any custom).

Init priority: HIGH (1), у Phase 2 — runs після WiFi і HAL але перед
business modules що можуть load recipes.

## Component layout

```
components/modesp_scenario/include/modesp/scenario/
├── engine.h                ← Public engine class
├── action_registry.h       ← ActionRegistry (custom actions + conditions)
├── continuous_behavior.h   ← ContinuousRegistry + ContinuousBehavior base
├── continuous_primitives.h ← PID, Hysteresis, Ramp built-ins
├── builtin_actions.h       ← register_builtins() entry point
├── action_param.h          ← POD types (ActionParam, ActionContext, ActionStatus)
├── runtime_types.h         ← SequenceRuntime, TrackRuntime, state enums
├── engine_error.h          ← EngineError enum
├── i_state_backend.h       ← IStateBackend interface (DI seam)
├── i_engine_observer.h     ← IEngineObserver interface
├── nvs_observer.h          ← NvsObserver impl (NVS persistence)
├── nvs_token.h             ← 96-byte token format
├── modr_format.h           ← Binary .modr wire format
├── modr_loader.h           ← Validator i LoadedScenario view
└── resource_arbiter.h      ← ISA-88 §5.3 resource claims

components/modesp_scenario/private/
├── instance.h              ← Per-instance FSM
├── track.h                 ← Per-track FSM
└── mirror.h                ← Mirror keys publisher (direct call)

components/modesp_scenario/src/
├── core/      engine.cpp, instance.cpp, track.cpp, modr_loader.cpp
├── actions/   action_registry.cpp, builtin_actions.cpp
├── continuous/continuous_registry.cpp, continuous_primitives.cpp
├── arbiter/   resource_arbiter.cpp
└── observers/ mirror.cpp, nvs_observer.cpp, nvs_token.cpp
```

## Архітектура у одному параграфі

Engine — multi-instance scenario runner. Кожен slot owns один loaded
`.modr` blob і його runtime state (`SequenceRuntime`). Per engine tick:
кожен loaded slot's instance ticked → instance ticks кожен track у
declaration order → track evaluates current phase's transitions і entry
actions. Actions і conditions resolve через uint16 djb2 hash through
injected ActionRegistry. Mirror keys (`<recipe>.scenario_state`,
`<recipe>.<track>_phase_name`, тощо) пишуться до SharedState кожен tick
через direct call. NVS persistence handled observer що listens до
scenario start, phase entry, і terminal events.

## Built-in actions і conditions

Provided через `register_builtin_actions(registry)`:

**Actions (3):** `log`, `set_state`, `wait_ms`.

**Conditions (10 leaf + 3 composite):** `time_elapsed_ms`, `state_key_eq`/
`_ne`/`_lt`/`_gt`/`_le`/`_ge`, `state_key_in_range`, `state_key_changed`,
`time_of_day_eq`, `all_of`, `any_of`, `not`.

Див. [02-module-author-guide/recipe-actions.md](../../02-module-author-guide/recipe-actions.md).

## Continuous primitives

Provided через `register_primitives(continuous_registry)`:

- `pid` — parallel-form PID з anti-windup і derivative-on-measurement.
- `hysteresis` — bang-bang з symmetric deadband.
- `ramp` — linear interpolation з start до end value over duration.

Див. [02-module-author-guide/continuous-behaviors.md](../../02-module-author-guide/continuous-behaviors.md).

## Wiring у main.cpp

```cpp
#include "modesp/scenario/engine.h"
#include "modesp/scenario/builtin_actions.h"
#include "modesp/scenario/continuous_primitives.h"
#include "modesp/scenario/nvs_observer.h"
#include "shared_state_backend.h"  // local adapter (main/)

static modesp::scenario::ActionRegistry     scenario_actions;
static modesp::scenario::ContinuousRegistry scenario_continuous;

// У app_main, після того як app.state() available:
static SharedStateBackend                shared_state_backend{app.state()};
static modesp::scenario::NvsObserver     scenario_nvs_obs{
    seq_nvs_write, seq_nvs_read, nullptr};
static modesp::scenario::IEngineObserver* scenario_obs_list[] = { &scenario_nvs_obs };
static modesp::scenario::Engine          scenario_engine{
    shared_state_backend,
    scenario_actions,
    scenario_continuous,
    scenario_obs_list};

scenario_nvs_obs.bind_engine(scenario_engine);
modesp::scenario::builtins::register_builtins(scenario_actions);
modesp::scenario::primitives::register_primitives(scenario_continuous);

app.modules().register_module(scenario_engine);
http_service.set_scenario_engine(&scenario_engine);
```

## State keys (engine-level)

| Key | Notes |
|---|---|
| `scenario.engine_active_count` | Кількість running scenarios. |
| `scenario.engine_active_tracks` | Total active tracks across усіх scenarios. |
| `scenario.engine_recovery_pending` | true якщо recovered scenario awaits manual resume. |

Recipe-specific mirror keys (declared у кожному recipe's manifest):
`<recipe>.scenario_state`, `<recipe>.<track>_phase_name`, тощо.

## HTTP API

Engine endpoints у `modesp_net`:

| Endpoint | Purpose |
|---|---|
| `GET /api/scenario/list` | Усі loaded scenarios + states. |
| `GET /api/scenario/info?handle=N` | Per-scenario details. |
| `POST /api/scenario/load` | Load `.modr` by path. |
| `POST /api/scenario/start` / `pause` / `resume` / `abort` / `unload` | Lifecycle. |

Full HIL test: [test_hil_scenario.py](../../../../tools/tests/test_hil_scenario.py).

## Memory і resources

| Item | Cost |
|---|---|
| Engine з 4 slots | ~16 KB (slots include `.modr` buffer до MODR_MAX_SIZE = 16 KB each) |
| ActionRegistry із 64-entry maps | ~6 KB |
| ContinuousRegistry із 32 entries | ~2 KB |
| ResourceArbiter із 32-entry map | ~1 KB |
| NvsObserver | ~256 bytes |

Default MAX_SEQUENCES=2 (Kconfig); кожен slot pre-allocates 16 KB buffer.
Bump до 4 для production deployments з multiple recipes; cost ~64 KB RAM.

## Deeper dives

Migrated scenario engine documentation живе у
[scenario-engine/](../scenario-engine/):

- `00_overview.md` — що це робить, для кого.
- `01_architecture.md` — internal architecture з diagrams.
- `02_binary_format.md` — `.modr` byte format з tables.
- `03_api_reference.md` — full C++ API surface.
- `04_state_machines.md` — scenario + track FSMs з diagrams.
- `05_synchronization.md` — tick-order cross-track sync.
- `06_resource_arbitration.md` — ISA-88 §5.3 mapping.
- `07_persistence.md` — NVS layout, write policy.
- `08_lifecycle.md` — build + runtime lifecycle.
- `09_manifest_integration.md` — recipe-as-manifest pipeline.
- `10_error_model.md` — engine error taxonomy.

Плюс 8 ADRs documenting non-obvious decisions і 3 usage guides.

> ℹ️ **Note:** scenario-engine docs migrated з old `modesp_sequence`
> location; вони factually current але speak з old engine class name.
> Phase 0/1/2 rebuild renamed `modesp_sequence` → `modesp_scenario` AND
> `SequenceEngine` → `Engine`. Docs mostly auto-scrubbed; якщо ви
> find stale references, fix on the spot.

## Що далі

- **[scenario-engine/](../scenario-engine/)** — deep dives.
- **[02-module-author-guide/recipe-authoring.md](../../02-module-author-guide/recipe-authoring.md)**
  — author-side guide для writing recipes.
- **[02-module-author-guide/recipe-actions.md](../../02-module-author-guide/recipe-actions.md)**
  — actions і conditions catalog.
- **[02-module-author-guide/continuous-behaviors.md](../../02-module-author-guide/continuous-behaviors.md)**
  — PID / hysteresis / ramp.
- **[modules/abs_test.md](../modules/abs_test.md)** *(planned)* —
  reference recipe.

## Source

- [`components/modesp_scenario/`](../../../../components/modesp_scenario/) — implementation.
- [`tools/compile_scenario.py`](../../../../tools/compile_scenario.py) —
  build-time compiler.
- [`tools/known_actions.json`](../../../../tools/known_actions.json) —
  action audit catalog.
