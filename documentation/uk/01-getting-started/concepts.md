# Concepts — чотири mental моделі

> 📖 **In English:** [documentation/en/01-getting-started/concepts.md](../../en/01-getting-started/concepts.md)

Перед написанням вашого першого module, чотири ідеї explain ~90% того
як фреймворк hangs together. Прочитайте це one раз і rest documentation
much easier to navigate.

Ця сторінка — **не tutorial**. Це **conceptual glossary** до якого ви
будете повертатися.

---

## 1. Manifest-driven

Кожен module ships із **`manifest.json`** що declares що module exposes
— state keys, UI cards, MQTT topics, persistence, logging channels —
**окремо** від C++ code що implements behaviour.

```
modules/simple_thermo/
├── manifest.json           # declarative interface
├── include/
│   └── simple_thermo_module.h
└── src/
    └── simple_thermo_module.cpp
```

Build-time tools (`generate_ui.py`, `compile_scenario.py`) read всі
manifests І emit:

- **`ui.json`** consumed by WebUI runtime.
- **`state_meta.h`** consumed by HTTP/MQTT/SharedState validation.
- **`mqtt_topics.h`** pre-computed topic strings.
- **`.modr`** binaries для recipe-typed manifests.

Ця separation gives **one source of truth per module**. Renaming
state key updates everything coherently — UI, MQTT, SharedState — без
hunting через C++ code.

**Чому це matters:** коли читаєте module, look at manifest first.
Він tells вам що module IS without needing read implementation.

→ Deeper: **[02-module-author-guide/manifest.md](../02-module-author-guide/manifest.md)**.

---

## 2. Modules і drivers — два types citizens

Фреймворк has **exactly two kinds** pluggable units:

| Type | What | Lifetime owned by |
|---|---|---|
| **Module** | Business logic. Reads/writes SharedState keys, owns logic loops. | `ModuleManager` (Phases 1-3 init). |
| **Driver** | Hardware abstraction. Talks to GPIO/I2C/OneWire/ADC. | `DriverManager` (HAL bridge). |

Обидва implement thin base interfaces:

- Module → `class Module : public modesp::BaseModule`.
- Driver → `class Driver : public modesp::IDriver` (з typed sub-interfaces
  `ISensorDriver`, `IActuatorDriver`).

Drivers publish до `equipment.<role>` keys. Modules read ці keys і
write higher-level state — `simple_thermo.output`, `alarm.fire_active`,
тощо. **Modules don't touch hardware directly** — вони consume
hardware abstraction що drivers provide.

Special case — **recipe modules** — modules з `module_type:
"recipe"` whose behaviour — scenario `.modr` binary instead of
C++ class.

→ Deeper: **[02-module-author-guide/overview.md](../02-module-author-guide/overview.md)**,
**[03-framework-reference/components/modesp_hal.md](../03-framework-reference/components/modesp_hal.md)**.

---

## 3. SharedState — runtime communication bus

**Немає inter-module function calls** у runtime. Modules
communicate by reading AND writing keys у typed key-value store:

```cpp
state.set("equipment.air_temp", 23.5);              // driver writes
float t; state.get("equipment.air_temp", t);        // module reads
state.set("simple_thermo.output", true);            // module writes
bool on; state.get("simple_thermo.output", on);     // інший module reads
```

Each key holds typed `StateValue` (variant з `bool`, `int32_t`,
`float`, `etl::string_view`). Type changes rejected at write time
коли validation enabled.

State writes **change-tracked**. Other subsystems (WebSocket
broadcast, DataLogger, MQTT publisher) observe changes WITHOUT polling
every key на every tick.

**Чому це matters:** modules стають trivially testable (stub state,
set inputs, run `on_update`, assert outputs). І topology — read
manifests — no hidden dependencies.

→ Deeper: **[02-module-author-guide/shared-state.md](../02-module-author-guide/shared-state.md)**.

---

## 4. Scenarios — declarative finite-state machines

Більшість real refrigeration / HVAC behaviour — **sequence з phases**:
defrost cycle, evaporator pulldown, alarm acknowledgement, OTA reboot,
cleaning mode. Hard-coding це у C++ means writing FSMs by hand —
error-prone і testing-hostile.

**Scenarios** — declarative finite-state machines authored як JSON
у manifest's `scenario:` block. Compiler turns each у
compact `.modr` binary що `modesp_scenario` engine interprets at
runtime.

Кожен scenario:

- Має **один або more parallel tracks** (concurrent FSMs з cross-track sync).
- Кожен track має **phases** з entry actions і transitions.
- Transitions можуть бути time-based, condition-based, АБО composite (`all_of`).
- Scenario terminates коли its `completion_rule` satisfied.

Simple example (defrost recipe):

```
phase_pump_down → phase_defrost → phase_drain → $complete
   ↓ if temp > 30°C
$fail
```

Recipes — first-class — operators can start/pause/abort їх через
WebUI / HTTP / MQTT. State persisted у NVS через tokens so power
loss recovers mid-scenario.

→ Deeper: **[02-module-author-guide/recipe-authoring.md](../02-module-author-guide/recipe-authoring.md)**,
**[03-framework-reference/components/modesp_scenario.md](../03-framework-reference/components/modesp_scenario.md)**,
**[03-framework-reference/modules/abs_test.md](../03-framework-reference/modules/abs_test.md)**.

---

## Як вони compose

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

Це весь фреймворк у одній picture. Все інше — mechanics цих чотирьох
ідей.

## Куди далі

- **[quickstart.md](quickstart.md)** — see it running за 10 minutes.
- **[installation.md](installation.md)** — toolchain і first build.
- **[02-module-author-guide/overview.md](../02-module-author-guide/overview.md)** —
  write ваш first module.
- **[03-framework-reference/architecture.md](../03-framework-reference/architecture.md)** —
  full system architecture з init phases, dependency graph.

## Source

Ця concepts page — condensed version:

- **[STYLE.md](../STYLE.md)** — documentation quality bar.
- Module Author Guide chapters 1-3.
- Architectural overview.
