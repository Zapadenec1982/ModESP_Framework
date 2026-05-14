# `generate_ui.py` — manifest validator AND code generator

> 📖 **Українською:** [documentation/uk/05-tools/generate_ui.md](../../uk/05-tools/generate_ui.md)

`tools/generate_ui.py` is the build-time entry point that turns
**module manifests** into:

1. **`data/ui.json`** — merged UI schema served at runtime via `GET /api/ui`.
2. **`generated/state_meta.h`** — `constexpr` metadata used by API
   handlers AND the SharedState for validation.
3. **`generated/mqtt_topics.h`** — pre-computed MQTT topic strings.
4. **`generated/display_screens.h`** — display/LCD menu data.

Run it from the project root:

```
python tools/generate_ui.py
```

Output is deterministic: re-running on unchanged inputs produces
byte-identical files. ESP-IDF's build system invokes це through
а CMake custom command — the generator is wired into the build
graph, so editing а manifest triggers regen.

The WebUI itself (`data/www/*.html|js|css`) is **static**. It loads
`ui.json` at runtime — це is what makes the framework manifest-driven.

REQUIRES: Python 3.8+. No external packages (stdlib only).

## What it reads

| Input | Purpose |
|---|---|
| `project.json` | Module list AND build-time options. |
| `modules/<name>/manifest.json` | Per-module state/ui/mqtt/loggable. |
| `drivers/<name>/manifest.json` | Per-driver state/settings/ui. |

Both module AND driver manifests are loaded і cross-validated.

## What it validates

The `ManifestValidator` runs over each manifest before generation:

- `manifest_version` present і matches supported (1).
- Required top-level fields (`module`, `state`).
- State key naming convention (`<module>.<key>`, ≤32 chars).
- Widget-to-state-type compatibility (e.g. `slider` needs `float`/`int`).
- Cross-module references: visible_when conditions point to real keys.
- Recipe-specific: track names, phase references, action/condition names
  у `tools/known_actions.json`.

On error, the generator prints `<file>:<line>:<col>: error[<code>]: <msg>`
і exits з code 1. Build fails.

## Output 1: `data/ui.json` (the runtime schema)

Schema structure:

```json
{
  "pages": [
    {
      "id": "thermostat",
      "title": "Thermostat",
      "icon": "home",
      "cards": [...]
    }
  ],
  "i18n": {"uk": {...}, "en": {...}}
}
```

WebUI fetches це once on load і re-renders if hash differs from
previous load. Localised strings from each manifest's `i18n` blocks
get merged into top-level dictionaries.

## Output 2: `generated/state_meta.h`

```cpp
namespace modesp::generated {
    struct StateMeta {
        const char* key;
        StateValueType type;
        bool mqtt_publish;
        bool mqtt_subscribe;
        // ...
    };
    constexpr StateMeta STATE_KEYS[] = { ... };
    constexpr size_t STATE_KEY_COUNT = ...;
}
```

Used by HTTP handlers to validate POST'ed keys, by MQTT for publish
routing, і by SharedState for type-aware coercion.

## Output 3: `generated/mqtt_topics.h`

```cpp
namespace modesp::generated::mqtt {
    constexpr const char* TOPIC_THERMO_TEMPERATURE = "modesp/+/thermostat/temperature";
    // ...
}
```

Pre-computed once, avoids `sprintf` at runtime. Used by `modesp_mqtt`.

## Output 4: `generated/display_screens.h`

Generates menu trees for LCD displays. Optional — only emitted if any
module manifest contains а `display:` block.

## CLI options

```
python tools/generate_ui.py --help
```

| Flag | Purpose |
|---|---|
| `--project FILE` | Path to project.json (default: `./project.json`). |
| `--modules-dir DIR` | Module manifest root (default: `./modules`). |
| `--output-data DIR` | Where to write `ui.json` (default: `./data`). |
| `--output-gen DIR` | Where to write `state_meta.h` etc. (default: `./generated`). |
| `--strict` | Treat warnings as errors. |

## Integration з CMake

`CMakeLists.txt` declares а custom command depending on the manifest
glob:

```cmake
add_custom_command(
    OUTPUT ${GEN_HEADERS}
    COMMAND python ${CMAKE_SOURCE_DIR}/tools/generate_ui.py ...
    DEPENDS ${MANIFEST_FILES}
)
```

Editing а manifest → CMake re-runs the generator → regenerated headers
→ affected components rebuild.

## Common pitfalls

**Encoding errors on Windows:** the script auto-reconfigures stdout to
UTF-8. If you redirect to file и see `?` characters, use PowerShell із
UTF-8 default або pipe through `chcp 65001`.

**Forgotten manifest registration:** modules not listed у `project.json`
are silently skipped. If your new module's UI doesn't appear, check
`project.json` first.

**Widget type mismatch:** `slider` on а `bool` key gives а validator
error. Either pick the right widget (`toggle` for booleans) or fix
the state type.

**Schema diff на rebuild:** if `ui.json` changes byte-for-byte without
manifest changes, та's а bug — generation should be deterministic. File
an issue.

## Next steps

- **[compile_scenario.md](compile_scenario.md)** — analogous tool для
  recipe manifests producing `.modr` binaries.
- **[02-module-author-guide/manifest.md](../02-module-author-guide/manifest.md)** —
  manifest schema reference.
- **[02-module-author-guide/ui-widgets.md](../02-module-author-guide/ui-widgets.md)** —
  what UI widgets the generator emits.

## Source

- [`tools/generate_ui.py`](../../../tools/generate_ui.py)
- [`tools/known_actions.json`](../../../tools/known_actions.json) — action/condition catalog.
