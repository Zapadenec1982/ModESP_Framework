# UI widgets

> 📖 **In English:** [documentation/en/02-module-author-guide/ui-widgets.md](../../en/02-module-author-guide/ui-widgets.md)

WebUI фреймворку — це Svelte SPA що рендериться повністю з JSON schema
(`data/ui.json`) згенерованої з ваших module маніфестів при build time. Ви
не пишете Svelte код, не торкаєтесь CSS — ви декларуєте widgets, cards, і
pages у JSON, і вони auto-рендеряться на WebUI пристрою.

Ця сторінка — каталог widgets: кожен widget type, його config поля, що він
рендерить, коли його використовувати. Прочитавши, ви знатимете який widget
обрати для кожного state key і як композувати їх у cards.

## Ментальна модель

UI schema flow:

```
   modules/*/manifest.json::ui  +  drivers/*/manifest.json::ui
                  │
                  ▼ (generate_ui.py merges + validates)
                  │
   data/ui.json  ──→  serv-иться при GET /api/ui
                  │
                  ▼ (Svelte SPA loads при boot)
                  │
   Pages, cards, widgets рендеряться
```

Widget — один із ~25 Svelte компонентів зареєстрованих у
`WidgetRenderer.svelte`. Ви reference-ите його через `widget` поле у вашій
manifest's UI section. Widgets read/write SharedState keys через common
WebSocket pipeline; ви не керуєте цим wiring manually.

## Page і card hierarchy

```
Page (top-level navigation entry)
└── Card (grouped panel)
    └── Widget (один bound до одного state key або driver setting)
```

Page-level config:

```json
"ui": {
  "page": "Thermostat",          // Page title показаний у nav
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

## Каталог widget-ів

### `value` — read-only display

Рендерить число, рядок, або bool як текст з optional unit. Default
fallback widget якщо `widget` поле відсутнє.

```json
{"key": "simple_thermo.temperature", "widget": "value"}
```

Optional config поля:
- `unit` — appended після value (наприклад `"°C"`).
- `format` — printf-style format string (наприклад `"%.2f"` для two
  decimals).
- `prefix` / `suffix` — text wrappers.

Use для sensor readings, computed metrics, status text. Найпоширеніший
widget.

### `indicator` — LED-style on/off

Рендерить кольоровий circle для boolean state. Default green=on, gray=off.

```json
{
  "key": "simple_thermo.output",
  "widget": "indicator",
  "label_on": "Heating",
  "label_off": "Idle"
}
```

Поля `on_label` / `off_label` у manifest's `state` declaration provide
default labels; widget-level overrides win.

Use для alarm states, equipment ON/OFF, fault flags.

### `slider` — range slider з live update

Drag-to-set numeric input з min/max/step з state's manifest declaration
(або widget-level overrides).

```json
{
  "key": "simple_thermo.setpoint",
  "widget": "slider"
}
```

Defaults: pulls min/max/step з state declaration. Override на widget
level якщо треба:

```json
{
  "key": "simple_thermo.setpoint",
  "widget": "slider",
  "min": 10, "max": 30, "step": 0.5
}
```

Debounce: 300 мс після last move щоб avoid network spam. Pending state
показаний during write; flashes green при success.

Use для setpoints, gains, deadbands. Найкращий для continuous values з
meaningful range.

### `number_input` — spin-box numeric input

Discrete numeric input з +/− кнопками і keyboard typing. Кращий ніж slider
для high-resolution values або wide ranges (наприклад, 1-3600 секунд).

```json
{
  "key": "simple_thermo.differential",
  "widget": "number_input"
}
```

Той самий min/max/step inheritance як slider.

### `toggle` — bool switch

Sliding ON/OFF toggle з tactile feedback. Writes immediately при tap.

```json
{
  "key": "alarms.enabled",
  "widget": "toggle"
}
```

Відрізняється від `indicator` — indicator — read-only display, toggle —
writable input.

### `select` — dropdown з enum

Picks one of fixed set of values. State type може бути `int` або `string`.

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

Use для mode pickers, profile selectors, enumerated settings.

### `text_input` — free-form string

Single-line text editor. Writes при blur або Enter.

```json
{
  "key": "device.name",
  "widget": "text_input",
  "placeholder": "Device name",
  "max_length": 32
}
```

Use для names, labels, MQTT topic prefixes — будь-що де schema — це
"valid UTF-8 до N chars".

### `password_input` — masked text

Те саме що `text_input` але рендерить dots (•). Включає show/hide eye
button.

```json
{
  "key": "wifi.password",
  "widget": "password_input"
}
```

Use для WiFi passwords, MQTT credentials, API tokens.

### `datetime_input` — date/time picker

Reads/writes ISO-8601 timestamp string.

```json
{
  "key": "ota.scheduled_at",
  "widget": "datetime_input"
}
```

### `chart` — time-series chart з datalogger

Рендерить SVG chart що показує recent history of logged channel. Pulls з
datalogger's `/api/datalogger/series` endpoint.

```json
{
  "key": "equipment.air_temp",
  "widget": "chart",
  "window": "1h",                // "10m" / "1h" / "24h" / "7d"
  "height": 200
}
```

Потребує щоб key був declared у manifest's `loggable.channels` section з
matching name. Див. [persistence.md](persistence.md) *(planned)* і
[modules/datalogger.md](../03-framework-reference/modules/datalogger.md)
*(planned)*.

### `button` — trigger action

Рендерить кнопку що POST-ить до action endpoint.

```json
{
  "widget": "button",
  "label": "Reboot device",
  "endpoint": "/api/restart",
  "confirm": "Are you sure?"
}
```

Без `key` поля — button не bind-иться до state. Поле `endpoint`
specifies який HTTP API path hit-ити.

Use для irreversible commands (reboot, factory reset, OTA trigger).

### `status_text` — semantic status з кольором

Як `value` але maps state strings до colored badges:

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

Use для FSM state visualization (scenario states, equipment status, alarm
levels).

## Specialised system widgets

Фреймворк поставляється з кількома pre-built widgets для system-level
operations. Вони беруть fixed configurations і живуть у власних pages
фреймворку — більшість module authors не потребує їх використовувати
безпосередньо але reference тут для completeness.

| Widget | Призначення | Де використовується |
|---|---|---|
| `firmware_upload` | OTA file upload UI з progress | System / Firmware page |
| `wifi_scan` | Scan і pick WiFi networks | Network page |
| `wifi_save` | Form для WiFi credentials | Network page |
| `ap_save` | Configure AP fallback | Network page |
| `mqtt_save` | MQTT broker settings form | Network → MQTT |
| `time_save` | NTP / manual time configuration | System → Time |
| `timezone_select` | Timezone dropdown з presets | System → Time |
| `auth_save` | Change admin credentials | System → Auth |
| `cloud_save` | AWS IoT or MQTT cloud configuration | Network → Cloud |
| `cert_upload` | Upload TLS certificates | Network → Cloud |
| `file_upload` | Generic file upload to LittleFS | System (rarely) |
| `actions_grid` | Grid of buttons для administrative ops | System |
| `defrost_toggle` | Refrigeration-specific manual defrost | Equipment (refrigeration only) |

Вони wire-яться у власних manifest sections фреймворку (під
`components/modesp_net/`, `modules/equipment/`, тощо). Reference але не
типово reuse у власних модулях.

## Driver-specific widgets

Для drivers (manifest's `driver` field, не `module`), використовуйте
`setting` замість `key`:

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

| Поле | Notes |
|---|---|
| `setting` | References key у driver's `settings` array, не state key. |
| `instance_per_binding` | Якщо `true`, card рендериться раз per bound driver instance (кожен зі своєю `{{hardware_id}}` substitution). |

Substitution tokens у card titles:
- `{{hardware_id}}` — binding's `hardware` поле (board.json ID).
- `{{role}}` — binding's `role` поле.
- `{{address}}` — binding's `address` поле (якщо є).

Use щоб дати кожній sensor card унікальний title що показує яке фізичне
hardware вона controls.

## `visible_when` — conditional display

Cards (і деякі widgets) accept `visible_when` clause to show/hide based
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

Shorthand: array означає "value у list":

```json
"visible_when": {
  "abs_test.scenario_state": ["running", "paused", "completed"]
}
```

Multiple keys: всі повинні match (AND):

```json
"visible_when": {
  "thermo.mode": "heating",
  "alarms.enabled": true
}
```

Use to hide setup pages після configure, show diagnostics лише during
active operation, gate advanced settings за service mode.

## Card layouts

```json
"layout": "single"   // Одна колонка, widgets stacked top-to-bottom
"layout": "grid"     // Two-column responsive grid
"layout": "split"    // Title + main value emphasis, used для prominent readings
```

Pick `single` за замовчуванням — simple і readable. `grid` для dense
config-heavy cards. `split` рідко потрібен.

## i18n у widget labels

Фреймворк поставляється з UK / EN / DE / PL translation packs. Widget
labels, card titles, і unit suffixes можуть використовувати translation
keys замість literal strings:

```json
{
  "key": "thermo.setpoint",
  "widget": "slider",
  "label_key": "thermo.label.setpoint",
  "unit_key": "common.unit.celsius"
}
```

Translation keys resolve at render time. Додайте ваші переклади у
`modules/<your_module>/i18n/<lang>.json`. Генератор merges їх у final
`data/www/i18n/<lang>.json` packs.

Якщо literal strings простіше (ви target одну мову), use `label` /
`unit` directly. Mix — fine.

## Поширені помилки

**Забутий `widget` field:** якщо ви omit `"widget": "..."`, renderer
defaults до `ValueWidget` (read-only display). Для editable widget вам
треба specify type explicitly.

**Use widget для non-existent state key:** widgets render `null` і
виглядають broken. Make sure key declared у якомусь module's `state`
section. Генератор зараз не cross-валідує widget keys, але visual
breakage at runtime — це симптом.

**Slider з huge range:** sliders work best для 50-500 step counts.
Якщо min/max/step дає 10000 steps, single pixel = 50 units. Use
`number_input` замість.

**Toggle на read-only state:** toggle widget writes до його key. Якщо
state декларує `access: "read"`, write rejects (HTTP 403). Use
`indicator` замість.

**Confusing `visible_when` semantics:** array shorthand означає "value у
list", не "any of these conditions". Для OR logic across keys потрібен
computed state key (Stage 1.5 може додати OR support).

**Driver widgets без `instance_per_binding`:** rendering одна card для
всіх DS18B20 instances не має сенсу — кожен binding має свою address і
offset. Завжди use `instance_per_binding: true` для per-driver-instance
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

Результуючий WebUI: "Thermostat" page з двома cards. Перша card показує
current readings (read-only). Друга card має interactive setpoint slider
і differential spinner.

## Що далі

- **[manifest.md](manifest.md#section-ui-any-module--driver)** — manifest
  syntax для UI sections.
- **[shared-state.md](shared-state.md)** — що bind ваші widgets.
- **[mqtt.md](mqtt.md)** *(planned)* — publishing state changes до
  external MQTT clients.
- **[components/modesp_net.md](../03-framework-reference/components/modesp_net.md)**
  *(planned)* — HTTP / WebSocket internals.
- **[modules/datalogger.md](../03-framework-reference/modules/datalogger.md)**
  *(planned)* — backing для `chart` widget.

## Source

- [`webui/src/components/WidgetRenderer.svelte`](../../../webui/src/components/WidgetRenderer.svelte)
  — widget dispatch table.
- [`webui/src/components/widgets/`](../../../webui/src/components/widgets/)
  — окремі widget implementations.
- [`modules/simple_thermo/manifest.json`](../../../modules/simple_thermo/manifest.json)
  — мінімальний UI приклад.
- [`modules/datalogger/manifest.json`](../../../modules/datalogger/manifest.json)
  — chart widget приклад.
