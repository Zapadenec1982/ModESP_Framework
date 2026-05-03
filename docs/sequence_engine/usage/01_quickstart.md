# Quickstart — Hello, Scenario

5-minute hands-on for ModESP business module developers. Cover loading
a recipe, starting it, observing live state, і power-cycle recovery.

## Prerequisites

- ESP-IDF 5.x toolchain set up (`idf.py --version` works)
- ModESP framework cloned, builds clean
- WiFi credentials configured (or use serial monitor only)

## 1. Author the recipe

A recipe is а ModESP module з `module_type: "recipe"` plus а `scenario`
section. Place at `modules/<your_recipe>/manifest.json`. Recipe name budget:
**≤ 12 chars** to fit 32-char SharedState key constraint.

Reference recipe lives at [modules/abs_test/manifest.json](../../../modules/abs_test/manifest.json).
Highlights:

```jsonc
{
  "manifest_version": 1,
  "module": "abs_test",            // ≤ 12 chars
  "module_type": "recipe",         // tells generator skip C++ binding
  "version": "1.0.0",
  "priority": 5,

  "state": {
    "abs_test.scenario_state":  {"type": "string", "access": "read"},
    "abs_test.main_phase_name": {"type": "string", "access": "read"}
    // ... mirror keys engine writes runtime
  },

  "ui": { /* widgets з visible_when, standard generate_ui.py pipeline */ },

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

Built-in actions і conditions reference у [02_writing_recipes.md](02_writing_recipes.md).

## 2. Build

`compile_scenario.py` runs automatically during `idf.py build` (CMake pre-step).
Manual invocation для quick check:

```bash
python tools/compile_scenario.py --recipe modules/abs_test/manifest.json \
                                 --output data/scenarios/abs_test.modr
```

Output `.modr` automatically bundled into LittleFS partition image.

## 3. Trigger from your business module

```cpp
#include "modesp/sequence/sequence_engine.h"
#include "modesp/sequence/builtin_actions.h"

class MyBusinessModule : public modesp::BaseModule {
    modesp::sequence::SequenceEngine* engine_;
    modesp::sequence::SequenceHandle handle_ = 0;

public:
    void set_engine(modesp::sequence::SequenceEngine* e) { engine_ = e; }

    bool on_init() override {
        // Load recipe (engine resolves /lfs/scenarios/abs_test.modr)
        handle_ = engine_->load_path("/lfs/scenarios/abs_test.modr");
        if (handle_ == 0) {
            ESP_LOGE("biz", "load failed: %d",
                     static_cast<int>(engine_->last_error()));
            return false;
        }
        return true;
    }

    void on_some_event() {
        if (handle_ != 0
         && engine_->state(handle_) == modesp::sequence::SequenceRuntime::State::LOADED) {
            engine_->start(handle_);
        }
    }
};
```

`main.cpp` integration (один раз при boot — see Step 16):

```cpp
#include "modesp/sequence/sequence_engine.h"
#include "modesp/sequence/builtin_actions.h"

static modesp::sequence::SequenceEngine sequence_engine(&app.state());
static MyBusinessModule biz_module;
biz_module.set_engine(&sequence_engine);

// Register builtins ONCE before any module init runs
modesp::sequence::builtins::register_builtins();

// Register engine before business modules що залежать від нього
app.modules().register_module(sequence_engine);
app.modules().register_module(biz_module);
```

## 4. Observe

WebUI page "Тест" (configured у recipe's `ui` section) shows mirror keys
live. Cards are gated by `visible_when` so hidden when scenario IDLE.

Serial monitor shows transitions logged via `log` action:

```
I (12345) abs_test: main: phase_a
I (13345) abs_test: main: completing
I (14345) abs_test: watcher: started
```

## 5. Power-cycle recovery

If device resets mid-scenario, engine reads NVS token at on_init, restores
phase_idx + phase_elapsed_ms, і enters PAUSED state. WebUI може show banner
through `visible_when: {abs_test.scenario_state: ["paused"]}`.

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
