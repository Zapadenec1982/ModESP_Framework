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
    modesp::scenario::SequenceEngine* engine_;
    modesp::scenario::SequenceHandle handle_ = 0;

public:
    void set_engine(modesp::scenario::SequenceEngine* e) { engine_ = e; }

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

`main.cpp` integration (once at boot — see Step 16):

```cpp
#include "modesp/scenario/engine.h"
#include "modesp/scenario/builtin_actions.h"

static modesp::scenario::SequenceEngine sequence_engine(&app.state());
static MyBusinessModule biz_module;
biz_module.set_engine(&sequence_engine);

// Register builtins ONCE before any module init runs
modesp::scenario::builtins::register_builtins();

// Register the engine before business modules that depend on it
app.modules().register_module(sequence_engine);
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

If the device resets mid-scenario, the engine reads the NVS token at on_init, restores
phase_idx + phase_elapsed_ms, and enters the PAUSED state. The WebUI can show a banner
via `visible_when: {abs_test.scenario_state: ["paused"]}`.

Resume:

```cpp
engine_->resume(handle_);  // continues from saved phase + elapsed_ms
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
