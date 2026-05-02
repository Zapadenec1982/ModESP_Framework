# 08 — Lifecycle (Build-Time + Runtime)

**Status:** placeholder. Заповнюється у Step 14 (sequence_engine.cpp).

## Заповнюється

- **Build-time lifecycle:**
  - Recipe authored у `modules/<recipe_name>/manifest.json`
  - CMake pre-build invokes `tools/generate_ui.py` (existing — reads state/ui/mqtt sections)
  - CMake pre-build invokes `tools/compile_scenario.py` (NEW — reads scenario section, emits `.modr`)
  - LittleFS image bundled з `data/scenarios/*.modr`
  - Firmware flash includes engine code + recipe binaries
- **Runtime lifecycle:**
  - Boot → engine `on_init()` → recovery for any persisted scenarios → enter PAUSED for recovered
  - Business module викликає `engine.load(name)` → engine reads .modr, validates against ActionRegistry, returns handle
  - `engine.start(h)` → atomic resource acquire → state RUNNING → first phase entry actions
  - Tick loop: global transitions → per-track phase progression → action execution → mirror keys updated
  - Completion / abort → exit actions → resources released → state COMPLETED / FAILED
- Diagram: from manifest authoring до runtime execution

## Reference

- Plan Q9 (manifest integration), Q2 (API), Q6 (state machines).
