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
2. **[Concepts](01-getting-started/concepts.md)** — four key
   ideas (manifest-driven, modules, scenarios, SharedState).
3. **[Module Author Guide → Overview](02-module-author-guide/overview.md)**
   — start writing your first module.

## Navigation

### 01 — Getting started

| Document | Status | Purpose |
|---|---|---|
| [quickstart.md](01-getting-started/quickstart.md) | ✅ | Flash, configure, run the reference scenario. |
| [installation.md](01-getting-started/installation.md) | ✅ | ESP-IDF install, repo clone, first build. |
| [concepts.md](01-getting-started/concepts.md) | ✅ | Core mental model. |

### 02 — Module Author Guide (Primary audience)

| Document | Status | Purpose |
|---|---|---|
| [overview.md](02-module-author-guide/overview.md) | ✅ | Module types, five core ideas, anatomy. |
| [manifest.md](02-module-author-guide/manifest.md) | ✅ | All manifest sections referenced і explained (module/recipe/driver). |
| [writing-a-module.md](02-module-author-guide/writing-a-module.md) | ✅ | C++ class anatomy + lifecycle hooks. |
| [writing-a-driver.md](02-module-author-guide/writing-a-driver.md) | ✅ | IDriver subclass, registration, sensor/actuator patterns. |
| [shared-state.md](02-module-author-guide/shared-state.md) | ✅ | Read/write patterns, change tracking, type rules. |
| [ui-widgets.md](02-module-author-guide/ui-widgets.md) | ✅ | Full widget catalog, cards, visible_when, i18n. |
| [mqtt.md](02-module-author-guide/mqtt.md) | ✅ | Publish/subscribe semantics, topic format, HA discovery. |
| [persistence.md](02-module-author-guide/persistence.md) | ✅ | NVS through PersistService, debounce, migrations. |
| [recipe-authoring.md](02-module-author-guide/recipe-authoring.md) | ✅ | Scenario recipe structure, tracks, phases, transitions. |
| [recipe-actions.md](02-module-author-guide/recipe-actions.md) | ✅ | Built-in actions/conditions, custom registration. |
| [continuous-behaviors.md](02-module-author-guide/continuous-behaviors.md) | ✅ | PID, hysteresis, ramp; custom. |
| [debugging.md](02-module-author-guide/debugging.md) | ✅ | Logs, HTTP / WS inspection, common bugs. |
| [best-practices.md](02-module-author-guide/best-practices.md) | ✅ | Patterns і anti-patterns checklist. |

### 03 — Framework reference

| Document | Status | Purpose |
|---|---|---|
| [architecture.md](03-framework-reference/architecture.md) | ✅ | System layers, dependencies, init phases. |
| [components/modesp_core.md](03-framework-reference/components/modesp_core.md) | ✅ | SharedState, BaseModule, ModuleManager, App. |
| [components/modesp_hal.md](03-framework-reference/components/modesp_hal.md) | ✅ | HAL abstractions, IDriver, DriverManager. |
| [components/modesp_services.md](03-framework-reference/components/modesp_services.md) | ✅ | Logger, Watchdog, Persist, Config, Error, SystemMonitor. |
| [components/modesp_net.md](03-framework-reference/components/modesp_net.md) | ✅ | Wi-Fi, HTTP server, WebSocket. |
| [components/modesp_mqtt.md](03-framework-reference/components/modesp_mqtt.md) | ✅ | MQTT client wrapper із TLS і HA discovery. |
| [components/modesp_aws.md](03-framework-reference/components/modesp_aws.md) | ✅ | AWS IoT alternative backend. |
| [components/modesp_json.md](03-framework-reference/components/modesp_json.md) | ✅ | JSON parsing utilities (jsmn wrapper). |
| [components/modesp_scenario.md](03-framework-reference/components/modesp_scenario.md) | ✅ | Scenario engine high-level overview. |
| scenario-engine/ | ⏳ planned | Engine deep dive (will link to migrated content as bridge). |
| [modules/equipment.md](03-framework-reference/modules/equipment.md) | ✅ | Equipment Manager — sensor/actuator HAL bridge. |
| [modules/datalogger.md](03-framework-reference/modules/datalogger.md) | ✅ | Channel logging, retention, plot API. |
| [modules/simple_thermo.md](03-framework-reference/modules/simple_thermo.md) | ✅ | Reference ON/OFF thermostat. |
| [modules/abs_test.md](03-framework-reference/modules/abs_test.md) | ✅ | Reference recipe з two parallel tracks. |
| [drivers/ds18b20.md](03-framework-reference/drivers/ds18b20.md) | ✅ | Dallas OneWire temperature sensor. |
| [drivers/ntc.md](03-framework-reference/drivers/ntc.md) | ✅ | NTC thermistor via ADC. |
| [drivers/relay.md](03-framework-reference/drivers/relay.md) | ✅ | GPIO relay actuator. |
| [drivers/pcf8574_relay.md](03-framework-reference/drivers/pcf8574_relay.md) | ✅ | I2C-expanded relay (PCF8574). |
| [drivers/digital_input.md](03-framework-reference/drivers/digital_input.md) | ✅ | GPIO contact input. |
| [drivers/pcf8574_input.md](03-framework-reference/drivers/pcf8574_input.md) | ✅ | I2C-expanded contact input. |
| web-ui.md | ⏳ planned | Svelte SPA architecture, state stores. |

### 04 — Hardware

| Document | Status | Purpose |
|---|---|---|
| [board-config.md](04-hardware/board-config.md) | ✅ | `board.json` schema і examples. |
| [bindings.md](04-hardware/bindings.md) | ✅ | `bindings.json` — driver-to-role mapping. |
| [ota.md](04-hardware/ota.md) | ✅ | OTA flow, rollback, partition layout. |
| [deployment.md](04-hardware/deployment.md) | ✅ | Flash, monitor, factory reset. |

### 05 — Tools

| Document | Status | Purpose |
|---|---|---|
| [generate_ui.md](05-tools/generate_ui.md) | ✅ | Build-time generator overview. |
| [compile_scenario.md](05-tools/compile_scenario.md) | ✅ | Recipe compiler і `.modr` format. |
| [dump_modr.md](05-tools/dump_modr.md) | ✅ | `.modr` inspector / debugger. |

### 06 — Contributing

| Document | Status | Purpose |
|---|---|---|
| [development-setup.md](06-contributing/development-setup.md) | ✅ | Development environment. |
| [testing.md](06-contributing/testing.md) | ✅ | Host tests, HIL tests, fuzz. |
| [code-style.md](06-contributing/code-style.md) | ✅ | C++ conventions. |
| [docs-style.md](06-contributing/docs-style.md) | ✅ | Cross-references [STYLE.md](../STYLE.md). |

### ADR — Architecture Decision Records

Located у [adr/](adr/) once written. Engine-specific decisions are у the
scenario engine section.

## Status

This documentation is а **clean-slate strategic rewrite** following the
`modesp_sequence` → `modesp_scenario` engine rebuild. All core pages are
now ✅ ready; remaining ⏳ planned entries (e.g. `scenario-engine/` deep
dive, `web-ui.md`) are scheduled for upcoming sessions.

The previous `docs/` directory remains accessible as **legacy reference** —
some pages there are still factually correct, some are outdated. None are
authoritative until rewritten under `documentation/`.

## Contributing

Read **[STYLE.md](../STYLE.md)** перед writing or editing any page. The
style guide locks the quality bar для це directory.
