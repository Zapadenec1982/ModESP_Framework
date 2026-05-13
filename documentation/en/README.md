# ModESP v4 — Documentation

> 📖 **Українська версія:** [documentation/uk/](../uk/README.md)
> 📋 **Style guide:** [STYLE.md](../STYLE.md)

ModESP v4 is а **manifest-driven ESP32 firmware framework**. Drop у module
manifests, run the build, ship а production-ready firmware із auto-generated
state schema, WebUI widgets, MQTT topics, OTA, і LittleFS partitioning.

This documentation targets **module authors** — engineers writing business
modules і scenario recipes on top of the framework. Other audiences
(framework contributors, hardware integrators, operators) have dedicated
sections too.

## Start here

In order:

1. **[Quickstart](01-getting-started/quickstart.md)** — flash а device,
   run the reference scenario, see live state у WebUI. Under 10 minutes.
2. **[Concepts](01-getting-started/concepts.md)** *(planned)* — four key
   ideas (manifest-driven, modules, scenarios, SharedState).
3. **[Module Author Guide → Overview](02-module-author-guide/overview.md)**
   — start writing your first module.

## Navigation

### 01 — Getting started

| Document | Status | Purpose |
|---|---|---|
| [quickstart.md](01-getting-started/quickstart.md) | ✅ | Flash, configure, run the reference scenario. |
| installation.md | ⏳ planned | ESP-IDF install, repo clone, first build. |
| concepts.md | ⏳ planned | Core mental model. |

### 02 — Module Author Guide (Primary audience)

| Document | Status | Purpose |
|---|---|---|
| [overview.md](02-module-author-guide/overview.md) | ✅ | Module types, five core ideas, anatomy. |
| manifest.md | ⏳ planned | All manifest sections referenced і explained. |
| writing-a-module.md | ⏳ planned | C++ class anatomy + lifecycle hooks. |
| shared-state.md | ⏳ planned | Read/write patterns, change tracking. |
| ui-widgets.md | ⏳ planned | Declarative UI generation. |
| mqtt.md | ⏳ planned | Pub/sub setup. |
| persistence.md | ⏳ planned | NVS through PersistService. |
| recipe-authoring.md | ⏳ planned | Scenario recipe structure. |
| recipe-actions.md | ⏳ planned | Built-in actions і custom registration. |
| continuous-behaviors.md | ⏳ planned | PID, hysteresis, ramp; custom. |
| debugging.md | ⏳ planned | Logs, HTTP API for state. |
| best-practices.md | ⏳ planned | Patterns і anti-patterns. |

### 03 — Framework reference

| Document | Status | Purpose |
|---|---|---|
| architecture.md | ⏳ planned | System layers, dependencies, init phases. |
| components/modesp_core.md | ⏳ planned | SharedState, BaseModule, ModuleManager, App. |
| components/modesp_hal.md | ⏳ planned | HAL abstractions, IDriver, DriverManager. |
| components/modesp_services.md | ⏳ planned | Logger, Watchdog, Persist, Config, Error. |
| components/modesp_net.md | ⏳ planned | Wi-Fi, HTTP server, WebSocket. |
| components/modesp_mqtt.md | ⏳ planned | MQTT client wrapper. |
| components/modesp_aws.md | ⏳ planned | AWS IoT alternative backend. |
| components/modesp_json.md | ⏳ planned | JSON parsing і serialization utilities. |
| components/modesp_scenario.md | ⏳ planned | Scenario engine high-level overview. |
| scenario-engine/ | ⏳ planned | Engine deep dive (will link to migrated content as bridge). |
| modules/equipment.md | ⏳ planned | Equipment Manager — sensor/actuator HAL bridge. |
| modules/datalogger.md | ⏳ planned | Channel logging, retention, plot API. |
| modules/simple_thermo.md | ⏳ planned | Reference ON/OFF thermostat. |
| modules/abs_test.md | ⏳ planned | Reference recipe з two parallel tracks. |
| web-ui.md | ⏳ planned | Svelte SPA architecture, state stores. |

### 04 — Hardware

| Document | Status | Purpose |
|---|---|---|
| board-config.md | ⏳ planned | `board.json` schema і examples. |
| bindings.md | ⏳ planned | `bindings.json` — driver-to-GPIO mapping. |
| ota.md | ⏳ planned | OTA flow, rollback, partition layout. |
| deployment.md | ⏳ planned | Flash, monitor, factory reset. |

### 05 — Tools

| Document | Status | Purpose |
|---|---|---|
| generate_ui.md | ⏳ planned | Build-time generator overview. |
| compile_scenario.md | ⏳ planned | Recipe compiler і `.modr` format. |
| dump_modr.md | ⏳ planned | `.modr` inspector / debugger. |

### 06 — Contributing

| Document | Status | Purpose |
|---|---|---|
| development-setup.md | ⏳ planned | Development environment. |
| testing.md | ⏳ planned | Host tests, HIL tests, fuzz. |
| code-style.md | ⏳ planned | C++ conventions. |
| docs-style.md | ⏳ planned | Cross-references [STYLE.md](../STYLE.md). |

### ADR — Architecture Decision Records

Located у [adr/](adr/) once written. Engine-specific decisions are у the
scenario engine section.

## Status

This documentation is а **clean-slate strategic rewrite** following the
`modesp_sequence` → `modesp_scenario` engine rebuild. Pages marked ⏳
**planned** are scheduled для upcoming sessions.

The previous `docs/` directory remains accessible as **legacy reference** —
some pages there are still factually correct, some are outdated. None are
authoritative until rewritten under `documentation/`.

## Contributing

Read **[STYLE.md](../STYLE.md)** перед writing or editing any page. The
style guide locks the quality bar для це directory.
