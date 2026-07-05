# Manifest reference

> 📖 **Українською:** [documentation/uk/02-module-author-guide/manifest.md](../../uk/02-module-author-guide/manifest.md)

The manifest is your module's contract із the framework. It's а JSON file
that declares everything the build system needs to know: what state keys
you expose, what UI widgets render, what MQTT topics publish, what config
persists, і (for recipes) what phase machine drives execution.

This page reference-documents every section, із real examples з the modules
shipped у the framework. After reading this you'll be able to write а
complete manifest for а new module, recipe, or driver without consulting
existing code.

## Three flavors of manifests

ModESP recognises three types based on top-level fields. The build pipeline
picks the right code generator for each.

| Flavor | Folder | Distinguishing field | C++ code? |
|---|---|---|---|
| **Service module** | `modules/<name>/` | `"module": "..."` | Yes (BaseModule subclass) |
| **Recipe module** | `modules/<name>/` | `"module_type": "recipe"` + `"scenario"` section | No |
| **Driver** | `drivers/<name>/` | `"driver": "..."` | Yes (IDriver subclass) |

Sections covered here apply to one or more flavors. Headers mark which.

## Top-level fields

These appear at the JSON root for every manifest:

| Field | Type | Required | Notes |
|---|---|---|---|
| `manifest_version` | int | yes | Currently `1`. Bumped when schema changes incompatibly. |
| `module` | string | one of these two | Module name (matches folder under `modules/`). Snake_case, ≤ 16 chars. |
| `driver` | string | one of these two | Driver name (matches folder under `drivers/`). Snake_case. |
| `module_type` | string | recipes only | Set to `"recipe"` для pure manifest modules without C++. |
| `version` | string | recommended | Semver — for your own tracking. |
| `description` | string | recommended | One-line human-readable summary. |
| `priority` | int | service modules | Init phase: `0`=CRITICAL, `1`=HIGH, `2`=NORMAL, `3`=LOW. See [overview.md](overview.md#3--three-phase-init-lifecycle). |
| `category` | string | drivers only | `"sensor"` or `"actuator"`. |
| `hardware_type` | string | drivers only | `"gpio"`, `"onewire_bus"`, `"adc"`, `"i2c"`, `"rs485"`, etc. |

Example top of `modules/simple_thermo/manifest.json`:

```json
{
  "manifest_version": 1,
  "module": "simple_thermo",
  "version": "1.0.0",
  "description": "Simple ON/OFF thermostat — demo module",
  "priority": 2
}
```

Example top of `drivers/ds18b20/manifest.json`:

```json
{
  "manifest_version": 1,
  "driver": "ds18b20",
  "description": "Dallas DS18B20 цифровий датчик температури",
  "category": "sensor",
  "hardware_type": "onewire_bus",
  "requires_address": true,
  "multiple_per_bus": true,
  "provides": {"type": "float", "unit": "°C", "range": [-55, 125]}
}
```

## Section: `requires` (service modules)

Declares the peripheral **roles** the module needs. A role is a
*capability*, not a driver: a thermostat needs "temperature" and does not
know who supplies it (ds18b20 / NTC / a BLE channel / a future LoRa link).
The generator and runtime bind any source of the same capability to the
role. This is a founding principle of the framework — see
[R0.1](../03-framework-reference/rules.md#r01--роль--здатність-capability-ніколи-не-драйвер)
and [R3.1](../03-framework-reference/rules.md#r31--матч-ролі-й-каналу--лише-за-capability).

### Per-role fields

| Field | Type | Required | Notes |
|---|---|---|---|
| `role` | string | yes | Role name the code resolves a source by (`find_sensor("air_temp")`). Unique within the module. |
| `type` | string | yes | Coarse category: `"sensor"` / `"actuator"`. |
| `capability` | string | recommended | **The matcher.** The capability the role needs — must exist in [`tools/capabilities.json`](../../../tools/capabilities.json). Any source of the same capability fills the role. This field, not `driver`, decides the match. |
| `kind` | `"sensor"` / `"actuator"` | optional | Coarse direction discriminator. When absent, the generator normalizes it from `type`. |
| `label` | string | optional | Human-readable role name in the bindings UI. **No transport** ("Room temperature", not "BLE room sensor") — [R1.3](../03-framework-reference/rules.md#r13--назви-ролейканалів--без-транспорту). |
| `optional` | bool | optional | `true` → the module starts even without a bound source. Default `false`. |

### Example

From `modules/equipment/manifest.json`:

```json
"requires": [
  {"role": "air_temp",    "type": "sensor",   "capability": "temperature", "label": "Air temperature"},
  {"role": "room_temp",   "type": "sensor",   "capability": "temperature", "label": "Room temperature", "optional": true},
  {"role": "orientation", "type": "sensor",   "capability": "angle",       "label": "Orientation",      "optional": true},
  {"role": "actuator_1",  "type": "actuator", "capability": "relay_out",   "label": "Actuator 1",       "optional": true}
]
```

> ⚠️ **Never hardcode a driver in a role.** The role declares a capability;
> the binding to concrete hardware lives in `bindings.json` (the `driver` +
> `device` fields), and a remote device's identity (MAC/topic) lives on the
> device row (board.json/devices.json), NEVER on the role
> ([R0.3](../03-framework-reference/rules.md#r03--ідентичність--на-пристрої-ніколи-на-ролі)).
> The generator cross-validates `capability` against the vocabulary — an
> unknown capability fails the build
> ([R8.3](../03-framework-reference/rules.md#r83--валідація-на-build-time)).

## Section: `state` (service modules)

Declares the SharedState keys the module owns. Each key gets typed metadata
that flows through to: the generated C++ state table, the WebUI auto-card,
MQTT topic strings, і datalogger channel discovery.

**Naming convention:** `<module>.<key_name>`, single dot, ≤ 32 chars total.

### Per-key fields

| Field | Type | Required | Notes |
|---|---|---|---|
| `type` | `"int"` / `"float"` / `"bool"` / `"string"` | yes | Maps to `modesp::StateValue` variant cases. |
| `access` | `"read"` / `"readwrite"` | yes | `"read"` = display-only у UI; `"readwrite"` = user can set via WebUI/MQTT. |
| `default` | typed | optional | Initial value if no persisted override. Typed must match `type`. |
| `min` / `max` / `step` | numeric | numeric types only | Bounds для UI widgets і validation. |
| `unit` | string | optional | `"°C"`, `"%"`, `"мс"` — shown next to value у UI. |
| `description` | string | optional | Tooltip у UI. |
| `persist` | bool | optional | `true` → PersistService saves/restores across reboot. Default `false`. |
| `mqtt_subscribe` | bool | optional | `true` → key writable via MQTT. Requires `access: "readwrite"`. |
| `on_label` / `off_label` | string | bool only | Labels for indicator widget (e.g., `"ON"`/`"OFF"`, `"Heating"`/`"Idle"`). |

### Example

```json
"state": {
  "simple_thermo.temperature": {
    "type": "float",
    "access": "read",
    "unit": "°C",
    "description": "Поточна температура"
  },
  "simple_thermo.setpoint": {
    "type": "float",
    "access": "readwrite",
    "default": 22.0,
    "min": 5,
    "max": 40,
    "step": 0.5,
    "unit": "°C",
    "persist": true,
    "mqtt_subscribe": true,
    "description": "Уставка температури"
  },
  "simple_thermo.output": {
    "type": "bool",
    "access": "read",
    "description": "Запит на реле",
    "on_label": "ON",
    "off_label": "OFF"
  }
}
```

> 💡 **Tip:** the generator emits `state_meta.h` із а constexpr table of all
> declared keys і their types. Your C++ code can reference keys через generated
> constants — typos are compile errors, not runtime "key not found".

## Section: `ui` (any module / driver)

Declarative WebUI. The generator merges all module/driver `ui` sections into
а single `data/ui.json` that the Svelte SPA loads. No Svelte code goes
through your hands; you write JSON.

### Top-level shape

```json
"ui": {
  "page": "Термостат",              // Page title shown у navigation
  "icon": "thermometer",            // Icon name (lucide / heroicons)
  "page_id": "thermostat",          // Optional URL slug. Default derived з page name.
  "access_level": "user",           // "user" / "service" / "admin" — gates visibility
  "cards": [...]                    // Array of card definitions
}
```

### Cards

А card is а grouped widget container that renders as one panel у the page.

```json
{
  "title": "Стан",                  // Card heading
  "subtitle": "Температура, режим", // Optional sub-heading
  "layout": "single",               // "single" / "grid" / "split"
  "visible_when": {                 // Optional — show only коли а state key matches
    "scenario.engine_active_count": {">": 0}
  },
  "widgets": [...]
}
```

### Widgets

The simplest widget shape:

```json
{"key": "simple_thermo.temperature", "widget": "value"}
```

Common widget types:

| `widget` value | Renders | Best for |
|---|---|---|
| `"value"` | Read-only number/string display із unit | Sensor readings, computed state. |
| `"indicator"` | LED-style on/off circle | Bool outputs, alarm state. |
| `"slider"` | Range slider із min/max/step | Setpoints, gain values. |
| `"number_input"` | Spin-box numeric input | Precise values, large ranges. |
| `"select"` | Dropdown із enum values | Mode pickers (HEATING/COOLING). |
| `"toggle"` | Bool switch | On/off configuration flags. |
| `"chart"` | Time-series chart pulling з datalogger | Temperature history, trend lines. |

Driver-specific shape (note `setting` instead of `key`):

```json
{
  "title": "DS18B20: {{hardware_id}}",
  "instance_per_binding": true,     // Render one card per bound instance
  "widgets": [
    {"setting": "read_interval_ms", "widget": "number_input"},
    {"setting": "offset",           "widget": "slider"},
    {"setting": "resolution",       "widget": "select"}
  ]
}
```

## Section: `mqtt` (service modules)

Lists which state keys publish to MQTT, і which accept MQTT writes. The
generator produces topic strings of the form `<base>/<module>/<key_name>`
where `<base>` is configured у `sdkconfig` (default `modesp/<device-id>`).

```json
"mqtt": {
  "publish": [
    "simple_thermo.temperature",
    "simple_thermo.state",
    "simple_thermo.output"
  ],
  "subscribe": [
    "simple_thermo.setpoint",
    "simple_thermo.differential"
  ]
}
```

**Rules:**
- Keys у `subscribe` must have `access: "readwrite"` and `mqtt_subscribe: true`
  у their `state` declaration. The generator validates це at build time.
- Published values get а throttled delta-publish (Stage 1.5 will document
  rates і LWT handling at [mqtt.md](mqtt.md) *(planned)*).

## Section: `display` (service modules, optional)

Describes what the module shows on the device's local display (OLED/LCD).
The generator merges every module's section into a single menu tree in
`generated/display_screens.h`; the `display` module renders it and
handles button navigation. See
[modules/display.md](../03-framework-reference/modules/display.md).

```json
"display": {
  "main_value": {"key": "simple_thermo.temperature", "format": "%.1f°C"},
  "menu_label": "Термостат",
  "menu_items": [
    {"label": "Уставка", "key": "simple_thermo.setpoint"},
    {"label": "Стан", "key": "simple_thermo.state"}
  ]
}
```

| Field | Description |
|---|---|
| `main_value` | Value for the idle screen: `key` + printf `format`. |
| `menu_label` | The module's submenu title. Falls back to `ui.page`, then the module name. |
| `menu_items[]` | Submenu entries: required `label` and `key`, optional `format` (printf). |

**Rules:**
- Every `key` must exist in this module's `state` section — validated
  at build time.
- Editability is derived from `state` automatically: `access: "readwrite"`
  makes the item editable with step/bounds from `min`/`max`/`step`;
  `options` gives a pick-list; `bool` becomes a toggle using
  `on_label`/`off_label`. Keys with `access: "read"` render as read-only
  values.
- A `readwrite` string without `options` renders read-only on the
  display (text cannot be edited with buttons) — the generator warns.

## Section: `loggable` (service modules)

Wires state keys to the DataLogger module — channels for time-series, events
for edge logging.

```json
"loggable": {
  "channels": {
    "simple_thermo.temperature": {
      "type": "temperature",
      "label": "Температура",
      "default": true              // Enabled out of the box
    }
  },
  "events": {
    "simple_thermo.output": {
      "id": 30,                    // Stable event ID — assign once, never change
      "edge": "both",              // "rising" / "falling" / "both"
      "label_on": "Нагрів ON",
      "label_off": "Нагрів OFF"
    }
  }
}
```

> ⚠️ **Warning:** event `id` is а stable byte-level identifier stored у the
> datalogger flash files. **Never reuse or renumber** existing IDs — historical
> data interpretation breaks. Pick unused IDs by scanning all modules'
> manifests for current values.

## Section: `features` (service modules, optional)

Declares compile-time feature flags. The generator emits constants до
`features_config.h` which your C++ і other generators read at compile time.

```json
"features": {
  "thermo_alarm": {
    "type": "bool",
    "default": false,
    "description": "Enable thermostat over-temperature alarm"
  },
  "thermo_max_setpoint": {
    "type": "int",
    "default": 40,
    "description": "Hard cap on setpoint (°C)"
  }
}
```

Use sparingly. Most "configuration" should live у `state` із `persist: true`
(runtime-changeable, no rebuild needed). Features are для compile-time-only
choices: flash size optimisations, mutually exclusive code paths, debug
gating.

## Section: `scenario` (recipe modules only)

This is the recipe DSL — phase/transition graph compiled to binary `.modr`
by `tools/compile_scenario.py`. Engine executes the resulting blob at
runtime.

```json
"scenario": {
  "default_phase_timeout_ms": 30000,
  "scenario_timeout_max_ms": 120000,
  "completion_rule": "all_tracks_complete",
  "tracks": [
    {
      "name": "main",
      "flags": ["main_track"],
      "phases": [
        {
          "name": "phase_a",
          "entry": [
            {"action": "log", "params": {"msg": "main: phase_a started"}}
          ],
          "transitions": [
            {"to": "phase_b", "when": {"time_elapsed_ms": 1000}}
          ]
        },
        // ... more phases
      ]
    }
  ]
}
```

Full reference: [recipe-authoring.md](recipe-authoring.md) *(planned)* і the
existing
[scenario-engine docs](../03-framework-reference/scenario-engine/).

## Driver-only sections

### `settings`

Persisted config schema for а driver instance (one driver definition може
have many bound instances — different sensors of the same type).

```json
"settings": [
  {
    "key": "read_interval_ms",
    "type": "int",
    "default": 1000,
    "min": 500, "max": 60000, "step": 100,
    "unit": "мс",
    "description": "Інтервал опитування",
    "persist": true
  }
]
```

Fields mirror `state` per-key fields. Settings stored у NVS under
`drv.<driver>.<instance>.<key>`.

### `provides`

What the driver produces (sensors) or accepts (actuators).

```json
// Sensor driver:
"provides": {"type": "float", "unit": "°C", "range": [-55, 125]}

// Actuator driver:
"provides": {"type": "bool", "description": "Relay state"}
```

### `requires_address`

Driver instance binding needs hardware addressing (OneWire ROM, I2C address,
etc.). Когда `true`, `bindings.json` must supply an `address` field per
instance.

```json
"requires_address": true,
"multiple_per_bus": true            // > 1 instance can share а bus
```

### `discovery`

Optional scanner endpoint — UI button що probes the hardware bus і returns
discovered devices.

```json
"discovery": {
  "supported": true,
  "scan_endpoint": "/api/drivers/ds18b20/scan",
  "returns": [
    {"field": "address",     "type": "string", "description": "ROM адреса (64-bit)"},
    {"field": "temperature", "type": "float",  "description": "Поточне показання"}
  ],
  "ui": {
    "title": "Сканер OneWire шини",
    "scan_button": "Сканувати",
    "result_table": true,
    "assign_action": true
  }
}
```

## Validation і build integration

`tools/generate_ui.py` runs as а pre-build CMake hook. It:

1. Discovers every `manifest.json` under `modules/` і `drivers/`.
2. Validates each against the **JSON Schema** in `tools/schemas/`
   (`module`, `driver`, `board`, `bindings`, `project` — draft-07,
   `additionalProperties:false`). A typo in a field name (`persits`), a
   wrong type (`"priority": "high"`), or an unknown widget fails the build
   with a clear `schema:` message instead of silently ignoring the field.
   Underscore-prefixed keys (`_note`, `_config_note`) are always allowed as
   free-form annotations. This document is the human-readable form of the
   schema.
3. Cross-validates references: every key у `mqtt.subscribe` must exist у
   `state`; every `setting.key` must be unique within а driver; every recipe
   `scenario` mirror key must be pre-declared у the recipe's `state` section.
4. Emits artifacts to `generated/` і `data/` (see
   [tools/generate_ui.md](../05-tools/generate_ui.md) *(planned)*).

A failure у any step aborts the build із а specific error message — invalid
manifests never reach runtime.

## Common mistakes

**Forgetting `priority`:** service module без `priority` lands у NORMAL phase
(priority `2`) by default — usually fine. Critical modules (watchdog, error
service) need `priority: 0`; HAL і drivers need `priority: 1`. Late init
phases (HTTP, WS) get `priority: 3`.

**State key length > 32 chars:** SharedState enforces 32-char key limit.
`<module>.<key>` budget: module name ≤ 12 chars + dot + key ≤ 19 chars.
Recipe authors hit це з long phase names — recipe name budget shrinks to ≤ 8.

**Forgetting `mqtt_subscribe: true` для writable keys:** key із
`access: "readwrite"` listed у `mqtt.subscribe` але missing the flag —
generator rejects з а clear message.

**`features` confused із `state`:** features are compile-time, state is
runtime. If users will adjust the value through the WebUI, use `state`.
If choice gates entire code blocks, use `features`.

**Recipe із undeclared mirror keys:** scenarios write mirror keys
(`<recipe>.scenario_state`, etc.) — these MUST be pre-declared у the recipe
manifest's `state` section, even though they're written by the engine, not
the recipe. The compiler cross-validates і fails the build otherwise.

## Next steps

- **[writing-a-module.md](writing-a-module.md)** *(planned)* — turn the
  manifest into а working C++ module.
- **[writing-a-driver.md](writing-a-driver.md)** *(planned)* — driver
  authoring із the IDriver interface.
- **[recipe-authoring.md](recipe-authoring.md)** *(planned)* — the `scenario`
  section deep dive.
- **[ui-widgets.md](ui-widgets.md)** *(planned)* — all widget types з
  rendered examples.

## Examples to study

Existing manifests worth reading source-first:

- [`modules/simple_thermo/manifest.json`](../../../modules/simple_thermo/manifest.json) —
  minimal service module із state, mqtt, loggable, ui. ~100 lines.
- [`modules/abs_test/manifest.json`](../../../modules/abs_test/manifest.json) —
  recipe module із а 2-track `scenario` section.
- [`drivers/ds18b20/manifest.json`](../../../drivers/ds18b20/manifest.json) —
  driver із settings, provides, discovery.
- [`modules/datalogger/manifest.json`](../../../modules/datalogger/manifest.json) —
  larger service module із channels, features.
- [`modules/equipment/manifest.json`](../../../modules/equipment/manifest.json) —
  `requires` section with roles as capabilities (temperature, angle, relay_out).
