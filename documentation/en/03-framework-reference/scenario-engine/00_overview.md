# 00 — Scenario Engine Overview

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/00_overview.md](../../../uk/03-framework-reference/scenario-engine/00_overview.md)

## What it is

`components/modesp_scenario/` is a **reusable library for building arbitrary time-dependent algorithms** that C++ business modules in ModESP_v4 call programmatically to execute phased timeline scenarios.

The engine solves one specific problem: **today, business modules duplicate timeline logic by hand** (state machines, phase counters, transition guards) and need elaborate synchronization between parallel processes. The engine provides composable primitives.

## Audience

**Primary consumer:** C++ authors of business modules (multicooker, fermenter, lab thermal cycle, irrigation controller, etc.). The engine is their library.

**NOT a consumer:** end-users in the WebUI. Authoring scenarios through the UI is a separate add-on task, **out of scope for this engine**.

## What problems it solves

1. **Phased timelines with transitions** — without hand-rolled state enum + counter scaffolding.
2. **Parallel tracks within ONE scenario** (like MIDI tracks) — without custom synchronization between business module subroutines.
3. **Multi-instance independent scenarios** (up to `MAX_SEQUENCES` concurrent, default 2 — Kconfig `CONFIG_MODESP_MAX_SEQUENCES`) — the engine provides the handles.
4. **Resource arbitration** (ISA-88 §5.3) — the recipe declares required resources, the engine ensures no conflicts at start.
5. **Power-loss recovery** — NVS-persisted token state restores the phase position on boot.
6. **Domain-agnostic primitives** + extensibility via ActionRegistry — domain modules add custom actions without changing the engine.

## Non-goals

- WebUI authoring (out of scope for the engine).
- Hard real-time control (the engine ticks at 100 Hz; tighter loops need direct module logic).
- Cross-module hardware arbitration (Stage 1.5; in the MVP it is last-write-wins).
- Compile-time / formal model checking (the compiler catches some semantic errors but not deadlock detection).

## Hardware target

- **Baseline:** ESP32-WROOM-32 (minimum spec, primary development target).
- The engine code is target-agnostic via ESP-IDF abstraction; other ESP32 variants work out of the box.

## Reading order for new contributors

1. `01_architecture.md` — high-level architecture diagram
2. `09_manifest_integration.md` — how recipes fit into the build pipeline
3. `usage/01_quickstart.md` — minimal hands-on example
4. `usage/02_writing_recipes.md` — authoring guide
5. ADRs `0001-0007` — critical architectural decisions with rationale
