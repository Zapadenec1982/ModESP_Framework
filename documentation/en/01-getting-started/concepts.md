# Concepts — four mental models

> 📖 **Українською:** [documentation/uk/01-getting-started/concepts.md](../../uk/01-getting-started/concepts.md)

Before writing your first module, four ideas explain ~90% of how the
framework hangs together. Read this once и the rest of the documentation
is much easier to navigate.

This page is **not а tutorial**. It's the **conceptual glossary** you'll
refer back to.

---

## 1. Manifest-driven

Every module ships із а **`manifest.json`** that declares what the
module exposes — state keys, UI cards, MQTT topics, persistence, logging
channels — **separately** from the C++ code that implements behaviour.

```
modules/simple_thermo/
├── manifest.json           # declarative interface
├── include/
│   └── simple_thermo_module.h
└── src/
    └── simple_thermo_module.cpp
```

Build-time tools (`generate_ui.py`, `compile_scenario.py`) read all
manifests AND emit:

- **`ui.json`** consumed by the WebUI runtime.
- **`state_meta.h`** consumed by HTTP/MQTT/SharedState validation.
- **`mqtt_topics.h`** pre-computed topic strings.
- **`.modr`** binaries for recipe-typed manifests.

This separation gives you **one source of truth per module**. Renaming а
state key updates everything coherently — UI, MQTT, SharedState — без
hunting through C++ code.

**Why це matters:** when reading а module, look at the manifest first.
It tells you what the module IS without you needing to read the
implementation.

→ Deeper: **[02-module-author-guide/manifest.md](../02-module-author-guide/manifest.md)**.

---

## 2. Modules і drivers — the two citizen types

The framework has **exactly two kinds** of pluggable units:

| Type | What it is | Lifetime owned by |
|---|---|---|
| **Module** | Business logic. Reads/writes SharedState keys, owns logic loops. | `ModuleManager` (Phases 1-3 init). |
| **Driver** | Hardware abstraction. Talks to GPIO/I2C/OneWire/ADC. | `DriverManager` (HAL bridge). |

Both implement тhin base interfaces:

- Module → `class Module : public modesp::BaseModule`.
- Driver → `class Driver : public modesp::IDriver` (із typed sub-interfaces
  `ISensorDriver`, `IActuatorDriver`).

Drivers publish to `equipment.<role>` keys. Modules read those keys і
write higher-level state — `simple_thermo.output`, `alarm.fire_active`,
etc. **Modules don't touch hardware directly** — they consume the
hardware abstraction that drivers provide.

**A role = a capability, never a driver** (R0.1). A module declares a
role by its **capability** — `temperature`, `relay_out`, `humidity`,
`dimmer`, etc. — NOT by a concrete driver. A thermostat needs
"temperature" and doesn't know who supplies it: `ds18b20`, an NTC, a BLE
channel, or a future LoRa sensor. Capabilities — both sensor and
actuator — are enumerated in `tools/capabilities.json` (the single
vocabulary source). A role accepts a driver channel ⟺ their `capability`
is equal and the direction (in/out) is consistent — never by driver
name, `hw_type`, or transport (R3.1).

Consequence: **the source of a capability is swappable**. A wired
driver, a BLE device, or a future transport (LoRa/MQTT/ESP-NOW) fill the
same role with no module change (R0.2). A remote device's identity (BLE
MAC, adv-name, topic) lives on the **device** row (`board.json` /
runtime `devices.json`), NEVER on the role binding (R0.3) — so the role
stays transport-agnostic.

A special case is **recipe modules** — modules із `module_type:
"recipe"` whose behaviour is а scenario `.modr` binary instead of а
C++ class.

→ Deeper: **[02-module-author-guide/overview.md](../02-module-author-guide/overview.md)**,
**[03-framework-reference/components/modesp_hal.md](../03-framework-reference/components/modesp_hal.md)**.

---

## 3. SharedState — the runtime communication bus

There are **no inter-module function calls** у the runtime. Modules
communicate by reading AND writing keys у а typed key-value store:

```cpp
state.set("equipment.air_temp", 23.5);              // driver writes
float t; state.get("equipment.air_temp", t);        // module reads
state.set("simple_thermo.output", true);            // module writes
bool on; state.get("simple_thermo.output", on);     // another module reads
```

Each key holds а typed `StateValue` (variant of `bool`, `int32_t`,
`float`, `etl::string_view`). Type changes are rejected at write time
when validation is enabled.

State writes are **change-tracked**. Other subsystems (WebSocket
broadcast, DataLogger, MQTT publisher) observe changes WITHOUT polling
every key on every tick.

**Why це matters:** modules become trivially testable (stub the state,
set inputs, run `on_update`, assert outputs). And the topology is а
read of the manifests — no hidden dependencies.

→ Deeper: **[02-module-author-guide/shared-state.md](../02-module-author-guide/shared-state.md)**.

---

## 4. Scenarios — declarative finite-state machines

Most real refrigeration / HVAC behaviour is а **sequence із phases**:
defrost cycle, evaporator pulldown, alarm acknowledgement, OTA reboot,
cleaning mode. Hard-coding це у C++ означає writing FSMs by hand —
error-prone і testing-hostile.

**Scenarios** are declarative finite-state machines authored as JSON
у the manifest's `scenario:` block. The compiler turns each into а
compact `.modr` binary that the `modesp_scenario` engine interprets at
runtime.

Each scenario:

- Has **one or more parallel tracks** (concurrent FSMs з cross-track sync).
- Each track has **phases** із entry actions і transitions.
- Transitions can be time-based, condition-based, OR composite (`all_of`).
- A scenario terminates when its `completion_rule` is satisfied.

A simple example (defrost recipe):

```
phase_pump_down → phase_defrost → phase_drain → $complete
   ↓ if temp > 30°C
$fail
```

Recipes are first-class — operators can start/pause/abort them через
WebUI / HTTP / MQTT. State is persisted у NVS through tokens so power
loss recovers mid-scenario.

→ Deeper: **[02-module-author-guide/recipe-authoring.md](../02-module-author-guide/recipe-authoring.md)**,
**[03-framework-reference/components/modesp_scenario.md](../03-framework-reference/components/modesp_scenario.md)**,
**[03-framework-reference/modules/abs_test.md](../03-framework-reference/modules/abs_test.md)**.

---

## How they compose

```
┌────────────────────────────────────────────────────────────────┐
│  Manifest layer (build time): generate_ui.py + compile_scenario│
│  produce ui.json, state_meta.h, mqtt_topics.h, *.modr          │
└────────────┬───────────────────────────────────────────────────┘
             │
             ▼
┌──────────────────────────────────────────────────────────────┐
│  Drivers       Modules        Scenarios (in modesp_scenario) │
│      └────────────┴─────────────┘                            │
│             write/read                                       │
│                ↓                                             │
│        ┌──────────────┐                                      │
│        │  SharedState │  ← single source of runtime truth    │
│        └──────────────┘                                      │
│                ↓                                             │
│  WebUI / WS broadcast │ MQTT publish │ DataLogger │ NVS      │
└──────────────────────────────────────────────────────────────┘
```

This is the entire framework у one picture. Everything else is
mechanics of these four ideas.

## Where to go from here

- **[quickstart.md](quickstart.md)** — see it running у 10 minutes.
- **[installation.md](installation.md)** — toolchain і first build.
- **[02-module-author-guide/overview.md](../02-module-author-guide/overview.md)** —
  write your first module.
- **[03-framework-reference/architecture.md](../03-framework-reference/architecture.md)** —
  full system architecture із init phases, dependency graph.

## Source

This concepts page is а condensed version of:

- **[STYLE.md](../STYLE.md)** — documentation quality bar.
- The Module Author Guide chapters 1-3.
- The architectural overview.
