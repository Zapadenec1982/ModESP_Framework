# UI widgets

> 📖 **Українською:** [documentation/uk/02-module-author-guide/ui-widgets.md](../../uk/02-module-author-guide/ui-widgets.md)

The framework's WebUI is а Svelte SPA що renders entirely from а JSON
schema (`data/ui.json`) generated з your module manifests at build time.
You don't write Svelte code, you don't touch CSS — you declare widgets,
cards, і pages у JSON, і они auto-render на the device's WebUI.

This page is the widget catalog: every widget type, its config fields,
what it renders, when to use it. After reading you'll know which widget
to pick для each state key і how to compose them у cards.

## Mental model

UI schema flows:

```
   modules/*/manifest.json::ui  +  drivers/*/manifest.json::ui
                  │
                  ▼ (generate_ui.py merges + validates)
                  │
   data/ui.json  ──→  served at GET /api/ui
                  │
                  ▼ (Svelte SPA loads at boot)
                  │
   Pages, cards, widgets render
```

A widget is one of ~25 Svelte components registered у `WidgetRenderer.svelte`.
You reference it through the `widget` field у your manifest's UI section.
Widgets read/write SharedState keys через а common WebSocket pipeline; you
don't manage that wiring manually.

## Page і card hierarchy

```
Page (top-level navigation entry)
└── Card (grouped panel)
    └── Widget (one bound to one state key or driver setting)
```

Page-level config:

```json
"ui": {
  "page": "Thermostat",          // Page title shown у nav
  "icon": "thermometer",          // Icon name (lucide-svelte set)
  "page_id": "thermostat",        // Optional URL slug
  "access_level": "user",         // "user" / "service" / "admin"
  "cards": [...]
}
```

Card-level config:

```json
{
  "title": "State",
  "subtitle": "Current readings",   // Optional sub-header
  "layout": "single",               // "single" / "grid" / "split"
  "visible_when": {                 // Optional show/hide condition
    "scenario.engine_active_count": {">": 0}
  },
  "widgets": [...]
}
```

## Widget catalog

### `value` — read-only display

Renders а number, string, or bool as text із optional unit. Default
fallback widget if `widget` field omitted.

```json
{"key": "simple_thermo.temperature", "widget": "value"}
```

Optional config fields:
- `unit` — appended after value (e.g., `"°C"`).
- `format` — printf-style format string (e.g., `"%.2f"` for two decimals).
- `prefix` / `suffix` — text wrappers.

Use for sensor readings, computed metrics, status text. Most common widget.

### `indicator` — LED-style on/off

Renders а colored circle для boolean state. Default green=on, gray=off.

```json
{
  "key": "simple_thermo.output",
  "widget": "indicator",
  "label_on": "Heating",
  "label_off": "Idle"
}
```

The `on_label` / `off_label` fields у the manifest's `state` declaration
provide default labels; widget-level overrides win.

Use for alarm states, equipment ON/OFF, fault flags.

### `slider` — range slider із live update

Drag-to-set numeric input із min/max/step from the state's manifest
declaration (or widget-level overrides).

```json
{
  "key": "simple_thermo.setpoint",
  "widget": "slider"
}
```

Defaults: pulls min/max/step from state declaration. Override at widget
level if needed:

```json
{
  "key": "simple_thermo.setpoint",
  "widget": "slider",
  "min": 10, "max": 30, "step": 0.5
}
```

Debounce: 300 ms after last move to avoid network spam. Pending state
shown during write; flashes green on success.

Use for setpoints, gains, deadbands. Best for continuous values із meaningful
range.

### `number_input` — spin-box numeric input

Discrete numeric input із +/− buttons і keyboard typing. Better than
slider for high-resolution values or wide ranges (е.g., 1-3600 seconds).

```json
{
  "key": "simple_thermo.differential",
  "widget": "number_input"
}
```

Same min/max/step inheritance as slider.

### `toggle` — bool switch

Sliding ON/OFF toggle із tactile feedback. Writes immediately on tap.

```json
{
  "key": "alarms.enabled",
  "widget": "toggle"
}
```

Differs from `indicator` — indicator is read-only display, toggle is
writable input.

### `select` — dropdown із enum

Picks one of а fixed set of values. State type can be `int` or `string`.

```json
{
  "key": "thermo.mode",
  "widget": "select",
  "options": [
    {"value": "cooling", "label": "Cooling"},
    {"value": "heating", "label": "Heating"},
    {"value": "off",     "label": "Off"}
  ]
}
```

Use for mode pickers, profile selectors, enumerated settings.

### `text_input` — free-form string

Single-line text editor. Writes on blur or Enter.

```json
{
  "key": "device.name",
  "widget": "text_input",
  "placeholder": "Device name",
  "max_length": 32
}
```

Use for names, labels, MQTT topic prefixes — anything where the schema is
just "valid UTF-8 up to N chars".

### `password_input` — masked text

Same as `text_input` but renders dots (•). Includes а show/hide eye button.

```json
{
  "key": "wifi.password",
  "widget": "password_input"
}
```

Use for WiFi passwords, MQTT credentials, API tokens.

### `datetime_input` — date/time picker

Reads/writes an ISO-8601 timestamp string.

```json
{
  "key": "ota.scheduled_at",
  "widget": "datetime_input"
}
```

### `chart` — time-series chart from datalogger

Renders а SVG chart showing recent history of а logged channel. Pulls from
datalogger's `/api/datalogger/series` endpoint.

```json
{
  "key": "equipment.air_temp",
  "widget": "chart",
  "window": "1h",                // "10m" / "1h" / "24h" / "7d"
  "height": 200
}
```

Requires the key be declared у the manifest's `loggable.channels` section
із а matching name. See [persistence.md](persistence.md) *(planned)* і
[modules/datalogger.md](../03-framework-reference/modules/datalogger.md)
*(planned)*.

### `button` — trigger an action

Renders а button що POSTs to an action endpoint.

```json
{
  "widget": "button",
  "label": "Reboot device",
  "endpoint": "/api/restart",
  "confirm": "Are you sure?"
}
```

No `key` field — button doesn't bind to state. The `endpoint` field
specifies which HTTP API path to hit.

Use for irreversible commands (reboot, factory reset, OTA trigger).

### `status_text` — semantic status із color

Like `value` but maps state strings to colored badges:

```json
{
  "key": "simple_thermo.state",
  "widget": "status_text",
  "states": {
    "off":     {"color": "gray",   "label": "Off"},
    "heating": {"color": "orange", "label": "Heating"},
    "idle":    {"color": "green",  "label": "Idle"},
    "fault":   {"color": "red",    "label": "Fault"}
  }
}
```

Use for FSM state visualization (scenario states, equipment status, alarm
levels).

## Specialised system widgets

The framework ships several pre-built widgets for system-level operations.
These take fixed configurations і live у the framework's own pages — most
module authors don't need to use them directly але reference here для
completeness.

| Widget | Purpose | Where it's used |
|---|---|---|
| `firmware_upload` | OTA file upload UI із progress | System / Firmware page |
| `wifi_scan` | Scan і pick WiFi networks | Network page |
| `wifi_save` | Form for WiFi credentials | Network page |
| `ap_save` | Configure AP fallback | Network page |
| `mqtt_save` | MQTT broker settings form | Network → MQTT |
| `time_save` | NTP / manual time configuration | System → Time |
| `timezone_select` | Timezone dropdown із presets | System → Time |
| `auth_save` | Change admin credentials | System → Auth |
| `cloud_save` | AWS IoT or MQTT cloud configuration | Network → Cloud |
| `cert_upload` | Upload TLS certificates | Network → Cloud |
| `file_upload` | Generic file upload to LittleFS | System (rarely) |
| `actions_grid` | Grid of buttons для administrative ops | System |

These are wired up у the framework's own manifest sections (under
`components/modesp_net/`, `modules/equipment/`, etc.). Reference but don't
typically reuse у your own modules.

## Driver-specific widgets

For drivers (manifest's `driver` field, not `module`), use `setting`
instead of `key`:

```json
"ui": {
  "page": "Sensor settings",
  "cards": [{
    "title": "DS18B20: {{hardware_id}}",
    "instance_per_binding": true,
    "widgets": [
      {"setting": "read_interval_ms", "widget": "number_input"},
      {"setting": "offset",           "widget": "slider"}
    ]
  }]
}
```

| Field | Notes |
|---|---|
| `setting` | References а key у the driver's `settings` array, not а state key. |
| `instance_per_binding` | If `true`, the card renders once per bound driver instance (each із its own `{{hardware_id}}` substitution). |

Substitution tokens у card titles:
- `{{hardware_id}}` — the binding's `hardware` field (board.json ID).
- `{{role}}` — the binding's `role` field.
- `{{address}}` — the binding's `address` field (if present).

Use to give each sensor card а unique title showing what physical hardware
it controls.

## `visible_when` — conditional display

Cards (and some widgets) accept а `visible_when` clause to show/hide based
on state values:

```json
"visible_when": {
  "scenario.engine_active_count": {">": 0}
}
```

Operators:
- `{"==": value}` — equal
- `{"!=": value}` — not equal
- `{">": value}`, `{"<": value}`, `{">=": value}`, `{"<=": value}` — comparisons
- `{"in": [val1, val2]}` — value у list

Shorthand: array means "value у list":

```json
"visible_when": {
  "abs_test.scenario_state": ["running", "paused", "completed"]
}
```

Multiple keys: all must match (AND):

```json
"visible_when": {
  "thermo.mode": "heating",
  "alarms.enabled": true
}
```

Use to hide setup pages once configured, show diagnostics only during
active operation, gate advanced settings behind service mode.

## Card layouts

```json
"layout": "single"   // One column, widgets stacked top-to-bottom
"layout": "grid"     // Two-column responsive grid
"layout": "split"    // Title + main value emphasis, used для prominent readings
```

Pick `single` by default — simple і readable. `grid` for dense
config-heavy cards. `split` rarely needed.

## i18n у widget labels

The framework ships із UK / EN / DE / PL translation packs. Widget labels,
card titles, і unit suffixes can use translation keys instead of literal
strings:

```json
{
  "key": "thermo.setpoint",
  "widget": "slider",
  "label_key": "thermo.label.setpoint",
  "unit_key": "common.unit.celsius"
}
```

Translation keys are resolved at render time. Add your translations
у `modules/<your_module>/i18n/<lang>.json`. The generator merges them до
the final `data/www/i18n/<lang>.json` packs.

If literal strings are simpler (you target one language), use `label` /
`unit` directly. Mix is fine.

## Common mistakes

**Forgetting `widget` field:** if you omit `"widget": "..."`, the renderer
defaults to `ValueWidget` (read-only display). For an editable widget you
must specify the type explicitly.

**Using widget for а non-existent state key:** widgets render `null` and
look broken. Make sure the key is declared у some module's `state`
section. Generator currently doesn't cross-validate widget keys, але
visual breakage at runtime is the symptom.

**Slider із huge range:** sliders work best for 50-500 step counts. If
min/max/step gives 10000 steps, single pixel = 50 units. Use
`number_input` instead.

**Toggle on read-only state:** toggle widget writes to its key. If state
declares `access: "read"`, write rejects (HTTP 403). Use `indicator`
instead.

**Confusing `visible_when` semantics:** array shorthand means "value у
list", not "any of these conditions". For OR logic across keys you'd need
а computed state key (Stage 1.5 may add OR support).

**Driver widgets без `instance_per_binding`:** rendering one card для all
DS18B20 instances doesn't make sense — each binding has its own address і
offset. Always use `instance_per_binding: true` для per-driver-instance
settings.

## Worked example

`modules/simple_thermo/manifest.json` UI section (real, shipped code):

```json
"ui": {
  "page": "Thermostat",
  "icon": "thermometer",
  "cards": [
    {
      "title": "State",
      "subtitle": "Temperature, mode",
      "layout": "single",
      "widgets": [
        {"key": "simple_thermo.temperature", "widget": "value"},
        {"key": "simple_thermo.state", "widget": "value"},
        {"key": "simple_thermo.output", "widget": "indicator"}
      ]
    },
    {
      "title": "Settings",
      "subtitle": "Setpoint, differential",
      "layout": "single",
      "widgets": [
        {"key": "simple_thermo.setpoint", "widget": "slider"},
        {"key": "simple_thermo.differential", "widget": "number_input"}
      ]
    }
  ]
}
```

Resulting WebUI: а "Thermostat" page із two cards. First card shows
current readings (read-only). Second card has interactive setpoint
slider і differential spinner.

## Next steps

- **[manifest.md](manifest.md#section-ui-any-module--driver)** — manifest
  syntax для UI sections.
- **[shared-state.md](shared-state.md)** — what bind your widgets to.
- **[mqtt.md](mqtt.md)** *(planned)* — publishing state changes to external
  MQTT clients.
- **[components/modesp_net.md](../03-framework-reference/components/modesp_net.md)**
  *(planned)* — HTTP / WebSocket internals.
- **[modules/datalogger.md](../03-framework-reference/modules/datalogger.md)**
  *(planned)* — backing для `chart` widget.

## Source

- [`webui/src/components/WidgetRenderer.svelte`](../../../webui/src/components/WidgetRenderer.svelte)
  — widget dispatch table.
- [`webui/src/components/widgets/`](../../../webui/src/components/widgets/)
  — individual widget implementations.
- [`modules/simple_thermo/manifest.json`](../../../modules/simple_thermo/manifest.json)
  — minimal UI example.
- [`modules/datalogger/manifest.json`](../../../modules/datalogger/manifest.json)
  — chart widget example.
