# ADR-0004: Recipe = Manifest (з опціональною `scenario` секцією)

**Status:** placeholder. Fully written у Step 2 (compile_scenario.py) і Step 4 (generate_ui.py extension).

## Decision summary

Recipe не існує як окремий тип файлу. Recipe — це повноцінний ModESP module manifest з:
- Standard sections (state, ui, mqtt) — processed by existing `generate_ui.py`
- New `module_type: "recipe"` field
- New optional `scenario` section — processed by new `compile_scenario.py` tool

Один файл, два extractors. Existing UI generation pipeline не переписується.

## Alternatives considered

- Окремі recipe.json файли poza `modules/` directory — rejected (breaks manifest-driven pipeline)
- Recipe authoring у power-user DSL (Tasmota Berry, Lua) — rejected (heap-required, semantics drift)
- Per-recipe directory з multi-file split — rejected (over-engineering для MVP)

## Consequences

- Recipe state keys і UI widgets pre-generated через існуючий pipeline (free per-recipe UX)
- `generate_ui.py` extension — мінімальна (~30 LOC: recognize `module_type: "recipe"`, skip C++ binding)
- Firmware-bundled recipes (firmware rebuild для new recipe — Stage 1.5 для OTA-uploadable)
- SharedState capacity bump 72→256 потрібен для multiple recipes' mirror keys

## References

- Plan Q9 для full spec
- `09_manifest_integration.md` для commerce algorithm details
- User directive: "він має повністтю вписуватись в нашу maifest-driven архітектуру"
