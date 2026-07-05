# Довідник маніфесту

> 📖 **In English:** [documentation/en/02-module-author-guide/manifest.md](../../en/02-module-author-guide/manifest.md)

Маніфест — це контракт вашого модуля з фреймворком. Це JSON-файл, який
декларує все, що повинна знати система збірки: які ключі стану ви
експонуєте, які віджети UI рендеряться, які теми MQTT публікуються, що
зберігається, і (для рецептів) яка машина фаз керує виконанням.

Ця сторінка — довідкова документація для кожної секції з реальними
прикладами з модулів, що поставляються з фреймворком. Прочитавши, ви
зможете написати повний маніфест для нового модуля, рецепта або драйвера
без звернення до наявного коду.

## Три різновиди маніфестів

ModESP розрізняє три типи на основі полів верхнього рівня. Конвеєр збірки
обирає потрібний генератор коду для кожного з них.

| Різновид | Папка | Розрізняюче поле | Має C++-код? |
|---|---|---|---|
| **Сервісний модуль** | `modules/<name>/` | `"module": "..."` | Так (підклас BaseModule) |
| **Модуль-рецепт** | `modules/<name>/` | `"module_type": "recipe"` + секція `"scenario"` | Ні |
| **Драйвер** | `drivers/<name>/` | `"driver": "..."` | Так (підклас IDriver) |

Секції, описані тут, стосуються одного або кількох різновидів. Заголовки
позначають, кого саме.

## Поля верхнього рівня

З'являються у корені JSON для кожного маніфесту:

| Поле | Тип | Обов'язкове | Примітки |
|---|---|---|---|
| `manifest_version` | int | так | Наразі `1`. Збільшується при несумісних змінах схеми. |
| `module` | string | одне з цих двох | Ім'я модуля (відповідає папці під `modules/`). Snake_case, ≤ 16 символів. |
| `driver` | string | одне з цих двох | Ім'я драйвера (відповідає папці під `drivers/`). Snake_case. |
| `module_type` | string | лише для рецептів | Встановити `"recipe"` для чистих маніфест-модулів без C++. |
| `version` | string | рекомендовано | Semver — для вашого власного відстеження. |
| `description` | string | рекомендовано | Однорядковий зрозумілий опис. |
| `priority` | int | сервісні модулі | Фаза ініціалізації: `0`=CRITICAL, `1`=HIGH, `2`=NORMAL, `3`=LOW. Див. [overview.md](overview.md#3--three-phase-init-lifecycle). |
| `category` | string | лише для драйверів | `"sensor"` або `"actuator"`. |
| `hardware_type` | string | лише для драйверів | `"gpio"`, `"onewire_bus"`, `"adc"`, `"i2c"`, `"rs485"` тощо. |

Приклад початку `modules/simple_thermo/manifest.json`:

```json
{
  "manifest_version": 1,
  "module": "simple_thermo",
  "version": "1.0.0",
  "description": "Simple ON/OFF thermostat — demo module",
  "priority": 2
}
```

Приклад початку `drivers/ds18b20/manifest.json`:

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

## Секція: `requires` (сервісні модулі)

Декларує **ролі** периферії, які потрібні модулю. Роль — це *здатність*
(capability), а не драйвер: термостат потребує «температуру» і не знає,
хто її дає (ds18b20 / NTC / BLE-канал / майбутній LoRa). Генератор і
runtime підбирають будь-яке джерело тієї ж capability під роль. Це
основний founding-принцип фреймворку — див.
[R0.1](../03-framework-reference/rules.md#r01--роль--здатність-capability-ніколи-не-драйвер)
і [R3.1](../03-framework-reference/rules.md#r31--матч-ролі-й-каналу--лише-за-capability).

### Поля на кожну роль

| Поле | Тип | Обов'язкове | Примітки |
|---|---|---|---|
| `role` | string | так | Ім'я ролі, за яким код резолвить джерело (`find_sensor("air_temp")`). Унікальне в межах модуля. |
| `type` | string | так | Груба категорія: `"sensor"` / `"actuator"`. |
| `capability` | string | рекомендовано | **Матчер.** Здатність, яку роль потребує — має бути в [`tools/capabilities.json`](../../../tools/capabilities.json). Будь-яке джерело тієї ж capability заповнює роль. Саме це поле, а НЕ `driver`, визначає збіг. |
| `kind` | `"sensor"` / `"actuator"` | опційно | Грубий дискримінатор напряму. Якщо відсутній — генератор нормалізує його з `type`. |
| `label` | string | опційно | Людиночитна назва ролі в UI прив'язок. **Без транспорту** («Room temperature», не «BLE room sensor») — [R1.3](../03-framework-reference/rules.md#r13--назви-ролейканалів--без-транспорту). |
| `optional` | bool | опційно | `true` → модуль стартує й без прив'язаного джерела. За замовчуванням `false`. |

### Приклад

Приклад із `modules/equipment/manifest.json`:

```json
"requires": [
  {"role": "air_temp",    "type": "sensor",   "capability": "temperature", "label": "Air temperature"},
  {"role": "room_temp",   "type": "sensor",   "capability": "temperature", "label": "Room temperature", "optional": true},
  {"role": "orientation", "type": "sensor",   "capability": "angle",       "label": "Orientation",      "optional": true},
  {"role": "actuator_1",  "type": "actuator", "capability": "relay_out",   "label": "Actuator 1",       "optional": true}
]
```

> ⚠️ **Ніколи не хардкодьте драйвер у ролі.** Роль оголошує capability;
> прив'язка конкретного заліза живе в `bindings.json` (поле `driver` +
> `device`), а ідентичність remote-пристрою (MAC/topic) — на рядку
> пристрою (board.json/devices.json), НІКОЛИ на ролі
> ([R0.3](../03-framework-reference/rules.md#r03--ідентичність--на-пристрої-ніколи-на-ролі)).
> Генератор перехресно валідує `capability` проти словника —
> невідома здатність ламає білд
> ([R8.3](../03-framework-reference/rules.md#r83--валідація-на-build-time)).

## Секція: `state` (сервісні модулі)

Декларує ключі SharedState, якими володіє модуль. Кожен ключ отримує
типізовану метаінформацію, що проходить далі до: згенерованої C++-таблиці
стану, авто-картки WebUI, рядків тем MQTT та виявлення каналів datalogger.

**Конвенція іменування:** `<module>.<key_name>`, одна крапка, ≤ 32
символів загалом.

### Поля на кожен ключ

| Поле | Тип | Обов'язкове | Примітки |
|---|---|---|---|
| `type` | `"int"` / `"float"` / `"bool"` / `"string"` | так | Відображається на варіанти `modesp::StateValue`. |
| `access` | `"read"` / `"readwrite"` | так | `"read"` = лише відображення у UI; `"readwrite"` = користувач може встановлювати через WebUI/MQTT. |
| `default` | typed | опційно | Початкове значення, якщо немає збереженого перевизначення. Тип має відповідати `type`. |
| `min` / `max` / `step` | numeric | лише для числових типів | Межі для віджетів UI та валідації. |
| `unit` | string | опційно | `"°C"`, `"%"`, `"мс"` — показується поруч зі значенням у UI. |
| `description` | string | опційно | Підказка у UI. |
| `persist` | bool | опційно | `true` → PersistService зберігає/відновлює між перезавантаженнями. За замовчуванням `false`. |
| `mqtt_subscribe` | bool | опційно | `true` → ключ доступний для запису через MQTT. Потребує `access: "readwrite"`. |
| `on_label` / `off_label` | string | лише для bool | Підписи для віджета-індикатора (напр., `"ON"`/`"OFF"`, `"Нагрів"`/`"Спокій"`). |

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

> 💡 **Підказка:** генератор створює `state_meta.h` з constexpr-таблицею
> всіх задекларованих ключів та їхніх типів. Ваш C++-код може посилатися
> на ключі через згенеровані константи — опечатки стають помилками
> компіляції, а не runtime-повідомленнями "key not found".

## Секція: `ui` (будь-який модуль / драйвер)

Декларативний WebUI. Генератор зливає всі секції `ui` модулів і драйверів
в єдиний `data/ui.json`, який завантажує Svelte SPA. Жоден Svelte-код не
проходить через ваші руки; ви пишете JSON.

### Форма верхнього рівня

```json
"ui": {
  "page": "Термостат",              // Заголовок сторінки у навігації
  "icon": "thermometer",            // Ім'я іконки (lucide / heroicons)
  "page_id": "thermostat",          // Опційний URL-slug. За замовчуванням з імені сторінки.
  "access_level": "user",           // "user" / "service" / "admin" — обмежує видимість
  "cards": [...]                    // Масив визначень карток
}
```

### Картки

Картка — це згрупований контейнер віджетів, що рендериться як одна панель
на сторінці.

```json
{
  "title": "Стан",                  // Заголовок картки
  "subtitle": "Температура, режим", // Опційний підзаголовок
  "layout": "single",               // "single" / "grid" / "split"
  "visible_when": {                 // Опційно — показувати лише коли ключ стану відповідає
    "scenario.engine_active_count": {">": 0}
  },
  "widgets": [...]
}
```

### Віджети

Найпростіша форма віджета:

```json
{"key": "simple_thermo.temperature", "widget": "value"}
```

Поширені типи віджетів:

| Значення `widget` | Що рендерить | Найкраще для |
|---|---|---|
| `"value"` | Відображення числа/рядка лише для читання з одиницею | Показання сенсорів, обчислений стан. |
| `"indicator"` | Кружок у стилі LED on/off | Бінарні виходи, стан тривоги. |
| `"slider"` | Повзунок діапазону з min/max/step | Уставки, значення коефіцієнтів. |
| `"number_input"` | Числовий ввід зі стрілками | Точні значення, великі діапазони. |
| `"select"` | Випадаючий список зі значеннями enum | Вибір режиму (HEATING/COOLING). |
| `"toggle"` | Перемикач для bool | Прапорці конфігурації on/off. |
| `"chart"` | Часовий ряд, що тягне дані з datalogger | Історія температури, лінії тренду. |

Форма для драйверів (зверніть увагу, `setting` замість `key`):

```json
{
  "title": "DS18B20: {{hardware_id}}",
  "instance_per_binding": true,     // Рендерити одну картку на прив'язаний екземпляр
  "widgets": [
    {"setting": "read_interval_ms", "widget": "number_input"},
    {"setting": "offset",           "widget": "slider"},
    {"setting": "resolution",       "widget": "select"}
  ]
}
```

## Секція: `mqtt` (сервісні модулі)

Перелічує, які ключі стану публікуються у MQTT, і які приймають запис
з MQTT. Генератор виробляє рядки тем у форматі
`<base>/<module>/<key_name>`, де `<base>` сконфігурований у `sdkconfig`
(за замовчуванням `modesp/<device-id>`).

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
- Ключі у `subscribe` мають мати `access: "readwrite"` і
  `mqtt_subscribe: true` у своїй декларації `state`. Генератор валідує
  це під час збірки.
- Опубліковані значення отримують придушену дельта-публікацію (Stage 1.5
  документуватиме частоти та обробку LWT у [mqtt.md](mqtt.md)
  *(заплановано)*).

## Секція: `display` (сервісні модулі, опційно)

Описує, що модуль показує на локальному дисплеї пристрою (OLED/LCD).
Генератор збирає секції всіх модулів в одне дерево меню у
`generated/display_screens.h`; модуль `display` рендерить його та
обробляє навігацію кнопками. Дивіться
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

| Поле | Опис |
|---|---|
| `main_value` | Значення для головного екрана: `key` + printf-`format`. |
| `menu_label` | Назва підменю модуля. Якщо нема — береться `ui.page`, потім ім'я модуля. |
| `menu_items[]` | Пункти підменю: обов'язкові `label` і `key`, опційний `format` (printf). |

**Правила:**
- Усі `key` мають існувати в секції `state` цього модуля — генератор
  валідує під час збірки.
- Редагованість виводиться зі `state` автоматично: `access: "readwrite"`
  робить пункт редагованим з кроком/межами з `min`/`max`/`step`;
  `options` дає вибір зі списку; `bool` — перемикач з
  `on_label`/`off_label`. Ключі з `access: "read"` показуються як
  read-only значення.
- `readwrite` string без `options` на дисплеї показується read-only
  (текст не редагується кнопками) — генератор попередить.

## Секція: `loggable` (сервісні модулі)

Прив'язує ключі стану до модуля DataLogger — канали для часових рядів,
події для логування фронтів.

```json
"loggable": {
  "channels": {
    "simple_thermo.temperature": {
      "type": "temperature",
      "label": "Температура",
      "default": true              // Увімкнено за замовчуванням
    }
  },
  "events": {
    "simple_thermo.output": {
      "id": 30,                    // Стабільний ID події — призначити раз, ніколи не змінювати
      "edge": "both",              // "rising" / "falling" / "both"
      "label_on": "Нагрів ON",
      "label_off": "Нагрів OFF"
    }
  }
}
```

> ⚠️ **Попередження:** `id` події — це стабільний побайтовий ідентифікатор,
> збережений у flash-файлах datalogger. **Ніколи не використовуйте
> повторно і не перенумеровуйте** наявні ID — інтерпретація історичних
> даних зламається. Підбирайте невикористані ID, переглядаючи маніфести
> всіх модулів на поточні значення.

## Секція: `features` (сервісні модулі, опційно)

Декларує прапорці можливостей часу компіляції. Генератор виробляє
константи у `features_config.h`, які ваш C++-код та інші генератори
читають під час компіляції.

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

Використовуйте помірковано. Більшість «конфігурації» повинна жити в
`state` з `persist: true` (можна змінити в runtime, без перезбірки).
Прапорці можливостей — для виборів виключно часу компіляції: оптимізації
розміру flash, взаємовиключні гілки коду, debug-обмеження.

## Секція: `scenario` (лише модулі-рецепти)

Це DSL рецептів — граф фаз і переходів, скомпільований у бінарний `.modr`
через `tools/compile_scenario.py`. Рушій виконує отриманий blob у runtime.

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
        // ... ще фази
      ]
    }
  ]
}
```

Повний довідник: [recipe-authoring.md](recipe-authoring.md) *(заплановано)*
та наявна
[документація scenario-engine](../03-framework-reference/scenario-engine/).

## Секції лише для драйверів

### `settings`

Схема збереженої конфігурації для екземпляра драйвера (одне визначення
драйвера може мати багато прив'язаних екземплярів — різні сенсори
одного типу).

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

Поля дзеркалять поля `state` на кожен ключ. Налаштування зберігаються у
NVS під `drv.<driver>.<instance>.<key>`.

### `provides`

Що драйвер виробляє (сенсори) або приймає (актуатори).

```json
// Драйвер сенсора:
"provides": {"type": "float", "unit": "°C", "range": [-55, 125]}

// Драйвер актуатора:
"provides": {"type": "bool", "description": "Стан реле"}
```

### `requires_address`

Прив'язка екземпляра драйвера потребує апаратної адресації (OneWire ROM,
I2C-адреса тощо). Коли `true`, `bindings.json` має надати поле `address`
для кожного екземпляра.

```json
"requires_address": true,
"multiple_per_bus": true            // > 1 екземпляр може ділити шину
```

### `discovery`

Опційний ендпоінт сканера — кнопка UI, що зондує апаратну шину і
повертає виявлені пристрої.

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

## Валідація та інтеграція зі збіркою

`tools/generate_ui.py` запускається як CMake-гак перед збіркою. Він:

1. Виявляє кожен `manifest.json` під `modules/` і `drivers/`.
2. Валідує кожен проти **JSON Schema** у `tools/schemas/`
   (`module`, `driver`, `board`, `bindings`, `project` — draft-07,
   `additionalProperties:false`). Опечатка в імені поля (`persits`),
   неправильний тип (`"priority": "high"`) чи невідомий widget ламають
   білд чіткою `schema:`-помилкою, а не тихо ігноруються. Ключі з
   підкресленням (`_note`, `_config_note`) завжди дозволені як вільні
   нотатки. Цей документ — людиночитна форма схеми.
3. Перехресно валідує посилання: кожен ключ у `mqtt.subscribe` має
   існувати у `state`; кожен `setting.key` має бути унікальним у межах
   драйвера; кожен дзеркальний ключ `scenario` рецепта має бути
   попередньо задекларований у секції `state` рецепта.
4. Виробляє артефакти до `generated/` і `data/` (див.
   [tools/generate_ui.md](../05-tools/generate_ui.md) *(заплановано)*).

Збій на будь-якому кроці перериває збірку з конкретним повідомленням —
невалідні маніфести ніколи не доходять до runtime.

## Типові помилки

**Забутий `priority`:** сервісний модуль без `priority` за замовчуванням
потрапляє до фази NORMAL (priority `2`) — зазвичай нормально. Критичним
модулям (watchdog, служба помилок) потрібно `priority: 0`; HAL і
драйверам — `priority: 1`. Пізні фази ініціалізації (HTTP, WS) отримують
`priority: 3`.

**Довжина ключа стану > 32 символів:** SharedState накладає обмеження у
32 символи. Бюджет `<module>.<key>`: ім'я модуля ≤ 12 символів + крапка
+ ключ ≤ 19 символів. Автори рецептів натикаються на це з довгими
іменами фаз — бюджет імені рецепта скорочується до ≤ 8.

**Забутий `mqtt_subscribe: true` для ключів запису:** ключ із
`access: "readwrite"`, перелічений у `mqtt.subscribe`, але без прапорця —
генератор відхиляє його з чітким повідомленням.

**`features` плутають зі `state`:** features — це час компіляції, state —
runtime. Якщо користувачі коригуватимуть значення через WebUI,
використовуйте `state`. Якщо вибір обмежує цілі блоки коду,
використовуйте `features`.

**Рецепт із незадекларованими дзеркальними ключами:** сценарії пишуть
дзеркальні ключі (`<recipe>.scenario_state` тощо) — їх ПОТРІБНО
попередньо задекларувати у секції `state` маніфесту рецепта, хоча їх
пише рушій, а не рецепт. Компілятор перехресно перевіряє це і інакше
завалює збірку.

## Що далі

- **[writing-a-module.md](writing-a-module.md)** *(заплановано)* —
  перетворити маніфест у робочий C++-модуль.
- **[writing-a-driver.md](writing-a-driver.md)** *(заплановано)* —
  написання драйвера з інтерфейсом IDriver.
- **[recipe-authoring.md](recipe-authoring.md)** *(заплановано)* —
  глибоке занурення у секцію `scenario`.
- **[ui-widgets.md](ui-widgets.md)** *(заплановано)* — усі типи віджетів
  з прикладами рендерингу.

## Приклади для вивчення

Наявні маніфести варто прочитати з джерельного коду:

- [`modules/simple_thermo/manifest.json`](../../../modules/simple_thermo/manifest.json) —
  мінімальний сервісний модуль зі state, mqtt, loggable, ui. ~100 рядків.
- [`modules/abs_test/manifest.json`](../../../modules/abs_test/manifest.json) —
  модуль-рецепт із 2-доріжковою секцією `scenario`.
- [`drivers/ds18b20/manifest.json`](../../../drivers/ds18b20/manifest.json) —
  драйвер із settings, provides, discovery.
- [`modules/datalogger/manifest.json`](../../../modules/datalogger/manifest.json) —
  більший сервісний модуль з каналами та можливостями.
- [`modules/equipment/manifest.json`](../../../modules/equipment/manifest.json) —
  секція `requires` з ролями як capability (temperature, angle, relay_out).
