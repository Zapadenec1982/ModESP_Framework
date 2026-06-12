# System overview — how it all connects

> 📖 **Українською:** [documentation/uk/01-getting-started/system-overview.md](../../uk/01-getting-started/system-overview.md)

This is the one page to read before you touch anything. It explains the
**philosophy**, the **single source of truth**, and the **chain of connections**
from a physical pin to a WebUI widget — so the rest of the docs make sense and
you know *where* to change *what*.

- [concepts.md](concepts.md) gives you the four runtime mental models (manifest,
  modules+drivers, SharedState, scenarios). Read it next.
- [03-framework-reference/architecture.md](../03-framework-reference/architecture.md)
  is the full reference (layers, tasks, init phases). Read it when you need depth.
- This page is the **connective tissue** between them.

---

## The philosophy (why the framework is shaped this way)

1. **Single source of truth, declared once.** A thing is described in exactly one
   place — a manifest — and the build *generates* everything derived from it (UI,
   MQTT topics, state metadata, C++ registration, Kconfig). Rename a state key in
   the manifest and the UI, MQTT, and persistence all follow. You never keep two
   files in sync by hand.

2. **Manifest is the contract; C++ is the behaviour.** Read a module's
   `manifest.json` to know *what it is* without reading a line of C++. The
   implementation is free to change as long as it honours the contract.

3. **Generated files are never edited.** Anything under `generated/`, plus
   `data/ui.json` and `components/modesp_hal/Kconfig`, is rewritten on every
   build. To change them, change the manifest and rebuild. They carry
   `DO NOT EDIT` banners.

4. **Inconsistency fails the build, not the device.** A binding that points at a
   nonexistent pin, a wrong driver type, or a driver disabled in menuconfig is a
   *build error* with a clear message — never a silent runtime "it just doesn't
   work" on an industrial machine.

5. **Two citizen types, one bus.** Everything pluggable is either a **module**
   (business logic) or a **driver** (hardware). They never call each other
   directly — they communicate only through **SharedState** keys. This makes the
   whole system's topology readable from the manifests and every module trivially
   testable.

6. **Modules never touch hardware.** Drivers own GPIO/I2C/OneWire/ADC and publish
   `equipment.<role>` keys; modules read those keys and write higher-level state.
   Swapping a DS18B20 for an NTC changes a binding, not a line of business logic.

7. **Zero heap in the hot path.** No `new`/`std::string`/`std::vector` in
   `on_update()`/`on_message()` — fixed-capacity ETL types only. The framework
   targets a 4 MB ESP32 running at 100 Hz, forever, without fragmentation.

8. **Declarative over imperative where it pays.** Multi-phase processes (defrost,
   pulldown, OTA) are authored as **scenarios** (declarative FSMs in JSON), not
   hand-written C++ state machines.

---

## Two layers: what you write vs. what the framework does

```
PRODUCT  (you)                 FRAMEWORK  (provided)
─────────────────              ──────────────────────────────────────────
modules/<m>/manifest.json  →   generator → ui.json, state_meta.h, mqtt, …
modules/<m>/*.cpp           ┐
drivers/<d>/manifest.json   │   EquipmentBase, DriverManager + registry
drivers/<d>/*.cpp           ├─→ SharedState, ModuleManager, App, 100 Hz loop
boards/<b>/board.json       │   HTTP/WS/MQTT/AWS, OTA, DataLogger, LittleFS
boards/<b>/bindings.json    │   Scenario engine, PersistService, Watchdog
project.json                ┘
```

You declare *what* (manifests) and write *behaviour* (module C++ + driver
factories). The framework supplies *everything else* and wires it together at
build time.

---

## The single source of truth — four inputs

| Input | Owns | Who writes it |
|---|---|---|
| `modules/<m>/manifest.json` | A module's state keys, UI cards, MQTT topics, persistence, log channels, (recipes) scenario | Module author |
| `drivers/<d>/manifest.json` | A driver's `category`, `hardware_type`, `requires_address`, settings, discovery | Driver author |
| `boards/<b>/board.json` | The board's physical resources (GPIO/OneWire/ADC/I2C-expander ids) | Board author |
| `boards/<b>/bindings.json` | Wiring: `{hardware → driver → role}` for that board | Deployer |
| `project.json` | Which modules build into this firmware | Product owner |

The build copies the **active** board's `board.json`/`bindings.json` into `data/`
(board selected via `idf.py menuconfig`), then the generator reads all of the
above at once and cross-checks them.

---

## The build pipeline — one generator, many outputs

`tools/generate_ui.py` runs as a pre-build CMake step (before ESP-IDF's
`project()`), validates everything, and — only if valid — emits:

| Generated artifact | From | Consumed by |
|---|---|---|
| `data/ui.json` | module manifests | WebUI |
| `generated/state_meta.h` | state keys | SharedState / Persist / MQTT |
| `generated/mqtt_topics.h` | mqtt sections | MqttService |
| `generated/module_{includes,instances,register}.h`, `modules.cmake` | project.json | `main.cpp`, CMake |
| `generated/datalogger_{channels,events}.h` | `loggable` sections | DataLogger |
| `components/modesp_hal/Kconfig` | `drivers/*/manifest.json` | menuconfig (per-driver toggle) |
| `generated/drivers.cmake` | drivers | `modesp_hal` REQUIRES |
| `generated/driver_register_all.h` | drivers | DriverManager (registration) |
| `generated/required_drivers.cmake` | active bindings | build-time consistency gate |
| `data/www/i18n/*.json` | per-module i18n | WebUI |

`tools/compile_scenario.py` runs alongside it, compiling recipe `scenario` blocks
into `data/scenarios/*.modr`. **Validation runs first**: a bad manifest, a binding
to a nonexistent pin/driver, a driver↔hardware type mismatch, or a recipe error
aborts the build before any file is written. See
[05-tools/generate_ui.md](../05-tools/generate_ui.md).

---

## The full chain — a pin to a widget

Trace one temperature sensor end-to-end. Every arrow is declared, never
hand-wired:

```
board.json:   onewire_buses[{id:"ow_1", gpio:32}]          ← the bus exists
bindings.json:{hardware:"ow_1", driver:"ds18b20",          ← wire driver→role
               role:"air_temp", address:"28:..."}
drivers/ds18b20/manifest.json: hardware_type=onewire_bus   ← type must match ow_1
   │  (generator validates all of the above, emits Kconfig toggle + registry glue)
   ▼ build
DriverManager::init()  → DriverRegistry.create_sensor("ds18b20", binding, hal)
   │  the ds18b20 factory finds ow_1 in the HAL, configures the driver
   ▼ runtime, 100 Hz
ds18b20 driver reads the bus  →  SharedState["equipment.air_temp"] = 4.5
   ▼
simple_thermo module reads "equipment.air_temp", writes "simple_thermo.output"
   ▼  (change-tracked — no polling)
WS broadcast → WebUI widget   |   MqttService → topic publish   |   DataLogger
```

The module never knew there was a DS18B20, a OneWire bus, or GPIO 32 — only the
`equipment.air_temp` key. That is principle #6 in action.

---

## The driver mechanism (the newest connective layer)

Drivers are **optional, self-registering, and validated** — worth understanding
because it ties board, bindings, menuconfig, and the registry together:

- **Registry, not hardcode.** `DriverManager` looks up `binding.driver_type` in
  `DriverRegistry` (a `type → factory` map). Each driver self-registers with one
  macro (`MODESP_REGISTER_SENSOR/ACTUATOR`); the generator wires the rest. Adding
  a driver touches **no** framework file.
- **Optional via menuconfig.** Every driver gets an auto-generated
  `CONFIG_MODESP_DRIVER_<NAME>` toggle (`idf.py menuconfig → ModESP Drivers`). A
  disabled driver isn't compiled (smaller binary).
- **Consistency is enforced.** If the active board *binds* a driver that's
  disabled, the build fails. `python tools/drivers_sync.py --fix` reconciles
  menuconfig with the board (`--prune` disables unused drivers; `--dry-run`
  previews). See [05-tools/drivers_sync.md](../05-tools/drivers_sync.md).

Full detail: [02-module-author-guide/writing-a-driver.md](../02-module-author-guide/writing-a-driver.md).

---

## The runtime model (in one breath)

One `App` owns one `SharedState` and one `ModuleManager`. Modules register in
three priority phases, then `update_all(dt_ms)` ticks every module at 100 Hz on
the **main task**. Modules read/write SharedState; HTTP/WS/MQTT run on **other
tasks** and therefore touch state only through the mutex-protected SharedState —
never module internals. Scenarios are a regular module (`modesp_scenario`) that
interprets `.modr` FSMs. Depth: [architecture.md](../03-framework-reference/architecture.md).

---

## Navigation map — "to change X, look at Y"

| I want to… | Edit | Read |
|---|---|---|
| Add a state key / UI card / MQTT topic | the module's `manifest.json` | [manifest.md](../02-module-author-guide/manifest.md) |
| Change business logic | the module's `*.cpp` (`on_update`) | [writing-a-module.md](../02-module-author-guide/writing-a-module.md) |
| Support new hardware | a new `drivers/<d>/` | [writing-a-driver.md](../02-module-author-guide/writing-a-driver.md) |
| Wire hardware for a deployment | `boards/<b>/bindings.json` | [04-hardware/bindings.md](../04-hardware/bindings.md) |
| Declare a board's pins | `boards/<b>/board.json` | [04-hardware/board-config.md](../04-hardware/board-config.md) |
| Choose which modules ship | `project.json` | [architecture.md](../03-framework-reference/architecture.md) |
| Enable/disable drivers | `idf.py menuconfig` or `drivers_sync.py` | [drivers_sync.md](../05-tools/drivers_sync.md) |
| Author a multi-phase process | the recipe's `scenario` block | [recipe-authoring.md](../02-module-author-guide/recipe-authoring.md) |
| Understand a module you didn't write | its `manifest.json` first | [concepts.md](concepts.md) |

**What you never edit:** `generated/*`, `data/ui.json`, `components/modesp_hal/Kconfig`
(all regenerated). **What you never call directly:** GPIO/buses (drivers do),
another module's functions (SharedState does), the HTTP/MQTT plumbing (manifests do).

## Where to go next

1. [concepts.md](concepts.md) — the four runtime mental models in depth.
2. [02-module-author-guide/overview.md](../02-module-author-guide/overview.md) — write your first module.
3. [architecture.md](../03-framework-reference/architecture.md) — the full reference when you need it.
