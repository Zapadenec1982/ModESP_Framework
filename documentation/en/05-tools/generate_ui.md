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
- **Bindings ↔ board ↔ driver** (the active board's `bindings.json`): every
  `hardware` exists in `board.json`; every `driver` has a manifest; the driver's
  `hardware_type` matches the board section of its hardware; `requires_address`
  honored; hardware reuse only with `multiple_per_bus` + distinct addresses;
  unique role per module. A mis-wired binding fails the build.

On error, the generator prints `<file>:<line>:<col>: error[<code>]: <msg>`
(or `[context] message` for cross-checks) і exits з code 1. Build fails.

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

Hierarchical on-device menu tree merged from every module's `display:`
section. Consumed by the `display` module
([modules/display.md](../03-framework-reference/modules/display.md)).

```cpp
namespace modesp::gen {
    enum class DisplayItemType : uint8_t {
        SUBMENU, VALUE, EDIT_FLOAT, EDIT_INT, EDIT_BOOL, EDIT_ENUM };

    struct DisplayMenuNode {
        const char* label; const char* key;
        const char* format; const char* unit;
        DisplayItemType type;
        float min, max, step;                  // edit bounds from state
        const DisplayEnumOption* options;      // for enum/bool
        uint8_t option_count;
        uint8_t first_child, child_count;      // for SUBMENU
    };

    static constexpr DisplayMenuNode MENU_NODES[] = { /* ... */ };
    static constexpr uint8_t MENU_ROOT_COUNT = 1;       // module submenus
    static constexpr DisplayMainValue MAIN_VALUES[] = { /* idle screen */ };
}
```

`MENU_NODES` layout: the first `MENU_ROOT_COUNT` nodes are module
submenus (root children); items follow, contiguous per submenu
(`first_child`/`child_count`). Item type is derived from `state`:
`readwrite` float/int → `EDIT_FLOAT`/`EDIT_INT` with `min`/`max`/`step`;
`options` → `EDIT_ENUM` with an option table; bool → `EDIT_BOOL` with
`on_label`/`off_label`; `read` → `VALUE`. The tree is capped at 255
nodes (`uint8_t` indices) — the generator fails the build beyond that.

## Output 5: module registration, driver glue, and the rest

Compact outputs that wire the build together (all `DO NOT EDIT`):

| File | From | Purpose |
|---|---|---|
| `generated/module_includes.h` / `module_instances.h` / `module_register.h` | `project.json` | `#include`, static instances, and `register_module()` calls used by `main.cpp` |
| `generated/modules.cmake` | `project.json` | module component list for `main/CMakeLists.txt` REQUIRES |
| `generated/mqtt_topics.h` | `mqtt` sections | per-key topic constants for `MqttService` |
| `generated/datalogger_channels.h` / `datalogger_events.h` | `loggable` sections | channel/event id tables for DataLogger |
| `components/modesp_hal/Kconfig` | `drivers/*/manifest.json` | one `CONFIG_MODESP_DRIVER_<NAME>` toggle per driver (menu "ModESP Drivers") |
| `generated/drivers.cmake` | drivers | `MODESP_ALL_DRIVERS` list for `modesp_hal` REQUIRES |
| `generated/driver_register_all.h` | drivers | guarded `modesp_register_all_drivers()` (skips disabled drivers) |
| `generated/required_drivers.cmake` | active `bindings.json` | `MODESP_BOUND_DRIVERS` — checked by `modesp_hal/CMakeLists.txt` to FATAL if a bound driver is disabled |
| `data/www/i18n/*.json` | per-module `i18n/` | merged translation packs |

The driver glue makes each driver optional and self-registering; see
[drivers_sync.md](drivers_sync.md) and
[writing-a-driver.md](../02-module-author-guide/writing-a-driver.md).

## CLI options

```
python tools/generate_ui.py --help
```

| Flag | Purpose |
|---|---|
| `--project FILE` | Path to project.json (default: `./project.json`). |
| `--modules-dir DIR` | Module manifest root (default: `./modules`). |
| `--drivers-dir DIR` | Driver manifest root (default: `./drivers`). |
| `--output-data DIR` | Where to write `ui.json` (default: `./data`). |
| `--output-gen DIR` | Where to write `state_meta.h` etc. (default: `./generated`). |
| `--minify` | Minified `ui.json` (no indentation). |

## Integration з CMake

The root `CMakeLists.txt` runs the generator via `execute_process()` on
every configure (before `project()`, so generated Kconfig/cmake files exist
before confgen). All inputs — `project.json`, `tools/generate_ui.py`,
`tools/compile_scenario.py`, `modules/*/manifest.json`,
`drivers/*/manifest.json`, `boards/*/board.json` — are registered in
`CMAKE_CONFIGURE_DEPENDS` (globs use `file(GLOB ... CONFIGURE_DEPENDS)` to
also catch added/removed files).

Editing a manifest → `idf.py build` re-runs configure by itself → generator
→ regenerated headers → affected components rebuild. No manual
`idf.py reconfigure` needed.

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
