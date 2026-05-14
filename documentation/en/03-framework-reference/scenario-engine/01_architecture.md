# 01 — Architecture

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/01_architecture.md](../../../uk/03-framework-reference/scenario-engine/01_architecture.md)

High-level overview of how Scenario Engine components fit together from
manifest authoring to runtime execution.

## Build-time + runtime pipeline

```
┌──────────────────────────────────────────────────────────────────────┐
│                        BUILD TIME PIPELINE                            │
│                                                                       │
│  modules/<recipe_name>/manifest.json                                  │
│  ├─ existing sections (state, ui, mqtt, loggable, features)           │
│  └─ NEW section: "scenario" {tracks: [{phases: [...]}], ...}          │
│                                                                       │
│       │                                          │                    │
│       ▼ existing pipeline                        ▼ new build step     │
│  tools/generate_ui.py                       tools/compile_scenario.py │
│  (extends to recognize module_type=recipe)  (new script — Step 2)     │
│       │                                          │                    │
│       ▼                                          ▼                    │
│  generated/state_meta.h                    data/scenarios/<n>.modr    │
│  data/ui.json                              (binary — staged to LFS)   │
│  generated/mqtt_topics.h                                              │
│                                                                       │
└──────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────────┐
│                        RUNTIME (ESP32)                                │
│                                                                       │
│  components/modesp_scenario/                                          │
│  ├─ Engine (modesp::scenario::Engine — BaseModule, multi-instance)    │
│  ├─ ActionRegistry (caller-owned; domain modules register actions)    │
│  ├─ ContinuousRegistry (caller-owned; standard primitives shipped)    │
│  ├─ ResourceArbiter (engine-owned; ISA-88 §5.3 two-scope arbitration) │
│  ├─ IStateBackend (DI interface — adapter to SharedState in main/)    │
│  ├─ IEngineObserver hooks → NvsObserver (edge-triggered persistence)  │
│  ├─ mirror::publish (direct-call mirror writer, runs every tick)      │
│  └─ Loads .modr from /data/scenarios/<name>.modr                       │
│                                                                       │
│  modules/<recipe_name>/  (no C++ code; recipe is manifest-only)       │
│                                                                       │
│  WebUI loads ui.json → renders widgets (existing infrastructure)      │
│  visible_when constraints show recipe widgets only when active        │
└──────────────────────────────────────────────────────────────────────┘
```

Note: there is no `IResourceArbiter` interface — the engine owns a concrete
`ResourceArbiter` member. Injection points are limited to `IStateBackend`,
`ActionRegistry`, `ContinuousRegistry`, and the observer span.

## Component responsibilities

| Component | File | Role |
|-----------|------|------|
| `Engine` | `include/modesp/scenario/engine.h` + `src/core/engine.cpp` | Public API surface; multi-instance dispatcher; ticks running scenarios; emits observer events; calls `mirror::publish` directly each tick. Constructor takes `IStateBackend&`, `ActionRegistry&`, `ContinuousRegistry&`, and an `etl::span<IEngineObserver*>` |
| `ActionRegistry` | `action_registry.{h,cpp}` | Caller-owned `ActionRegistry` (no singleton); registered actions resolved at compile via djb2 hashes. Injected into Engine via constructor. Two pools (actions / conditions), fixed-capacity ETL flat_maps |
| `ContinuousRegistry` | `continuous_behavior.{h,cpp}`, `continuous_primitives.{h,cpp}` | Caller-owned, no singleton, injected via constructor. Stage 2 ships standard primitives (PID, hysteresis, ramp) in `continuous_primitives.h` — registration is opt-in via `primitives::register_primitives()` |
| `ResourceArbiter` | `resource_arbiter.{h,cpp}` (concrete class in `private/` is referenced from the header; engine-owned member, no interface abstraction) | ISA-88 §5.3 atomic acquire/release for scenario- and phase-scope resources; zero-heap ETL flat_map |
| `IStateBackend` | `i_state_backend.h` | Engine's sole view of the underlying state store. Two raw virtuals (`get_raw`, `set_raw`) over `modesp::StateValue`; typed accessors inline. Production adapter to `modesp::SharedState` lives in `main/`; host tests use `StubStateBackend` |
| `IEngineObserver` | `i_engine_observer.h` | Three edge hooks (`on_scenario_started`, `on_phase_entered`, `on_scenario_terminal`) + `on_tick`. Observers are read-only — they cannot mutate engine state. Empty default bodies → unused overrides compile to no-ops |
| `NvsObserver` | `nvs_observer.{h,cpp}` | Implements `IEngineObserver`. Owns NVS write throttling and recovery callback wiring. Throttle policy: state changes immediate, main-track phase changes immediate, non-main phase changes ≥1 s debounce. Engine doesn't link `nvs_flash` directly — observer accepts caller-supplied read/write callbacks |
| `ModrLoader` | `modr_loader.{h,cpp}` | Validates `.modr` byte buffers, returns a `LoadedScenario` view |
| `BuiltinActions` | `builtin_actions.{h,cpp}` | Domain-agnostic actions (`log`, `set_state`, `wait_ms`) + leaf conditions |
| `runtime_types.h` | `include/modesp/scenario/runtime_types.h` | POD `TrackRuntime` and `SequenceRuntime` (per-instance runtime state). FSM advancement lives in `src/core/track.cpp` and `src/core/instance.cpp` and is folded into `Engine::on_update` — no standalone `SequenceTrack` / `SequenceInstance` classes |
| `NvsToken` | `nvs_token.{h,cpp}` | 96-byte persistence token; serialize / deserialize with CRC16. Magic `'SCTK'` (`'SQTK'` rejected for legacy compatibility) |
| `EngineError` | `engine_error.h` | Uniform error code enum |

## Data flow per tick

```
ModuleManager calls engine.on_update(dt_ms)
   │
   ▼
For each loaded slot in the engine:
   │
   ├─ Tick the SequenceRuntime (folded into engine.cpp / instance.cpp):
   │     │
   │     ├─ Process global transitions (priority sorted)
   │     │     └─ On match: abort instance (release phase_scope, fail tracks)
   │     │
   │     ├─ For each track (declaration order; src/core/track.cpp):
   │     │     ├─ Increment phase_elapsed_ms (saturating)
   │     │     ├─ Handle WAITING_FOR_RESOURCE (retry phase-scope acquire)
   │     │     ├─ Run exit actions (one per tick) if pending transition
   │     │     ├─ Run entry actions (one per tick)
   │     │     ├─ Evaluate transitions; on match latch target phase
   │     │     └─ Check phase timeout
   │     │
   │     └─ Check completion_rule; transition scenario state if satisfied
   │
   ├─ mirror::publish(state, slot)   ← direct call, runs unconditionally every
   │                                    tick. Helper in private/mirror.h.
   │
   └─ On state-machine edges, emit observer hooks synchronously:
         on_scenario_started   — IDLE/LOADED → RUNNING
         on_phase_entered      — track enters new phase (incl. initial)
         on_scenario_terminal  — scenario reaches COMPLETED or FAILED
      NvsObserver listens to these and applies its own throttle policy
      (state changes immediate; main-track phase changes immediate;
       non-main phase changes ≥1 s debounce). `on_tick(dt_ms)` is also
      dispatched to every observer for any tick-driven internal state
      (NvsObserver uses it to advance its per-slot throttle counter).
```

Notes on what is *not* in the engine any more (versus the pre-rebuild
`modesp_sequence::SequenceEngine`):

- No `publish_mirror_keys()` member — mirror writes are a direct call to
  `mirror::publish` in the tick path.
- No `persist_scan()` / `persist_slot()` members — persistence is delegated
  entirely to `NvsObserver`, which owns its own throttle state.
- No singletons. `ActionRegistry` and `ContinuousRegistry` are caller-owned
  references injected into the constructor; multiple engine instances can
  coexist with independent registries (useful for host test harnesses).

## Manifest-driven integration

The cornerstone architectural choice (per ADR-0004): a recipe is a
**ModESP module manifest** with `module_type: "recipe"` plus a `scenario`
section. This reuses the existing pipeline:

| Section | Read by | Output |
|---------|---------|--------|
| `state` | `generate_ui.py` (existing) | `state_meta.h` declarations |
| `ui` | `generate_ui.py` (existing) | `ui.json` widgets |
| `mqtt` | `generate_ui.py` (existing) | `mqtt_topics.h` |
| `scenario` (NEW) | `compile_scenario.py` (new) | `data/scenarios/<n>.modr` binary |

Zero WebUI generator changes. Zero widget-type changes. A recipe author
gets full UI + state + MQTT + persistence integration for free because
the existing tooling reads standard sections from the manifest.

## Memory budget (ESP32-WROOM-32)

Per slot (default `CONFIG_MODESP_MAX_SEQUENCES = 2`):
- `SequenceRuntime` ~600 bytes (`tracks[6]` × ~100 bytes each)
- `uint8_t buffer[MODR_MAX_SIZE]` = 4 KB (`CONFIG_MODESP_MODR_MAX_SIZE`,
  default 4 096; bump via menuconfig if a recipe exceeds the budget)
- Slot bookkeeping (`buffer_size`, dedup flags) ~24 bytes
- **Total per slot: ~4.6 KB**

Pool overhead:
- `Engine` struct: ~9.2 KB (2 slots × 4.6 KB) + per-slot edge-detect
  bookkeeping (`last_emitted_state_`, `last_emitted_phase_`)
- `ActionRegistry`: ~3 KB (up to 64 entries × ~50 bytes, two pools)
- `ResourceArbiter`: ~640 bytes (32 entries × 20 bytes)
- `NvsObserver`: ~64 bytes per-slot throttle counters (sized for
  `MAX_SLOTS = 8`, the Kconfig ceiling)

**Engine total: ~13 KB SRAM** at the default `MAX_SEQUENCES = 2` /
`MODR_MAX_SIZE = 4 KB` configuration. Scales linearly with both Kconfig
knobs — at the historical 4 × 16 KB settings the engine still fits
comfortably in WROOM-32's 320 KB DRAM, but the new defaults reclaim
~54 KB for application code.

## Cross-references

- [00_overview.md](00_overview.md) — what and why
- [02_binary_format.md](02_binary_format.md) — `.modr` byte layout
- [03_api_reference.md](03_api_reference.md) — public C++ API
- [04_state_machines.md](04_state_machines.md) — scenario and track FSMs
- [09_manifest_integration.md](09_manifest_integration.md) — full build
  pipeline including schema validation and compiler error catalog
