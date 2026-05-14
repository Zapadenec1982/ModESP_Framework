# ADR-0004: Recipe = Manifest (with an optional `scenario` section)

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/adr/0004-recipe-as-manifest.md](../../../../uk/03-framework-reference/scenario-engine/adr/0004-recipe-as-manifest.md)

**Status:** placeholder. Fully written in Step 2 (compile_scenario.py) and Step 4 (generate_ui.py extension).

## Decision summary

A recipe does not exist as a separate file type. A recipe is a full-fledged ModESP module manifest with:
- Standard sections (state, ui, mqtt) — processed by the existing `generate_ui.py`
- A new `module_type: "recipe"` field
- A new optional `scenario` section — processed by a new `compile_scenario.py` tool

One file, two extractors. The existing UI generation pipeline is not rewritten.

## Alternatives considered

- Separate `recipe.json` files outside the `modules/` directory — rejected (breaks the manifest-driven pipeline)
- Recipe authoring in a power-user DSL (Tasmota Berry, Lua) — rejected (heap-required, semantics drift)
- Per-recipe directory with multi-file split — rejected (over-engineering for MVP)

## Consequences

- Recipe state keys and UI widgets are pre-generated through the existing pipeline (free per-recipe UX)
- `generate_ui.py` extension is minimal (~30 LOC: recognize `module_type: "recipe"`, skip C++ binding)
- Firmware-bundled recipes (firmware rebuild required for a new recipe — OTA-uploadable in Stage 1.5)
- SharedState capacity bump 72→256 is required for multiple recipes' mirror keys

## References

- Plan Q9 for the full spec
- `09_manifest_integration.md` for the merge algorithm details
- User directive: "it must fit completely into our manifest-driven architecture"
