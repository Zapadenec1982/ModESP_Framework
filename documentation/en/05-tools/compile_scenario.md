# `compile_scenario.py` — recipe manifest → `.modr` binary

> 📖 **Українською:** [documentation/uk/05-tools/compile_scenario.md](../../uk/05-tools/compile_scenario.md)

Build-time tool that compiles `scenario:` blocks из recipe manifests
into а compact binary format (`.modr`) consumed by the scenario engine.

Parallel to `generate_ui.py` — same role on the build side, different
output: instead of generating C++ headers AND `ui.json`, це emits one
`.modr` file per recipe. The engine `Engine::load_buffer` parses these
at runtime.

REQUIRES: Python 3.8+, `jsonschema` (`pip install -r tools/requirements.txt`).

```
python tools/compile_scenario.py \
    --modules-dir modules \
    --output-dir data/scenarios
```

Compiled `.modr` files end up у `data/scenarios/` і ship як part of
the LittleFS data partition. The engine loads them via either path
(`Engine::load_path`) або pre-loaded buffer (`Engine::load_buffer`).

## What it reads

- Every `modules/<name>/manifest.json` із `module_type: "recipe"` AND
  а `scenario:` key.
- `tools/scenario_schema.json` — JSON schema applied to each manifest.
- `tools/known_actions.json` — catalog of valid action і condition
  names, із their parameter signatures. Recipe manifests can only
  reference actions у this catalog.

## What it writes

Per recipe, one binary file:

```
data/scenarios/<recipe_name>.modr
```

The binary format is documented у `modesp_scenario/include/modesp/scenario/modr_format.h`.
Highlights:

- Magic `'MODR'` (0x52444F4D), version 1.
- Header (56 bytes) із counts і offsets to each section.
- Tracks array, phases array, transitions array, actions array, params array.
- String pool (deduplicated UTF-8 strings, length-prefixed).
- CRC32 footer for integrity check at load time.

Max size: 16 KB per recipe (`MODR_MAX_SIZE`). Most recipes stay у
~2-4 KB.

## Action і condition names → 16-bit hashes

Actions і conditions are hashed via **djb2_hash16** so the runtime
can dispatch у constant time without string comparison. The compiler
hashes names, the registries register handlers by hash. Collisions
(rare у the small known_actions namespace) cause а compile error
з а pointer to both colliding names — you must rename one.

```python
def djb2_hash16(s: str) -> int:
    h = 5381
    for ch in s.encode("utf-8"):
        h = ((h << 5) + h + ch) & 0xFFFFFFFF
    return h & 0xFFFF
```

The C++ side has an identical implementation у `modr_format.h`.

## Error categories

The tool emits `<file>:<line>:<col>: error[<CODE>]: <msg>` із numeric
codes grouped by phase:

| Code prefix | Phase | Example |
|---|---|---|
| E01xx | Schema validation | E0101: `tracks` is not an array |
| E02xx | Semantic (refs, types, hash collisions) | E0204: action `unknown_action` not у known_actions.json |
| E03xx | Binary emission | E0301: phase string pool overflow |
| E04xx | Cross-validation із manifest.state | E0401: scenario refs `equipment.foo` not declared у state |

Non-zero exit code on any error. Build CMake fails accordingly.

## CLI usage

**Build all recipes:**

```
python tools/compile_scenario.py --modules-dir modules --output-dir data/scenarios
```

**Build single recipe:**

```
python tools/compile_scenario.py --recipe modules/abs_test/manifest.json --output abs_test.modr
```

**Validate without writing:**

```
python tools/compile_scenario.py --modules-dir modules --dry-run
```

## Integration з CMake

Hooked into the build the same way as `generate_ui.py` — а custom
command depending on manifest glob, output written to the data
partition image:

```cmake
add_custom_command(
    OUTPUT ${MODR_FILES}
    COMMAND python ${CMAKE_SOURCE_DIR}/tools/compile_scenario.py ...
    DEPENDS ${RECIPE_MANIFESTS}
)
```

The `.modr` files end up у the LittleFS image flashed alongside firmware.

## Verifying the output

Use `dump_modr.py` to inspect а compiled `.modr`:

```
python tools/dump_modr.py data/scenarios/abs_test.modr
```

Prints header, tracks, phases, transitions, і string pool. Use --hex
for а raw byte dump alongside the structured view.

## Common pitfalls

**`jsonschema` not installed:** the tool exits із code 2 і а pointer
to `tools/requirements.txt`. Install і retry.

**`known_actions.json` out of date:** when you add an action у С++,
also register it у `known_actions.json` із param signature. Otherwise
all recipes referencing it fail compilation.

**Recipe name too long:** mirror keys are `<recipe>.<key>` із а 32-char
budget. Recipe name budget is **12 chars** so common mirror keys
fit. The tool warns at 13+; errors at 16+.

**Hash collision:** if а new action name collides із an existing one,
you get а compile-time error із the two names. Rename one to break
the collision. djb2 has ~1 у 60k collision rate — rare у practice.

## Next steps

- **[02-module-author-guide/recipe-authoring.md](../02-module-author-guide/recipe-authoring.md)** —
  recipe grammar reference.
- **[dump_modr.md](dump_modr.md)** — inspect compiled binaries.
- **[03-framework-reference/components/modesp_scenario.md](../03-framework-reference/components/modesp_scenario.md)** —
  engine that consumes `.modr`.
- **[03-framework-reference/modules/abs_test.md](../03-framework-reference/modules/abs_test.md)** —
  reference recipe.

## Source

- [`tools/compile_scenario.py`](../../../tools/compile_scenario.py)
- [`tools/scenario_schema.json`](../../../tools/scenario_schema.json)
- [`tools/known_actions.json`](../../../tools/known_actions.json)
- [`components/modesp_scenario/include/modesp/scenario/modr_format.h`](../../../components/modesp_scenario/include/modesp/scenario/modr_format.h) —
  binary format.
