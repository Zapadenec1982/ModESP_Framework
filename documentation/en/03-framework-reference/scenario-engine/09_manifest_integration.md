# 09 — Manifest Integration

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/09_manifest_integration.md](../../../uk/03-framework-reference/scenario-engine/09_manifest_integration.md)

**Status:** Filled in Step 2a (compile_scenario.py landed). Updated in Step 4 (generate_ui.py extension).
**ADR:** [`adr/0004-recipe-as-manifest.md`](adr/0004-recipe-as-manifest.md)
**Code:** [`tools/compile_scenario.py`](../../tools/compile_scenario.py), [`tools/scenario_schema.json`](../../tools/scenario_schema.json), [`tools/known_actions.json`](../../tools/known_actions.json)

## Core principle

> **A recipe IS a manifest.** There is no separate file format. The existing ModESP manifest pipeline (which already handles the state, ui, mqtt, and loggable sections) is extended with an optional `scenario` section.

One JSON file, two extractors:

| Extractor | Reads section | Output |
|---|---|---|
| `tools/generate_ui.py` (existing — minor extension in Step 4) | `state`, `ui`, `mqtt`, `loggable`, `features` | `data/ui.json`, `generated/state_meta.h`, `generated/mqtt_topics.h`, etc. |
| `tools/compile_scenario.py` (new in Step 2) | `scenario` | `data/scenarios/<recipe_name>.modr` (binary) |

## Recipe manifest skeleton

```jsonc
{
  "manifest_version": 1,
  "module": "recipe_my_process",       // module name; used as NVS key and file path
  "module_type": "recipe",             // NEW field — distinguishes recipe from C++-backed module
  "version": "1.0.0",
  "priority": 5,                       // ignored for recipes (no C++ runtime)

  "state": {
    // Mirror state keys the engine WILL write at runtime.
    // Cross-validated by compile_scenario.py (Step 2b feature).
    "recipe_my_process.scenario_state": {"type": "string", "access": "read"},
    "recipe_my_process.main_phase_name": {"type": "string", "access": "read"}
  },

  "ui": {
    // Standard UI section — generate_ui.py emits widgets.
    "page": "Process",
    "icon": "play",
    "cards": [{
      "title": "Status",
      "visible_when": {"recipe_my_process.scenario_state": ["running", "paused"]},
      "widgets": [{"key": "recipe_my_process.main_phase_name", "widget": "value"}]
    }]
  },

  "mqtt": {
    "publish": ["recipe_my_process.scenario_state"]
  },

  "scenario": {                        // NEW section — compile_scenario.py emits .modr
    "default_phase_timeout_ms": 60000,
    "completion_rule": "all_tracks_complete",
    "tracks": [
      {
        "name": "main",
        "flags": ["main_track"],
        "phases": [
          {"name": "init", "transitions": [{"to": "$complete"}]}
        ]
      }
    ]
  }
}
```

## Build pipeline integration

Both tools run pre-build (via CMake hooks). Their order is independent — neither depends on the other's output. Both scan `modules/*/manifest.json`.

### `compile_scenario.py` algorithm

1. **Discovery:** scan `modules/*/manifest.json`, filter to those with `"module_type": "recipe"` AND a `"scenario"` key present.

2. **JSON load:** parse the manifest. Errors are caught and re-raised as `CompileError` with file:line:col.

3. **Schema validation** ([`tools/scenario_schema.json`](../../tools/scenario_schema.json), draft-07):
    - Required fields (`default_phase_timeout_ms`, `completion_rule`, `tracks`)
    - Type/enum constraints
    - Array bounds (max 6 tracks, max 32 phases per track, max 8 transitions per phase)
    - Pattern constraints (track name `^[a-z][a-z0-9_]{0,7}$`, phase name `^[a-z][a-z0-9_]{0,15}$`)
    - Composable conditions (`all_of`/`any_of`/`not` recursive)
    Errors → **E0101** (a single error code captures all schema failures with a detailed inner message).

4. **Semantic uniqueness** (E0208):
    - Track names unique within a scenario
    - Phase names unique within each track (different tracks may reuse names)
    - JSON Schema's `uniqueItems` does not apply to arrays of objects, so an explicit check is required.

5. **Hash resolution:**
    - Action/condition names → djb2_hash16 low-16
    - Cross-checked against the `tools/known_actions.json` registry
    - Collision detection: two known actions with an identical hash → **E0203**
    - Hash mismatch (stored ≠ computed): **E0202**

6. **Cross-validation** (E04XX, deferred to Step 2b):
    - Mirror state keys the engine writes are derived from the scenario section
    - Compared against the manifest's `state` section
    - Any mismatch raises a compile error

7. **Binary emission** (per [`02_binary_format.md`](02_binary_format.md)):
    - String pool interning (length-prefixed dedup)
    - Layout planning (offsets pre-computed)
    - Header → tracks → phase tables → transition arrays → resources → string pool → CRC32
    - All structs naturally aligned (no packing)
    - Total size verified against the header.total_size field

8. **CRC32 trailer:** CRC-32/ISO-HDLC over the entire body (matches Python `zlib.crc32` AND ESP-IDF `esp_crc32_le`).

9. **Output:** `data/scenarios/<module_name>.modr` (bundled in LittleFS at firmware flash time).

### Error message format

```
<file>:<line>:<col>: error[<code>]: <human message>
```

Example:
```
modules/recipe_plov/manifest.json:42:18: error[E0207]: transition target 'wrong_phase' from phase 'simmer' not found in track 'heat'. Valid targets: ['warmup', 'simmer', 'finish'] + ['$complete', '$abort']
```

### Error code catalog

| Code | Class | Trigger |
|------|-------|---------|
| E0001 | Setup | Required Python package missing (jsonschema) |
| E0101 | Schema | JSON Schema draft-07 validation failure (any) |
| E0102 | Parse | Malformed JSON |
| E0103 | Manifest | Missing `module` field |
| E0104 | Manifest | Wrong/missing `module_type` (must be "recipe") |
| E0105 | Manifest | Missing `scenario` section |
| E0202 | Registry | known_actions.json hash ≠ djb2_hash16(name) |
| E0203 | Registry | Two names in known_actions.json with an identical hash (collision) |
| E0204 | Semantics | Phase-scope resources used (deferred to Step 2b) |
| E0205 | Semantics | Global transitions used (deferred to Step 2b) |
| E0206 | Semantics | Conditional transitions used (deferred to Step 2b) |
| E0207 | Semantics | Transition target references an unknown phase |
| E0208 | Semantics | Duplicate track or phase name |
| E0210 | Semantics | Unknown condition operator |
| E0211 | Semantics | Condition expression must be a single-key object |
| E0212 | Semantics | `time_elapsed_ms` requires a non-negative integer |
| E0213 | Semantics | `state_key_*` missing a required field (key/value) |
| E0214 | Semantics | `time_of_day_eq` missing hh/mm |
| E0215 | Semantics | `all_of`/`any_of` requires a non-empty array |
| E0216 | Semantics | String value in a condition without string_pool context |
| E0217 | Semantics | Unsupported value type for a condition param |
| E0218 | Semantics | Composite condition exceeds max nesting depth (16) — DoS guard |
| E0220 | Action | Action invocation missing an 'action' field |
| E0221 | Action | Action params must be an object |
| E0222 | Action | Action param count mismatch vs descriptor |
| E0223 | Globals | Global transition `to` not "$abort" or omitted |
| E0224 | Globals | Global transition missing `when` clause |
| E0225 | Action | `set_state` 'type' param invalid (must be i32/f32/bool) |
| E0301 | Emission | String exceeds u8 length limit (>255 bytes) |
| E0302 | Emission | Compiled binary exceeds MODR_MAX_SIZE (16 KB) |
| E0303 | Emission | Internal: emitted bytes ≠ header.total_size |
| E0226 | Strict | Unknown action name (--strict mode elevates W0220) |
| E0231 | Strict | Unknown ContinuousBehavior (--strict mode elevates W0230) |
| E0401 | Cross-val | manifest.state missing mirror key declarations |
| E0402 | Cross-val | Derived mirror key exceeds the 32-char SharedState budget |
| E0403 | Cross-val | Type mismatch — manifest declares the wrong type for a mirror key |

### Warnings (non-blocking, default mode)

| Code | Class | Trigger | Becomes in --strict |
|------|-------|---------|-------------------|
| W0220 | Action | Unknown action name (domain module must register it at runtime) | E0226 |
| W0230 | Continuous | Unknown ContinuousBehavior reference in `phase.continuous` | E0231 |

### CLI flags

- `--strict` — elevate warnings to errors. This is an industry-standard pattern (TypeScript `--strict`, GCC `-Werror`, ESLint `--max-warnings 0`). Use in CI to detect typos and drift.

Full descriptions with examples → [`10_error_model.md`](10_error_model.md) (filled in Step 2b once accumulated).

## `generate_ui.py` extensions (Step 4)

Minimal changes (~30 LOC):
- Recognize `"module_type": "recipe"` as a valid value (currently only "module").
- Skip C++ binding generation for recipes (no `<name>_module.cpp` registered in `module_register.h`).
- Recognize the `"scenario"` section as valid (skip — handled by compile_scenario.py).

UI side: existing widgets handle recipe state keys without changes. `visible_when` constraints (already a feature of generate_ui.py) show recipe widgets only when the scenario is active.

## Golden file infrastructure

`tools/tests/fixtures/scenarios/<name>.modr` — committed binaries. Pytest reads them and compares byte-exact against the builder/compiler output.

Update procedure (Step 2b coming):
```bash
python tools/compile_scenario.py --regenerate-goldens
```
An explicit confirmation prompt is required — golden updates require review.

## Distribution model

Recipes are firmware-shipped (bundled in LittleFS at flash time). Adding or changing a recipe currently requires a firmware rebuild. This matches the MVP scope.

**Stage 1.5 enhancement:** OTA-uploadable recipes via generic mirror state keys (when needed). The recipe-as-manifest model still applies; only the delivery mechanism additionally supports cloud push.

## Naming and budget constraints

| Constraint | Limit | Rationale |
|---|---|---|
| Recipe (module) name | ≤ 12 chars (`MAX_RECIPE_NAME_LEN`) | SharedState key budget (32 chars total) |
| Track name | ≤ 8 chars (`MAX_TRACK_NAME_LEN`) | Same budget — `recipe_X.track_field` ≤ 32 |
| Phase name | ≤ 16 chars | Loose — used only in the `phase_name` mirror key, not concatenated |
| Tracks per scenario | ≤ 6 (`MAX_TRACKS_PER_SCENARIO`) | RAM budget (~32 B per track instance × 4 instances) |
| Phases per track | ≤ 32 | Generous; realistic recipes have 3–12 phases |
| Transitions per phase | ≤ 8 | Per-tick eval cost cap |
| Resources per scenario | ≤ 32 | Engine ownership map size |
| Total .modr size | ≤ 16 KB (`MODR_MAX_SIZE`) | Buffer pre-allocated per loaded scenario |

## See also

- [02_binary_format.md](02_binary_format.md) — `.modr` byte layout details
- [10_error_model.md](10_error_model.md) — error code descriptions with trigger examples
- [usage/02_writing_recipes.md](usage/02_writing_recipes.md) — author-facing guide
- [adr/0004-recipe-as-manifest.md](adr/0004-recipe-as-manifest.md) — design rationale
- Plan `.claude/plans/quirky-imagining-lake.md` Q9 — original spec
