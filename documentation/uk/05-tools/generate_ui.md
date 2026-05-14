# `generate_ui.py` — manifest validator І code generator

> 📖 **In English:** [documentation/en/05-tools/generate_ui.md](../../en/05-tools/generate_ui.md)

`tools/generate_ui.py` — build-time entry point що turns
**module manifests** на:

1. **`data/ui.json`** — merged UI schema served at runtime через `GET /api/ui`.
2. **`generated/state_meta.h`** — `constexpr` metadata used by API
   handlers І SharedState для validation.
3. **`generated/mqtt_topics.h`** — pre-computed MQTT topic strings.
4. **`generated/display_screens.h`** — display/LCD menu data.

Run з project root:

```
python tools/generate_ui.py
```

Output deterministic: re-running на unchanged inputs produces
byte-identical files. ESP-IDF's build system invokes це через
CMake custom command — generator wired у build
graph, тому editing manifest triggers regen.

WebUI сам (`data/www/*.html|js|css`) — **static**. Loads
`ui.json` at runtime — це і є те що makes framework manifest-driven.

REQUIRES: Python 3.8+. No external packages (stdlib only).

## Що читає

| Input | Purpose |
|---|---|
| `project.json` | Module list і build-time options. |
| `modules/<name>/manifest.json` | Per-module state/ui/mqtt/loggable. |
| `drivers/<name>/manifest.json` | Per-driver state/settings/ui. |

Як module так і driver manifests loaded і cross-validated.

## Що validates

`ManifestValidator` runs over кожний manifest перед generation:

- `manifest_version` present і matches supported (1).
- Required top-level fields (`module`, `state`).
- State key naming convention (`<module>.<key>`, ≤32 chars).
- Widget-to-state-type compatibility (наприклад `slider` needs `float`/`int`).
- Cross-module references: visible_when conditions point до real keys.
- Recipe-specific: track names, phase references, action/condition names
  у `tools/known_actions.json`.

При error generator prints `<file>:<line>:<col>: error[<code>]: <msg>`
і exits з code 1. Build fails.

## Output 1: `data/ui.json` (runtime schema)

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

WebUI fetches це once on load і re-renders якщо hash differs від
previous load. Localised strings з кожного manifest's `i18n` blocks
get merged у top-level dictionaries.

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

Used by HTTP handlers щоб validate POST'ed keys, by MQTT for publish
routing, і by SharedState для type-aware coercion.

## Output 3: `generated/mqtt_topics.h`

```cpp
namespace modesp::generated::mqtt {
    constexpr const char* TOPIC_THERMO_TEMPERATURE = "modesp/+/thermostat/temperature";
    // ...
}
```

Pre-computed once, avoids `sprintf` at runtime. Used by `modesp_mqtt`.

## Output 4: `generated/display_screens.h`

Generates menu trees для LCD displays. Optional — only emitted якщо any
module manifest contains `display:` block.

## CLI options

```
python tools/generate_ui.py --help
```

| Flag | Purpose |
|---|---|
| `--project FILE` | Path до project.json (default: `./project.json`). |
| `--modules-dir DIR` | Module manifest root (default: `./modules`). |
| `--output-data DIR` | Куди write `ui.json` (default: `./data`). |
| `--output-gen DIR` | Куди write `state_meta.h` etc. (default: `./generated`). |
| `--strict` | Treat warnings як errors. |

## Integration з CMake

`CMakeLists.txt` declares custom command depending на manifest
glob:

```cmake
add_custom_command(
    OUTPUT ${GEN_HEADERS}
    COMMAND python ${CMAKE_SOURCE_DIR}/tools/generate_ui.py ...
    DEPENDS ${MANIFEST_FILES}
)
```

Editing manifest → CMake re-runs generator → regenerated headers
→ affected components rebuild.

## Common pitfalls

**Encoding errors on Windows:** скрипт auto-reconfigures stdout до
UTF-8. Якщо redirect до file і see `?` characters, use PowerShell з
UTF-8 default або pipe through `chcp 65001`.

**Forgotten manifest registration:** modules not listed у `project.json`
silently skipped. Якщо ваш new module's UI не appear, перевір
`project.json` first.

**Widget type mismatch:** `slider` на `bool` key gives validator
error. Або pick right widget (`toggle` для booleans), або fix
state type.

**Schema diff at rebuild:** якщо `ui.json` changes byte-for-byte без
manifest changes — це bug — generation should be deterministic. File
issue.

## Що далі

- **[compile_scenario.md](compile_scenario.md)** — analogous tool для
  recipe manifests producing `.modr` binaries.
- **[02-module-author-guide/manifest.md](../02-module-author-guide/manifest.md)** —
  manifest schema reference.
- **[02-module-author-guide/ui-widgets.md](../02-module-author-guide/ui-widgets.md)** —
  які UI widgets generator emits.

## Source

- [`tools/generate_ui.py`](../../../tools/generate_ui.py)
- [`tools/known_actions.json`](../../../tools/known_actions.json) — action/condition catalog.
