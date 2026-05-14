# Module Author Guide — Overview

> 📖 **Українською:** [documentation/uk/02-module-author-guide/overview.md](../../uk/02-module-author-guide/overview.md)

This guide is for engineers writing **business-logic modules** і **scenario
recipes** on top of ModESP. By the end of this section you should be able to:

- Drop а new module into `modules/your_thing/` і have it picked up by the build.
- Declare state keys, UI widgets, and MQTT topics declaratively (no manual
  schema, no C++ boilerplate per key).
- Write а scenario recipe as part of а manifest і have it compile to а
  binary blob that the engine executes at runtime.
- Read і write state через а thread-safe, type-checked store (SharedState).
- Persist module config across reboots without touching NVS APIs directly.

## What is а module?

In ModESP, а **module** is а unit of business logic that lives у its own
directory under `modules/` і ships а **manifest.json** describing everything
the framework needs to know about it:

- Which **state keys** the module reads і writes (typed: int, float, bool, string).
- Which **UI widgets** appear для it on the WebUI (declarative — no Svelte
  code in your hands).
- Which **MQTT topics** it publishes / subscribes to.
- (Optional) А **scenario** section if the module is а recipe — declarative
  phase/transition graph compiled to а binary `.modr` at build time.
- (Optional) **Feature flags**, **i18n strings**, **datalogger channels**.

The framework's build-time generator (`tools/generate_ui.py`) reads all
module manifests, produces а merged UI schema, а C++ state-metadata header,
MQTT topic constants, і а CMake module list. You write the C++ class із
business logic; everything else is generated.

## Two flavors of modules

| Type | Has C++ code? | Has manifest scenario? | Use when |
|---|---|---|---|
| **Service module** | Yes (BaseModule subclass) | No | Continuous logic: a thermostat, а sensor reader, an alarm manager. Active on every tick. |
| **Recipe module** | No | Yes | Time-bounded process: а cook program, batch reactor cycle, irrigation sequence. Engine drives it through phases. |

You can mix — а business module that loads а recipe on demand is а valid
design (е.g. operator picks recipe А or В via UI, your module calls
`engine.load_path()`).

## The five core ideas

### 1. Manifest-driven everything

Your module's contract is а JSON file. The framework reads it, generates
everything it can statically, і only асks you to write the actual logic.
Adding а new state key takes one line у `manifest.json` — no C++ changes,
no UI changes, no MQTT plumbing.

See **[manifest.md](manifest.md)** для the full schema.

### 2. SharedState as the data backbone

There's one in-process state store (`modesp::SharedState`) shared by all
modules. It's а typed, thread-safe key-value map of bounded capacity. Modules
read each other's outputs by reading the corresponding state keys — no
direct pointers, no observer registration. The HTTP API, WebSocket, MQTT
publisher, і datalogger all observe SharedState too.

See **shared-state.md** *(planned)* для read/write patterns, change tracking,
and lifetime guarantees.

### 3. Three-phase init lifecycle

Modules are constructed at static-storage init time, then go through three
init phases driven by the App / ModuleManager:

1. **Phase 1 (CRITICAL):** error service, watchdog, config, persistence,
   system monitor. Runs первой.
2. **Phase 2 (HIGH/NORMAL):** Wi-Fi, cloud, equipment, drivers, **scenario
   engine**, business modules. After config is loaded.
3. **Phase 3 (LOW):** HTTP, WebSocket. Last — depends on everything above.

Your module's `priority` field у the manifest picks its phase. See
**writing-a-module.md** *(planned)* для the lifecycle hooks (`on_init`,
`on_update`, `on_message`, `on_stop`).

### 4. Scenarios as compiled artifacts

If you write а recipe, it doesn't run as JSON at runtime. Build-time
`compile_scenario.py` produces а binary `.modr` blob (CRC-protected,
4-byte aligned, bounded ≤ 16 KB) that the engine loads from LittleFS.
This means recipe authoring shifts production complexity from runtime to
build time — invalid recipes never reach the device.

See **recipe-authoring.md** *(planned)* і
**[scenario-engine/](../03-framework-reference/scenario-engine/)** для deep dive.

### 5. Zero heap allocation

The framework targets ESP32-WROOM-32 (320 KB DRAM). Standard C++ containers
are avoided — we use ETL (Embedded Template Library) for fixed-capacity maps,
vectors, queues, optionals. Modules SHOULD follow suit — no `std::vector`,
no `std::map`, no `new`/`delete`. Use ETL or static-size POD types.

See **best-practices.md** *(planned)* для allocation conventions і common
pitfalls.

## Anatomy of а module folder

```
modules/your_thing/
├── manifest.json          ← REQUIRED — module contract
├── CMakeLists.txt         ← REQUIRED for service modules; omit для recipes
├── include/
│   └── your_thing.h       ← Module C++ class declaration (service modules)
└── src/
    └── your_thing.cpp     ← Implementation (service modules)
```

Recipe modules contain **only** `manifest.json` — no C++, no CMakeLists. The
framework recognises them by `"module_type": "recipe"` у the manifest.

## When you don't need а new module

Sometimes you want behavior, not а new module. Consider:

- **One-off scenario** with no permanent state? Use а recipe (no C++,
  faster iteration).
- **Custom action / condition** для recipes? Register through
  `ActionRegistry` from an existing module's init — see
  **recipe-actions.md** *(planned)*.
- **New continuous control primitive (PID variant)?** Register а
  `ContinuousBehavior` factory — see **continuous-behaviors.md** *(planned)*.
- **Hardware-specific driver (new I2C sensor)?** Goes у `components/modesp_hal/`
  з а new `IDriver` subclass, not а module. See
  **[hardware/bindings.md](../04-hardware/bindings.md)** *(planned)*.

## Recommended reading order

1. [Manifest reference](manifest.md) — what you'll write most often.
2. shared-state.md *(planned)* — how data flows.
3. writing-a-module.md *(planned)* — the C++ side.
4. ui-widgets.md *(planned)* — how WebUI renders your state.
5. [best-practices.md](best-practices.md) — patterns і anti-patterns.

If your goal is а recipe, skip ahead to:

1. [Manifest reference](manifest.md) — same manifest hosts the `scenario` section.
2. recipe-authoring.md *(planned)*.
3. recipe-actions.md *(planned)*.

## Existing modules as worked examples

Look at these for reference:

- [`modules/simple_thermo/`](../../../modules/simple_thermo/) — minimal
  service module (ON/OFF thermostat). Manifest з state, UI, MQTT;
  ~150 LOC C++. Good first read.
- [`modules/datalogger/`](../../../modules/datalogger/) — bigger service
  module з features (channels, retention, plot data API). See
  [datalogger reference](../03-framework-reference/modules/datalogger.md).
- [`modules/equipment/`](../../../modules/equipment/) — service module that
  bridges manifest з HAL drivers (most coupled module — read AFTER you
  understand the basics).
- [`modules/abs_test/`](../../../modules/abs_test/) — pure recipe module
  (no C++). Two parallel tracks з cross-track synchronization. Reference
  for recipe authoring.
