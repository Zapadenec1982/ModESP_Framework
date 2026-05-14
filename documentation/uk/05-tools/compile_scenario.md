# `compile_scenario.py` — recipe manifest → `.modr` binary

> 📖 **In English:** [documentation/en/05-tools/compile_scenario.md](../../en/05-tools/compile_scenario.md)

Build-time tool що compiles `scenario:` blocks з recipe manifests
у compact binary format (`.modr`) consumed by scenario engine.

Parallel до `generate_ui.py` — same role на build side, different
output: instead of generating C++ headers І `ui.json`, це emits one
`.modr` file per recipe. Engine `Engine::load_buffer` parses їх
at runtime.

REQUIRES: Python 3.8+, `jsonschema` (`pip install -r tools/requirements.txt`).

```
python tools/compile_scenario.py \
    --modules-dir modules \
    --output-dir data/scenarios
```

Compiled `.modr` files end up у `data/scenarios/` і ship як частина
LittleFS data partition. Engine loads їх через path
(`Engine::load_path`) або pre-loaded buffer (`Engine::load_buffer`).

## Що читає

- Кожен `modules/<name>/manifest.json` з `module_type: "recipe"` І
  `scenario:` key.
- `tools/scenario_schema.json` — JSON schema applied до кожного manifest.
- `tools/known_actions.json` — catalog of valid action і condition
  names, з їх parameter signatures. Recipe manifests can only
  reference actions з цього catalog.

## Що пише

Per recipe, один binary file:

```
data/scenarios/<recipe_name>.modr
```

Binary format documented у `modesp_scenario/include/modesp/scenario/modr_format.h`.
Highlights:

- Magic `'MODR'` (0x52444F4D), version 1.
- Header (56 bytes) з counts і offsets до кожної section.
- Tracks array, phases array, transitions array, actions array, params array.
- String pool (deduplicated UTF-8 strings, length-prefixed).
- CRC32 footer для integrity check at load time.

Max size: 16 KB per recipe (`MODR_MAX_SIZE`). Most recipes stay у
~2-4 KB.

## Action і condition names → 16-bit hashes

Actions і conditions hashed через **djb2_hash16** так щоб runtime
could dispatch у constant time без string comparison. Compiler
hashes names, registries register handlers by hash. Collisions
(rare у small known_actions namespace) cause compile error
з pointer до both colliding names — потрібно rename one.

```python
def djb2_hash16(s: str) -> int:
    h = 5381
    for ch in s.encode("utf-8"):
        h = ((h << 5) + h + ch) & 0xFFFFFFFF
    return h & 0xFFFF
```

C++ side has identical implementation у `modr_format.h`.

## Error categories

Tool emits `<file>:<line>:<col>: error[<CODE>]: <msg>` з numeric
codes grouped by phase:

| Code prefix | Phase | Example |
|---|---|---|
| E01xx | Schema validation | E0101: `tracks` is not an array |
| E02xx | Semantic (refs, types, hash collisions) | E0204: action `unknown_action` not у known_actions.json |
| E03xx | Binary emission | E0301: phase string pool overflow |
| E04xx | Cross-validation з manifest.state | E0401: scenario refs `equipment.foo` not declared у state |

Non-zero exit code на будь-яку error. Build CMake fails accordingly.

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

Hooked у build тим самим способом як `generate_ui.py` — custom
command depending на manifest glob, output written до data
partition image:

```cmake
add_custom_command(
    OUTPUT ${MODR_FILES}
    COMMAND python ${CMAKE_SOURCE_DIR}/tools/compile_scenario.py ...
    DEPENDS ${RECIPE_MANIFESTS}
)
```

`.modr` files end up у LittleFS image flashed alongside firmware.

## Verifying output

Use `dump_modr.py` щоб inspect compiled `.modr`:

```
python tools/dump_modr.py data/scenarios/abs_test.modr
```

Prints header, tracks, phases, transitions, і string pool. Use --hex
для raw byte dump alongside structured view.

## Common pitfalls

**`jsonschema` не installed:** tool exits з code 2 і pointer
до `tools/requirements.txt`. Install і retry.

**`known_actions.json` out of date:** коли add action у С++,
також register її у `known_actions.json` з param signature. Otherwise
всі recipes referencing її fail compilation.

**Recipe name too long:** mirror keys are `<recipe>.<key>` з 32-char
budget. Recipe name budget — **12 chars** so common mirror keys
fit. Tool warns at 13+; errors at 16+.

**Hash collision:** якщо new action name collides з existing one,
ви отримаєте compile-time error з two names. Rename one щоб break
collision. djb2 has ~1 у 60k collision rate — rare у practice.

## Що далі

- **[02-module-author-guide/recipe-authoring.md](../02-module-author-guide/recipe-authoring.md)** —
  recipe grammar reference.
- **[dump_modr.md](dump_modr.md)** — inspect compiled binaries.
- **[03-framework-reference/components/modesp_scenario.md](../03-framework-reference/components/modesp_scenario.md)** —
  engine що consumes `.modr`.
- **[03-framework-reference/modules/abs_test.md](../03-framework-reference/modules/abs_test.md)** —
  reference recipe.

## Source

- [`tools/compile_scenario.py`](../../../tools/compile_scenario.py)
- [`tools/scenario_schema.json`](../../../tools/scenario_schema.json)
- [`tools/known_actions.json`](../../../tools/known_actions.json)
- [`components/modesp_scenario/include/modesp/scenario/modr_format.h`](../../../components/modesp_scenario/include/modesp/scenario/modr_format.h) —
  binary format.
