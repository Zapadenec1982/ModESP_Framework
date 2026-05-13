# 00 — Sequence Engine Overview

**Status:** Stage 0 — placeholder. Filled out as architecture stabilizes (Step 1+).

## Що це

`components/modesp_scenario/` — це **переюзабельна бібліотека для побудови довільних часозалежних алгоритмів**, яку C++ business modules ModESP_v4 викликають програмно для виконання phased timeline scenarios.

Engine вирішує одну конкретну проблему: **сьогодні business modules дублюють timeline logic вручну** (state machines, phase counters, transition guards) і змушені складно синхронізувати кілька паралельних процесів. Engine надає composable примітиви.

## Для кого

**Primary consumer:** C++ автори business modules (multicooker, fermenter, lab thermal cycle, irrigation controller, тощо). Engine — це їхня бібліотека.

**NOT consumer:** end-users у WebUI. Authoring scenarios через UI — окрема задача-надбудова, **поза цим engine**.

## Які проблеми вирішує

1. **Phased timelines з transitions** — без ручного state enum + counter scaffolding.
2. **Parallel tracks within ONE scenario** (як MIDI tracks) — без custom synchronization between business module subroutines.
3. **Multi-instance independent scenarios** (до 6 concurrent) — handles надає engine.
4. **Resource arbitration** (ISA-88 §5.3) — recipe declares required resources, engine ensures no conflicts at start.
5. **Power-loss recovery** — NVS-persisted token state restores phase position на boot.
6. **Domain-agnostic primitives** + extensibility через ActionRegistry — domain modules add custom actions без зміни engine.

## Що NOT вирішує

- WebUI authoring (поза engine).
- Hard real-time control (engine ticks at 100 Hz; tighter loops треба direct module logic).
- Cross-module hardware arbitration (Stage 1.5; MVP — last-write-wins).
- Compile-time / formal model checking (compiler ловить деякі semantic errors, але не deadlock detection).

## Hardware target

- **Baseline:** ESP32-WROOM-32 (minimum spec, primary development target).
- Engine код target-agnostic через ESP-IDF abstraction; інші ESP32 variants працюють "out of the box".

## Reading order для нових contributors

1. `01_architecture.md` — high-level architecture діаграма
2. `09_manifest_integration.md` — як recipes вписуються у build pipeline
3. `usage/01_quickstart.md` — minimal hands-on example
4. `usage/02_writing_recipes.md` — authoring guide
5. ADRs `0001-0007` — критичні architectural decisions з обґрунтуваннями
