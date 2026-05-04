# 08 — Lifecycle (Build-Time + Runtime)

End-to-end story: recipe authoring → compilation → flashing → boot →
execution → recovery. Walks через every system layer that touches а
recipe.

## Build-time

```
1. Author writes manifest:
   modules/<recipe>/manifest.json
   ├─ "module_type": "recipe"
   ├─ standard sections: state, ui, mqtt
   └─ NEW section: "scenario" з tracks/phases/transitions

2. CMake pre-build hooks (run before C++ compilation):

   tools/generate_ui.py
     ↓ scans modules/*/manifest.json
     ↓ reads "state", "ui", "mqtt", "features" sections
     ↓ emits:
       - generated/state_meta.h        (всі declared SharedState keys)
       - data/ui.json                  (WebUI widget tree)
       - generated/mqtt_topics.h       (MQTT pub/sub topics)
       - generated/module_register.h   (БЕЗ binding для recipe-type modules)

   tools/compile_scenario.py
     ↓ scans modules/*/manifest.json filtering "module_type" == "recipe"
     ↓ for each: validates "scenario" section, hash-resolves actions,
       emits binary
     ↓ data/scenarios/<recipe>.modr   (file size 100B-16KB typical)

3. ESP-IDF build:
   - C++ компонент modesp_sequence builds engine code
   - LittleFS partition image bundles data/* (включно з data/scenarios/*.modr)
   - Final firmware: ELF + LFS image
```

## Runtime — boot sequence

```
power on
  │
  ▼
NVS init (always)
  │
  ▼
ConfigService reads board.json + bindings.json
  │
  ▼
HAL initializes GPIO from BoardConfig
  │
  ▼
DriverManager creates drivers
  │
  ▼
Phase 1 modules init: ErrorService, LoggerService, ConfigService, ...
  │
  ▼
Phase 2 module preparation:
  - sequence_engine.set_state(&app.state())
  - modesp::sequence::builtins::register_builtins()      ← built-in actions/conditions
  - sequence_engine.set_nvs_callbacks(write, read, ...)  ← persistence wiring
  - app.modules().register_module(sequence_engine)
  - modesp_register_modules(app)                          ← business modules incl.
                                                            those що may register
                                                            their own actions
  │
  ▼
Phase 2 init_all:
  - sequence_engine.on_init() called → arbiter.clear_for_tests, slots reset
  - Business modules init → они можуть call ActionRegistry::register_action
    у their own on_init
  │
  ▼
Phase 3 modules: HTTP, WebSocket, MQTT
  │
  ▼
Main loop @ 100 Hz:
  for each module: on_update(dt_ms)
    └─ sequence_engine.on_update(10):
         ├─ instance_tick(...) для each running instance
         ├─ publish_mirror_keys(...) для each loaded slot
         └─ persist_scan(...) для each loaded slot
```

## Runtime — recipe load + start

A business module triggers а recipe (typically on user action або system event):

```cpp
// 1. Load
SequenceHandle h = engine.load_path("/data/scenarios/abs_test.modr");
if (h == 0) {
    ESP_LOGE(...); return;  // check engine.last_error()
}

// 2. (Optional) Recovery from persisted state
if (engine.try_recover(h) == EngineError::OK) {
    // Slot тепер у PAUSED state із phase_idx + elapsed_ms restored.
    // User decides via WebUI button: resume() або abort().
    // У це point recipe не runs — manual intervention required.
} else {
    // No persisted state OR recovery failed — start fresh
    EngineError err = engine.start(h);
    if (err != EngineError::OK) {
        // RESOURCE_CONTENDED або internal error
        engine.unload(h);
        return;
    }
}

// 3. Engine on_update ticks scenario every 10ms; eventually:
//    - state(h) → COMPLETED → user sees result у WebUI
//    - state(h) → FAILED → check track states for which failed
//    - state(h) stays RUNNING indefinitely if recipe has no terminal path
//      (probably а bug — scenario_timeout_max_ms safety net)
```

## Tick-by-tick state evolution example

For а minimal 2-phase recipe із time-based transition:

```
Tick 0: load_path() → state = LOADED
        engine.start() → state = RUNNING
                       → arbiter.acquire_scenario() (no resources here = no-op)
                       → instance_start() — track 0 → RUNNING, phase_idx = 0

Tick 1: on_update(10):
        - instance_tick:
          - track_tick(0): phase_elapsed_ms = 10
                            entry actions run (one per tick)
        - publish_mirror_keys: writes "<recipe>.scenario_state" = "running"
        - persist_scan: state changed (LOADED→RUNNING) → write callback fires

Tick 2..10: track 0 advances entry actions, eventually evaluates transitions.

Tick 11: time_elapsed_ms condition fires (ms=100):
         - latch target = phase 1; running_exit_actions = true

Tick 12+: exit actions run (phase 0); apply transition →
         - track.phase_idx = 1, entry_action_progress = 0
         - phase 1 entry actions begin

...

Tick N: phase 1 transition to $complete fires:
        - track 0 → COMPLETED
        - completion_rule (all_tracks_complete) satisfied → scenario → COMPLETED
        - arbiter.release_scenario() (no resources here)
        - publish_mirror_keys: "scenario_state" = "completed"
        - persist_scan: state changed (RUNNING→COMPLETED) → write callback fires
```

## Crash scenario

Mid-execution power loss. State у NVS:

```
Before power loss (at last persist):
  NVS["seqstate"]["t0"] = serialized seq_token із:
    - magic = 'SQTK'
    - scenario_id = djb2("abs_test")
    - scenario_state = RUNNING
    - tracks[0].phase_idx = 1
    - tracks[0].phase_elapsed_ms = 5000
    - ... CRC

Power restored. Boot sequence runs normally up до Phase 2 init_all.
```

A business module's `on_init` (або а dedicated recovery service) calls:

```cpp
auto h = engine.load_path("/data/scenarios/abs_test.modr");
if (h == 0) return;

EngineError err = engine.try_recover(h);
if (err == EngineError::OK) {
    // engine.state(h) == PAUSED
    // engine.track_phase_idx(h, 0) == 1
    // engine.track_phase_elapsed_ms(h, 0) == 5000
    // Hardware state unknown (was on phase 1 when power lost — heater might
    // have been mid-cycle). Engine doesn't restart hardware.
    // User decides:
    //   - resume(h): continue from where stopped (phase 1, 5s elapsed)
    //   - abort(h): force FAILED; recipe's abort handler закриває hardware
}
```

WebUI banner (Stage 1.5):
> Scenario "abs_test" recovered to PAUSED у phase 1. Hardware may не reflect
> recipe state. **Resume**, **Abort**, or **Unload**?

## Cleanup і unload

```
Eventually:
  engine.unload(h) → 
    - arbiter.release_scenario(h)
    - arbiter.release_phase(h, t) для each track
    - slot reset (buffer_size = 0); state → IDLE
    - last_persisted_* preserved у NVS (slot t<idx> остается)

Якщо firmware update changes recipe з same name:
  - On next try_recover: token's scenario_id matches new recipe's id
    (djb2("abs_test") same), AND track_count + phase_count must також match
    OR deserialize_token returns INVALID_FILE
  - On schema mismatch: caller treats as no-data, calls start() to begin fresh
  - NVS slot eventually overwritten на next persist
```

## See also

- [00_overview.md](00_overview.md) — what + why
- [01_architecture.md](01_architecture.md) — component diagram
- [04_state_machines.md](04_state_machines.md) — state transition tables
- [07_persistence.md](07_persistence.md) — persistence + recovery details
- [09_manifest_integration.md](09_manifest_integration.md) — manifest pipeline details
- [usage/01_quickstart.md](usage/01_quickstart.md) — runnable starter
