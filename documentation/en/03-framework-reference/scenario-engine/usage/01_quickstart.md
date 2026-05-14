# Quickstart — Hello, Scenario

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/usage/01_quickstart.md](../../../../uk/03-framework-reference/scenario-engine/usage/01_quickstart.md)

A 5-minute hands-on guide for ModESP business module developers. Covers loading
a recipe, starting it, observing live state, and power-cycle recovery.

## Prerequisites

- ESP-IDF 5.x toolchain set up (`idf.py --version` works)
- ModESP framework cloned, builds clean
- WiFi credentials configured (or use serial monitor only)

## 1. Author the recipe

A recipe is a ModESP module with `module_type: "recipe"` plus a `scenario`
section. Place it at `modules/<your_recipe>/manifest.json`. Recipe name budget:
**≤ 12 chars** to fit the 32-char SharedState key constraint.

The reference recipe lives at [modules/abs_test/manifest.json](../../../modules/abs_test/manifest.json).
Highlights:

```jsonc
{
  "manifest_version": 1,
  "module": "abs_test",            // ≤ 12 chars
  "module_type": "recipe",         // tells generator to skip C++ binding
  "version": "1.0.0",
  "priority": 5,

  "state": {
    "abs_test.scenario_state":  {"type": "string", "access": "read"},
    "abs_test.main_phase_name": {"type": "string", "access": "read"}
    // ... mirror keys the engine writes at runtime
  },

  "ui": { /* widgets with visible_when, standard generate_ui.py pipeline */ },

  "scenario": {
    "default_phase_timeout_ms": 30000,
    "completion_rule": "all_tracks_complete",
    "tracks": [
      { "name": "main", "flags": ["main_track"],
        "phases": [
          { "name": "phase_a",
            "entry": [{"action": "set_state",
                       "params": {"key": "test.output_a", "type": "bool", "value": true}}],
            "transitions": [{"to": "phase_b", "when": {"time_elapsed_ms": 1000}}] }
          // ...
        ]
      }
    ]
  }
}
```

The full built-in action and condition reference is in [02_writing_recipes.md](02_writing_recipes.md).

## 2. Build

`compile_scenario.py` runs automatically during `idf.py build` (CMake pre-step).
Manual invocation for a quick check:

```bash
python tools/compile_scenario.py --recipe modules/abs_test/manifest.json \
                                 --output data/scenarios/abs_test.modr
```

The output `.modr` is automatically bundled into the LittleFS partition image.

## 3. Trigger from your business module

```cpp
#include "modesp/scenario/engine.h"
#include "modesp/scenario/builtin_actions.h"

class MyBusinessModule : public modesp::BaseModule {
    modesp::scenario::Engine* engine_;
    modesp::scenario::SequenceHandle handle_ = 0;

public:
    void set_engine(modesp::scenario::Engine* e) { engine_ = e; }

    bool on_init() override {
        // Load recipe (engine resolves /data/scenarios/abs_test.modr)
        handle_ = engine_->load_path("/data/scenarios/abs_test.modr");
        if (handle_ == 0) {
            ESP_LOGE("biz", "load failed: %d",
                     static_cast<int>(engine_->last_error()));
            return false;
        }
        return true;
    }

    void on_some_event() {
        if (handle_ != 0
         && engine_->state(handle_) == modesp::scenario::SequenceRuntime::State::LOADED) {
            engine_->start(handle_);
        }
    }
};
```

`main.cpp` integration (once at boot — see Step 16). The engine takes its
dependencies (state backend, registries, observers) via constructor injection —
there are no singletons:

```cpp
#include "modesp/scenario/engine.h"
#include "modesp/scenario/action_registry.h"
#include "modesp/scenario/continuous_behavior.h"
#include "modesp/scenario/continuous_primitives.h"
#include "modesp/scenario/nvs_observer.h"
#include "modesp/scenario/builtin_actions.h"
#include "shared_state_backend.h"  // app-layer adapter for modesp::SharedState

// State backend adapter
static SharedStateBackend sb{app.state()};

// Caller-owned registries (no singletons)
static modesp::scenario::ActionRegistry     actions;
static modesp::scenario::ContinuousRegistry continuous;

// NVS persistence observer — caller supplies read/write callbacks
static modesp::scenario::NvsObserver nvs_obs{nvs_write_fn, nvs_read_fn, nullptr};
static modesp::scenario::IEngineObserver* obs_list[] = {&nvs_obs};

// Engine — constructor takes state, registries, and observer span
static modesp::scenario::Engine engine{sb, actions, continuous, obs_list};

// Populate registries BEFORE any module init runs
modesp::scenario::builtins::register_builtins(actions);
// Optional: register the standard continuous primitives (PID, hysteresis, ramp).
// Domain modules can also register their own.
modesp::scenario::primitives::register_primitives(continuous);

// Bind observer to engine (NvsObserver reads engine state when serialising)
nvs_obs.bind_engine(engine);

static MyBusinessModule biz_module;
biz_module.set_engine(&engine);

// Register the engine before business modules that depend on it
app.modules().register_module(engine);
app.modules().register_module(biz_module);
```

## 4. Observe

The WebUI page "Test" (configured in the recipe's `ui` section) shows mirror keys
live. Cards are gated by `visible_when` so they stay hidden when the scenario is IDLE.

The serial monitor shows transitions logged via the `log` action:

```
I (12345) abs_test: main: phase_a
I (13345) abs_test: main: completing
I (14345) abs_test: watcher: started
```

## 5. Power-cycle recovery

Recovery is **not** automatic. After the device resets and the scenario is
re-loaded (`load_path`/`load_buffer`), the caller must explicitly ask the engine
to restore persisted state from NVS by calling `try_recover()`, passing the
same `NvsObserver` instance wired in `main.cpp`. On success, the scenario's
`phase_idx` + `phase_elapsed_ms` are restored and the scenario enters the
**PAUSED** state — the caller must then explicitly call `resume()` to continue.

```cpp
// After load_path() succeeds:
auto err = engine_->try_recover(handle_, nvs_obs);
if (err == modesp::scenario::EngineError::OK) {
    // Scenario is now PAUSED at the restored phase/elapsed_ms.
    // WebUI can show a banner via visible_when: {abs_test.scenario_state: ["paused"]}.
    // Caller decides when to resume — e.g. on user confirmation:
    engine_->resume(handle_);  // continues from saved phase + elapsed_ms
}
```

Abort:

```cpp
engine_->abort(handle_);   // tracks transition through their abort paths
```

## Next steps

- [02_writing_recipes.md](02_writing_recipes.md) — full action vocabulary, transition kinds, parameter authoring
- [03_registering_actions.md](03_registering_actions.md) — adding domain-specific actions
- [examples/01_minimal_3phase.md](examples/01_minimal_3phase.md) — minimal single-track recipe
- [examples/02_dual_track_sync.md](examples/02_dual_track_sync.md) — cross-track sync pattern
