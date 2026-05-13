# Reference маніфесту

> 📖 **In English:** [documentation/en/02-module-author-guide/manifest.md](../../en/02-module-author-guide/manifest.md)

Маніфест — це контракт вашого модуля з фреймворком. JSON file що декларує
все що build system повинен знати: які state keys ви exposing, які UI widgets
рендеряться, які MQTT topics публікуються, що persist-иться, і (для рецептів)
яка phase machine драйвить виконання.

Ця сторінка — reference для кожної секції з реальними прикладами із модулів
що поставляються з фреймворком. Прочитавши, ви зможете написати повний
маніфест для нового модуля, recipe або драйвера без consult-ування existing
коду.

## Три категорії маніфестів

ModESP розпізнає три типи на основі top-level полів. Build pipeline обирає
правильний генератор для кожного.

| Категорія | Папка | Розрізняюче поле | C++ код? |
|---|---|---|---|
| **Service module** | `modules/<name>/` | `"module": "..."` | Так (BaseModule subclass) |
| **Recipe module** | `modules/<name>/` | `"module_type": "recipe"` + `"scenario"` секція | Ні |
| **Driver** | `drivers/<name>/` | `"driver": "..."` | Так (IDriver subclass) |

Секції тут стосуються одного або кількох типів. Заголовки маркують це.

## Top-level поля

З'являються у JSON root для кожного маніфесту:

| Поле | Тип | Обов'язкове | Примітки |
|---|---|---|---|
| `manifest_version` | int | так | Зараз `1`. Bump-иться при incompatible зміні схеми. |
| `module` | string | одне з цих двох | Ім'я модуля (відповідає папці під `modules/`). Snake_case, ≤ 16 chars. |
| `driver` | string | одне з цих двох | Ім'я драйвера (відповідає папці під `drivers/`). Snake_case. |
| `module_type` | string | лише recipes | Встановити у `"recipe"` для pure manifest модулів без C++. |
| `version` | string | рекомендоване | Semver — для вашого власного tracking. |
| `description` | string | рекомендоване | Однорядковий human-readable опис. |
| `priority` | int | service модулі | Init phase: `0`=CRITICAL, `1`=HIGH, `2`=NORMAL, `3`=LOW. Див. [overview.md](overview.md#3--three-phase-init-lifecycle). |
| `category` | string | лише драйвери | `"sensor"` або `"actuator"`. |
| `hardware_type` | string | лише драйвери | `"gpio"`, `"onewire_bus"`, `"adc"`, `"i2c"`, `"rs485"`, тощо. |

Приклад top of `modules/simple_thermo/manifest.json`:

```json
{
  "manifest_version": 1,
  "module": "simple_thermo",
  "version": "1.0.0",
  "description": "Simple ON/OFF thermostat — demo module",
  "priority": 2
}
```

Приклад top of `drivers/ds18b20/manifest.json`:

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

## Секція: `state` (service модулі)

Декларує SharedState keys що модуль володіє. Кожен key отримує типізовану
metadata що тече до: згенерованої C++ state table, WebUI auto-card, MQTT
topic strings, і datalogger channel discovery.

**Конвенція іменування:** `<module>.<key_name>`, один dot, ≤ 32 символів total.

### Per-key поля

| Поле | Тип | Обов'язкове | Примітки |
|---|---|---|---|
| `type` | `"int"` / `"float"` / `"bool"` / `"string"` | так | Маппиться на `modesp::StateValue` variant cases. |
| `access` | `"read"` / `"readwrite"` | так | `"read"` = display-only у UI; `"readwrite"` = user може встановлювати через WebUI/MQTT. |
| `default` | typed | optional | Initial value якщо нема persisted override. Типізоване повинно match `type`. |
| `min` / `max` / `step` | numeric | лише numeric типи | Bounds для UI widgets і validation. |
| `unit` | string | optional | `"°C"`, `"%"`, `"мс"` — показується поруч із value у UI. |
| `description` | string | optional | Tooltip у UI. |
| `persist` | bool | optional | `true` → PersistService зберігає/відновлює через reboot. Default `false`. |
| `mqtt_subscribe` | bool | optional | `true` → key writable через MQTT. Потребує `access: "readwrite"`. |
| `on_label` / `off_label` | string | лише bool | Лейбли для indicator widget (наприклад `"ON"`/`"OFF"`, `"Нагрів"`/`"Idle"`). |

### Приклад

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

> 💡 **Tip:** генератор виробляє `state_meta.h` з constexpr table усіх
> декларованих keys і їхніх типів. Ваш C++ код може referencing keys через
> згенеровані константи — typos це compile errors, не runtime "key not found".

## Секція: `ui` (будь-який модуль / driver)

Декларативний WebUI. Генератор зливає всі module/driver `ui` секції у єдиний
`data/ui.json` що Svelte SPA завантажує. Жодного Svelte коду не проходить
через ваші руки; ви пишете JSON.

### Top-level shape

```json
"ui": {
  "page": "Термостат",              // Page title показаний у навігації
  "icon": "thermometer",            // Icon name (lucide / heroicons)
  "page_id": "thermostat",          // Optional URL slug. Default derived з page name.
  "access_level": "user",           // "user" / "service" / "admin" — gates visibility
  "cards": [...]                    // Масив card definitions
}
```

### Cards

Card — це grouped widget container що рендериться як одна panel у page.

```json
{
  "title": "Стан",                  // Заголовок картки
  "subtitle": "Температура, режим", // Optional sub-heading
  "layout": "single",               // "single" / "grid" / "split"
  "visible_when": {                 // Optional — показувати лише коли state key matches
    "scenario.engine_active_count": {">": 0}
  },
  "widgets": [...]
}
```

### Widgets

Найпростіша widget shape:

```json
{"key": "simple_thermo.temperature", "widget": "value"}
```

Найпоширеніші widget types:

| `widget` значення | Рендерить | Кращий для |
|---|---|---|
| `"value"` | Read-only number/string display з unit | Sensor readings, computed state. |
| `"indicator"` | LED-style on/off circle | Bool outputs, alarm state. |
| `"slider"` | Range slider з min/max/step | Setpoints, gain values. |
| `"number_input"` | Spin-box numeric input | Precise values, large ranges. |
| `"select"` | Dropdown із enum values | Mode pickers (HEATING/COOLING). |
| `"toggle"` | Bool switch | On/off configuration flags. |
| `"chart"` | Time-series chart pull-ається з datalogger | Temperature history, trend lines. |

Driver-specific shape (зверніть увагу `setting` замість `key`):

```json
{
  "title": "DS18B20: {{hardware_id}}",
  "instance_per_binding": true,     // Рендерити одну картку на bound instance
  "widgets": [
    {"setting": "read_interval_ms", "widget": "number_input"},
    {"setting": "offset",           "widget": "slider"},
    {"setting": "resolution",       "widget": "select"}
  ]
}
```

## Секція: `mqtt` (service модулі)

Перелічує які state keys публікуються у MQTT, і які приймають MQTT writes.
Генератор виробляє topic strings форми `<base>/<module>/<key_name>` де
`<base>` сконфігурований у `sdkconfig` (default `modesp/<device-id>`).

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

**Правила:**
- Keys у `subscribe` повинні мати `access: "readwrite"` і `mqtt_subscribe: true`
  у `state` декларації. Генератор валідує це при build.
- Published values отримують throttled delta-publish (Stage 1.5 буде
  документувати rates і LWT handling у [mqtt.md](mqtt.md) *(planned)*).

## Секція: `loggable` (service модулі)

Wire-ить state keys до DataLogger модуля — channels для time-series, events
для edge logging.

```json
"loggable": {
  "channels": {
    "simple_thermo.temperature": {
      "type": "temperature",
      "label": "Температура",
      "default": true              // Увімкнено out of the box
    }
  },
  "events": {
    "simple_thermo.output": {
      "id": 30,                    // Stable event ID — призначити раз, ніколи не змінювати
      "edge": "both",              // "rising" / "falling" / "both"
      "label_on": "Нагрів ON",
      "label_off": "Нагрів OFF"
    }
  }
}
```

> ⚠️ **Warning:** event `id` — це stable byte-level identifier збережений у
> datalogger flash files. **Ніколи не reuse-айте і не renumber-уйте** існуючі
> IDs — interpretation історичних даних поламається. Підберіть unused IDs
> скануючи маніфести усіх модулів для current values.

## Секція: `features` (service модулі, optional)

Декларує compile-time feature flags. Генератор виробляє константи у
`features_config.h` що ваш C++ і інші генератори читають при compile time.

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
    "description": "Hard cap на setpoint (°C)"
  }
}
```

Використовуйте sparingly. Більшість "configuration" повинна жити у `state`
з `persist: true` (runtime-changeable, без rebuild). Features — для
compile-time-only виборів: flash size optimisations, mutually exclusive code
paths, debug gating.

## Секція: `scenario` (лише recipe модулі)

Це DSL рецептів — phase/transition graph скомпільований у бінарний `.modr`
через `tools/compile_scenario.py`. Engine виконує отриманий blob при runtime.

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
        // ... ще phases
      ]
    }
  ]
}
```

Повний reference: [recipe-authoring.md](recipe-authoring.md) *(planned)* і
існуючі
[scenario-engine docs](../03-framework-reference/scenario-engine/).

## Driver-only секції

### `settings`

Persisted config schema для driver instance (одне driver definition може
мати багато bound instances — різні sensors того ж типу).

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

Поля mirror-ять `state` per-key поля. Settings зберігаються у NVS під
`drv.<driver>.<instance>.<key>`.

### `provides`

Що драйвер виробляє (sensors) або приймає (actuators).

```json
// Sensor driver:
"provides": {"type": "float", "unit": "°C", "range": [-55, 125]}

// Actuator driver:
"provides": {"type": "bool", "description": "Стан реле"}
```

### `requires_address`

Driver instance binding потребує hardware addressing (OneWire ROM, I2C
address, тощо). Коли `true`, `bindings.json` повинен supply `address` поле
per instance.

```json
"requires_address": true,
"multiple_per_bus": true            // > 1 instance може ділити шину
```

### `discovery`

Optional scanner endpoint — UI button що probe-ить hardware bus і повертає
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

## Валідація і build integration

`tools/generate_ui.py` запускається як pre-build CMake hook. Він:

1. Discover-ить кожен `manifest.json` під `modules/` і `drivers/`.
2. Валідує кожен проти схеми (цей документ — human-readable форма тієї схеми).
3. Cross-валідує посилання: кожен key у `mqtt.subscribe` повинен існувати у
   `state`; кожен `setting.key` повинен бути unique у межах драйвера; кожен
   recipe `scenario` mirror key повинен бути pre-declared у recipe's `state`
   секції.
4. Виробляє artifacts у `generated/` і `data/` (див.
   [tools/generate_ui.md](../05-tools/generate_ui.md) *(planned)*).

Падіння у будь-якому кроці aborts build з конкретним error message —
невалідні маніфести ніколи не доходять до runtime.

## Поширені помилки

**Забутий `priority`:** service module без `priority` потрапляє у NORMAL phase
(priority `2`) за замовчуванням — зазвичай ОК. Critical модулі (watchdog,
error service) потребують `priority: 0`; HAL і драйвери потребують
`priority: 1`. Пізні init phases (HTTP, WS) отримують `priority: 3`.

**State key довжина > 32 chars:** SharedState enforces 32-char key limit.
`<module>.<key>` бюджет: module name ≤ 12 chars + dot + key ≤ 19 chars.
Recipe authors хіт-ять це з довгими phase names — recipe name budget
зменшується до ≤ 8.

**Забутий `mqtt_subscribe: true` для writable keys:** key із
`access: "readwrite"` перерахований у `mqtt.subscribe` але без флагу —
генератор відхиляє з чітким повідомленням.

**`features` сплутаний з `state`:** features — compile-time, state —
runtime. Якщо users будуть adjusting value через WebUI — використовуйте
`state`. Якщо вибір gates entire code blocks — використовуйте `features`.

**Recipe з undeclared mirror keys:** scenarios пишуть mirror keys
(`<recipe>.scenario_state`, тощо) — ці МУСЯТЬ бути pre-declared у recipe
manifest's `state` section, навіть хоча їх пише engine, не recipe.
Компілятор cross-валідує і fails build інакше.

## Що далі

- **[writing-a-module.md](writing-a-module.md)** *(planned)* — перетворити
  маніфест у working C++ модуль.
- **[writing-a-driver.md](writing-a-driver.md)** *(planned)* — driver
  authoring з IDriver інтерфейсом.
- **[recipe-authoring.md](recipe-authoring.md)** *(planned)* — `scenario`
  секція deep dive.
- **[ui-widgets.md](ui-widgets.md)** *(planned)* — усі widget types з
  rendered прикладами.

## Приклади для вивчення

Existing маніфести варто прочитати source-first:

- [`modules/simple_thermo/manifest.json`](../../../modules/simple_thermo/manifest.json) —
  мінімальний service module з state, mqtt, loggable, ui. ~100 рядків.
- [`modules/abs_test/manifest.json`](../../../modules/abs_test/manifest.json) —
  recipe module з 2-track `scenario` секцією.
- [`drivers/ds18b20/manifest.json`](../../../drivers/ds18b20/manifest.json) —
  driver з settings, provides, discovery.
- [`modules/datalogger/manifest.json`](../../../modules/datalogger/manifest.json) —
  більший service module з channels, features.
