# 01 — Architecture

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/01_architecture.md](../../../uk/03-framework-reference/scenario-engine/01_architecture.md)

High-level overview of how Sequence Engine components fit together from
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
│  ├─ SequenceEngine (BaseModule, multi-instance, multi-track)          │
│  ├─ ActionRegistry (domain modules register actions/conditions)       │
│  ├─ ContinuousRegistry (Stage 2 — PID, hysteresis, ramp)              │
│  ├─ ResourceArbiter (ISA-88 §5.3 two-scope arbitration)               │
│  ├─ ModrLoader (validates .modr blobs)                                │
│  ├─ NvsToken (persist/recover state)                                  │
│  └─ Loads .modr from /data/scenarios/<name>.modr                       │
│                                                                       │
│  modules/<recipe_name>/  (no C++ code; recipe is manifest-only)       │
│                                                                       │
│  WebUI loads ui.json → renders widgets (existing infrastructure)      │
│  visible_when constraints show recipe widgets only when active        │
└──────────────────────────────────────────────────────────────────────┘
```

## Component responsibilities

| Component | File | Role |
|-----------|------|------|
| `SequenceEngine` | `sequence_engine.{h,cpp}` | Public API surface; multi-instance dispatcher; ticks running scenarios; publishes mirror keys; orchestrates persistence |
| `ActionRegistry` | `action_registry.{h,cpp}` | Singleton hash → ActionDescriptor table; domain modules register custom actions |
| `ContinuousRegistry` | `continuous_behavior.{h,cpp}`, `continuous_registry.cpp` | Reserved for Stage 2 (PID, hysteresis, ramp behaviors) |
| `ModrLoader` | `modr_loader.{h,cpp}` | Validates `.modr` byte buffers, returns a `LoadedScenario` view |
| `ResourceArbiter` | `resource_arbiter.{h,cpp}` | ISA-88 §5.3 atomic acquire/release for scenario and phase scope |
| `BuiltinActions` | `builtin_actions.{h,cpp}` | 3 domain-agnostic actions (log, set_state, wait_ms) + 10 leaf conditions |
| `SequenceTrack` | `sequence_track.{h,cpp}` | Per-track state machine (track_tick); condition evaluator |
| `SequenceInstance` | `sequence_instance.{h,cpp}` | Per-scenario state machine (instance_tick); global transition handling |
| `NvsToken` | `nvs_token.{h,cpp}` | 96-byte persistence token; serialize/deserialize with CRC16 |
| `EngineError` | `engine_error.h` | Uniform error code enum |

## Data flow per tick

```
ModuleManager calls engine.on_update(dt_ms)
   │
   ▼
For each loaded slot in the engine:
   │
   ├─ instance_tick(runtime, dt_ms, state, arbiter)
   │     │
   │     ├─ Process global transitions (priority sorted)
   │     │     └─ On match: instance_abort (release phase_scope, fail tracks)
   │     │
   │     ├─ For each track (declaration order):
   │     │     └─ track_tick(runtime, track_idx, dt_ms, state, arbiter)
   │     │           │
   │     │           ├─ Increment phase_elapsed_ms (saturating)
   │     │           ├─ Handle WAITING_FOR_RESOURCE (try acquire phase resources)
   │     │           ├─ Run exit actions (one per tick) if pending transition
   │     │           ├─ Run entry actions (one per tick)
   │     │           ├─ Evaluate transitions; on match latch target
   │     │           └─ Check phase timeout
   │     │
   │     └─ Check completion_rule; transition scenario state if satisfied
   │
   ├─ publish_mirror_keys(slot)  ← writes <recipe>.<...> keys to SharedState
   │
   └─ persist_scan(dt_ms)   ← detects changes, throttles, invokes NVS callback
```

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

Per slot (4 default):
- `SequenceRuntime` ~600 bytes (tracks[6] × ~100 bytes each)
- `uint8_t buffer[MODR_MAX_SIZE]` = 16 KB
- Persistence tracking ~24 bytes
- **Total per slot: ~16.6 KB**

Pool overhead:
- `SequenceEngine` struct: ~64 KB (4 slots × 16.6 KB)
- `ActionRegistry`: ~3 KB (64 entries × ~50 bytes)
- `ResourceArbiter`: ~640 bytes (32 entries × 20 bytes)

**Engine total: ~67 KB SRAM** (default config). Fits comfortably in
WROOM-32's 320 KB DRAM.

## Cross-references

- [00_overview.md](00_overview.md) — what and why
- [02_binary_format.md](02_binary_format.md) — `.modr` byte layout
- [03_api_reference.md](03_api_reference.md) — public C++ API
- [04_state_machines.md](04_state_machines.md) — scenario and track FSMs
- [09_manifest_integration.md](09_manifest_integration.md) — full build
  pipeline including schema validation and compiler error catalog
