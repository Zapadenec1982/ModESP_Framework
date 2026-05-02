# 09 — Manifest Integration

**Status:** placeholder. Заповнюється у Step 2 (compile_scenario.py) і Step 4 (generate_ui.py extension).

## Заповнюється

- **Recipe = manifest** principle:
  - `modules/<recipe_name>/manifest.json` з `module_type: "recipe"`
  - Standard sections (state, ui, mqtt, loggable) processed by existing `generate_ui.py`
  - NEW `scenario` section processed by `compile_scenario.py`
  - One file, two extractors
- **`generate_ui.py` extensions** (~30 LOC):
  - Recognize `module_type: "recipe"` allowed value
  - Skip C++ binding generation (no `<name>_module.cpp` register)
  - Recognize `scenario` section as valid (skip — handled by compile_scenario.py)
- **`compile_scenario.py` algorithm:**
  - Scan `modules/*/manifest.json`
  - Filter: has `module_type: "recipe"` AND `scenario` key
  - Validate against `tools/scenario_schema.json`
  - Resolve action/condition hashes (collision detection via `tools/known_actions.json`)
  - Cross-validate: every state key engine WILL write MUST be declared у manifest's `state` section
  - Emit `data/scenarios/<recipe_name>.modr`
- **Compiler error message specification:**
  - Format: `<file>:<line>:<col>: error[<code>]: <human message>`
  - Example: `modules/recipe_plov/manifest.json:42:18: error[E0203]: phase 'simmer' references undefined transition target '$wrong'. Valid targets: phase_a, phase_b, $complete, $abort.`
  - Code prefixes: E01XX (schema), E02XX (semantics — references, types), E03XX (binary emit), E04XX (cross-validation з manifest state)
- **Golden file infrastructure:**
  - `tools/tests/fixtures/scenarios/` зберігає `.modr` golden binaries + `.json` source
  - Pytest recompiles, byte-compares
  - Update procedure: `python tools/compile_scenario.py --regenerate-goldens` з explicit confirm prompt
- WebUI integration via `visible_when` constraints — show recipe widgets лише коли scenario active

## Reference

- ADR-0004 для recipe-as-manifest decision rationale
- Plan Q9 для current spec
