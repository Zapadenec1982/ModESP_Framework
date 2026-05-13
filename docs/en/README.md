# ModESP v4 — Documentation

> 📖 **Українська версія:** [docs/uk/](../uk/README.md)

ModESP v4 is а **manifest-driven ESP32 firmware framework** that lets you ship
hardware-aware applications without rebuilding the platform every time.
Drop in module manifests, run the build, get а production-ready firmware із
auto-generated state schema, WebUI widgets, MQTT topics, and OTA support.

This documentation is structured for **module authors** — engineers who want
to write business-logic modules і scenario recipes on top of the framework.
Other audiences (framework contributors, hardware integrators, operators)
have dedicated sections too.

## Start Here

If you're new — read these in order:

1. **[Getting started → Quickstart](01-getting-started/quickstart.md)** —
   boot the reference scenario `abs_test` on real hardware у under 10 minutes.
2. **[Getting started → Concepts](01-getting-started/concepts.md)** —
   the four key ideas (manifest-driven, modules, scenarios, SharedState).
3. **[Module Author Guide → Overview](02-module-author-guide/overview.md)** —
   start writing your first module.

## Navigation

### 01 — Getting Started

| Document | Purpose |
|---|---|
| [installation.md](01-getting-started/installation.md) | Install ESP-IDF, clone repo, first build. |
| [quickstart.md](01-getting-started/quickstart.md) | Flash і run the reference scenario. |
| [concepts.md](01-getting-started/concepts.md) | Core mental model. |
| [tutorial-legacy.md](01-getting-started/tutorial-legacy.md) | (Legacy) thermostat module walkthrough. |

### 02 — Module Author Guide (Primary audience)

| Document | Purpose |
|---|---|
| [overview.md](02-module-author-guide/overview.md) | What is а module; lifecycle; mental model. |
| [manifest.md](02-module-author-guide/manifest.md) | All manifest sections explained. |
| writing-a-module.md *(planned)* | C++ class anatomy + registration. |
| shared-state.md *(planned)* | Reading and writing state from а module. |
| ui-widgets.md *(planned)* | Generating UI cards z manifest. |
| mqtt.md *(planned)* | Pub/sub setup. |
| persistence.md *(planned)* | NVS via PersistService. |
| recipe-authoring.md *(planned)* | Writing scenario recipes. |
| recipe-actions.md *(planned)* | Built-in vs custom actions. |
| continuous-behaviors.md *(planned)* | PID, hysteresis, ramp; custom. |
| debugging.md *(planned)* | Logs, HTTP API for state. |
| best-practices.md *(planned)* | Patterns and anti-patterns. |

### 03 — Framework Reference

| Document | Purpose |
|---|---|
| [architecture.md](03-framework-reference/architecture.md) | System layers, dependency diagram. |
| [components/](03-framework-reference/components/) | Per-component reference (9 components). |
| [modules/](03-framework-reference/modules/) | Per-module reference. |
| [scenario-engine/](03-framework-reference/scenario-engine/) | Scenario engine deep dive (`modesp_scenario`). |
| [web-ui.md](03-framework-reference/web-ui.md) | Svelte SPA architecture *(planned, currently UK only)*. |

### 04 — Hardware

| Document | Purpose |
|---|---|
| [board-config.md](04-hardware/board-config.md) | `board.json` schema і examples. |
| bindings.md *(planned)* | `bindings.json`: driver-to-GPIO mapping. |
| ota.md *(planned)* | OTA flow, rollback, dual-image partition layout. |
| deployment.md *(planned)* | Flash, monitor, factory reset. |

### 05 — Tools

| Document | Purpose |
|---|---|
| generate_ui.md *(planned)* | Build-time generator for state schema, UI, MQTT topics. |
| compile_scenario.md *(planned)* | Recipe compiler (`.modr` binary format). |
| dump_modr.md *(planned)* | Recipe inspector / debugger. |

### 06 — Contributing

| Document | Purpose |
|---|---|
| development-setup.md *(planned)* | Setting up а development environment. |
| testing.md *(planned)* | Host tests, HIL tests, fuzz tests. |
| code-style.md *(planned)* | C++ conventions, naming, comment style. |
| docs-style.md *(planned)* | How to update і maintain these docs. |

### ADR — Architecture Decision Records

Located у [adr/](adr/). Engine-specific ADRs у
[03-framework-reference/scenario-engine/adr/](03-framework-reference/scenario-engine/adr/).

## Status

This documentation set is being rebuilt as part of the `modesp_sequence` →
`modesp_scenario` engine rebuild (see [CHANGELOG](../../CHANGELOG.md) entries
under "Phase 0..4"). Pages marked *(planned)* are scheduled for upcoming
sessions. Existing content has been migrated and stale references cleaned
up, but content quality varies until the dedicated rewrite passes.

## Contributing to These Docs

This is **plain Markdown**. No build step, no static site generator. Edit
files directly, send а PR (or commit on the feature branch). Conventions:

- One file == one topic.
- Every page begins з а 1-paragraph "what і why" lede.
- Code examples must be copy-paste runnable (no pseudocode without explicit
  callout `<!-- pseudocode -->`).
- Bilingual: every EN page has а UK counterpart at the matching path under
  [docs/uk/](../uk/). Translations are independent — update both when content
  changes.
