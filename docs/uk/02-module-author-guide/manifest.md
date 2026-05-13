# ModESP v4 — Стандарт маніфестів (Manifest Standard)

> Версія стандарту: 2.0
> Дата: 2026-03-01
>
> Маніфести — ЄДИНЕ ДЖЕРЕЛО ПРАВДИ про компоненти системи.
> Все генерується з них: UI, C++ headers, MQTT topics, display screens, feature flags.
> Якщо чогось немає в маніфесті — цього не існує для системи.

---

## Огляд системи маніфестів

### Чотири типи маніфестів

| # | Тип | Файл | Хто створює | Навіщо |
|---|-----|------|-------------|--------|
| 1 | **Board** | `data/board.json` | Розробник PCB | Що є на платі (GPIO, шини, I2C, expanders) |
| 2 | **Driver** | `drivers/<name>/manifest.json` | Розробник драйвера | Що драйвер вміє, що потребує |
| 3 | **Module** | `modules/<name>/manifest.json` | Розробник модуля | Бізнес-логіка, UI, MQTT, features, constraints |
| 4 | **Bindings** | `data/bindings.json` | Інтегратор/оператор | Хто з ким з'єднаний |

Додатково є **Project manifest** (`project.json`) — перелік активних модулів та системні налаштування.

### Як вони пов'язані

```
Board manifest            Driver manifest           Module manifest
"що є на платі"           "що драйвер вміє"         "що модуль хоче"

 expander_outputs:         driver: pcf8574_relay      requires:
   relay_1 (exp pin 0) ←── hardware_type:      ←───   role: compressor
   relay_2 (exp pin 1)      i2c_expander_output        type: actuator
                             category: actuator         drivers: [relay, pcf8574_relay]

 onewire_buses:            driver: ds18b20            requires:
   ow_1 (GPIO32)      ←── hardware_type:       ←───   role: air_temp
                            onewire_bus                 type: sensor
                            category: sensor            drivers: [ds18b20, ntc]
                   ^                             ^
                   └────── Bindings manifest ────┘
                           "хто з ким з'єднаний"
                           module: ЗАВЖДИ "equipment"
```

### Генерація артефактів

```
Module manifests ──┐
Driver manifests ──┼─→ tools/generate_ui.py ─→ 5 артефактів
Board manifest   ──┤                             │
Bindings manifest ─┘                             ├─ data/ui.json (UI schema)
                                                 ├─ generated/state_meta.h (constexpr metadata)
                                                 ├─ generated/mqtt_topics.h (MQTT topic arrays)
                                                 ├─ generated/display_screens.h (LCD/OLED)
                                                 └─ generated/features_config.h (feature flags)
```

Генератор автоматично запускається при `idf.py build` (через CMakeLists.txt).

**НЕ РЕДАГУЙ згенеровані файли вручну** — вони перезаписуються при кожному build.

---

## 1. Board Manifest (`data/board.json`)

Board manifest описує фізичну плату: які GPIO, шини, I2C expanders доступні.

### 1.1 Обов'язкові поля

| Поле | Тип | Опис |
|------|-----|------|
| `manifest_version` | int | Версія формату (завжди `1`) |
| `board` | string | Ідентифікатор плати (наприклад `"kc868_a6"`) |
| `version` | string | Версія PCB |
| `description` | string | Опис плати |

### 1.2 Секції hardware

#### `i2c_buses` — I2C шини

```json
"i2c_buses": [
  {"id": "i2c_0", "sda": 4, "scl": 15, "freq_hz": 100000, "label": "I2C шина"}
]
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `id` | string | так | Унікальний ID шини |
| `sda` | int | так | GPIO для SDA |
| `scl` | int | так | GPIO для SCL |
| `freq_hz` | int | так | Частота шини (100000 або 400000) |
| `label` | string | ні | Людино-читабельна назва |

#### `i2c_expanders` — I2C розширювачі (PCF8574)

```json
"i2c_expanders": [
  {"id": "relay_exp", "bus": "i2c_0", "chip": "pcf8574", "address": "0x24", "pins": 8, "label": "Реле PCF8574"},
  {"id": "input_exp", "bus": "i2c_0", "chip": "pcf8574", "address": "0x22", "pins": 8, "label": "Входи PCF8574"}
]
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `id` | string | так | Унікальний ID розширювача |
| `bus` | string | так | Посилання на `i2c_buses[].id` |
| `chip` | string | так | Тип мікросхеми (`"pcf8574"`) |
| `address` | string | так | I2C адреса у форматі `"0xNN"` |
| `pins` | int | так | Кількість пінів (8 для PCF8574) |
| `label` | string | ні | Людино-читабельна назва |

#### `expander_outputs` — Виходи через I2C expander

```json
"expander_outputs": [
  {"id": "relay_1", "expander": "relay_exp", "pin": 0, "active_high": false, "label": "Реле 1"},
  {"id": "relay_2", "expander": "relay_exp", "pin": 1, "active_high": false, "label": "Реле 2"}
]
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `id` | string | так | Унікальний ID виходу |
| `expander` | string | так | Посилання на `i2c_expanders[].id` |
| `pin` | int | так | Номер піна на розширювачі (0-7) |
| `active_high` | bool | так | Логіка активації (false = active low) |
| `label` | string | ні | Людино-читабельна назва |

**hardware_type:** `i2c_expander_output`

#### `expander_inputs` — Входи через I2C expander

```json
"expander_inputs": [
  {"id": "din_1", "expander": "input_exp", "pin": 0, "invert": true, "label": "Вхід 1"}
]
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `id` | string | так | Унікальний ID входу |
| `expander` | string | так | Посилання на `i2c_expanders[].id` |
| `pin` | int | так | Номер піна на розширювачі (0-7) |
| `invert` | bool | так | Інверсія логіки |
| `label` | string | ні | Людино-читабельна назва |

**hardware_type:** `i2c_expander_input`

#### `gpio_outputs` — Прямі GPIO виходи (реле)

```json
"gpio_outputs": [
  {"id": "relay_1", "gpio": 14, "active_high": true, "label": "Реле 1 (GPIO14)"}
]
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `id` | string | так | Унікальний ID |
| `gpio` | int | так | Номер GPIO |
| `active_high` | bool | так | Логіка активації |
| `label` | string | ні | Назва |

**hardware_type:** `gpio_output`

#### `gpio_inputs` — Прямі GPIO входи

```json
"gpio_inputs": [
  {"id": "door_1", "gpio": 26, "pull_up": true, "label": "Вхід дверей"}
]
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `id` | string | так | Унікальний ID |
| `gpio` | int | так | Номер GPIO |
| `pull_up` | bool | ні | Ввімкнути внутрішній pull-up |
| `label` | string | ні | Назва |

**hardware_type:** `gpio_input`

#### `onewire_buses` — OneWire шини (DS18B20)

```json
"onewire_buses": [
  {"id": "ow_1", "gpio": 32, "label": "OneWire шина 1"},
  {"id": "ow_2", "gpio": 33, "label": "OneWire шина 2"}
]
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `id` | string | так | Унікальний ID шини |
| `gpio` | int | так | GPIO пін |
| `label` | string | ні | Назва |

**hardware_type:** `onewire_bus`

#### `adc_channels` — ADC канали (NTC термістори)

```json
"adc_channels": [
  {"id": "adc_1", "gpio": 36, "atten": 3, "label": "ADC 1 (0-3.3V)"}
]
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `id` | string | так | Унікальний ID каналу |
| `gpio` | int | так | GPIO пін (тільки ADC1: 32-39) |
| `atten` | int | так | Attenuator: 0=0dB, 1=2.5dB, 2=6dB, 3=12dB (0-3.3V) |
| `label` | string | ні | Назва |

**hardware_type:** `adc_channel`

### 1.3 Повний приклад board.json (KC868-A6)

```json
{
  "manifest_version": 1,
  "board": "kc868_a6",
  "version": "1.0.0",
  "description": "Kincony KC868-A6 — 6 реле (PCF8574), 6 входів (PCF8574), I2C",
  "i2c_buses": [
    {"id": "i2c_0", "sda": 4, "scl": 15, "freq_hz": 100000, "label": "I2C шина"}
  ],
  "i2c_expanders": [
    {"id": "relay_exp",  "bus": "i2c_0", "chip": "pcf8574", "address": "0x24", "pins": 8, "label": "Реле PCF8574"},
    {"id": "input_exp",  "bus": "i2c_0", "chip": "pcf8574", "address": "0x22", "pins": 8, "label": "Входи PCF8574"}
  ],
  "expander_outputs": [
    {"id": "relay_1", "expander": "relay_exp", "pin": 0, "active_high": false, "label": "Реле 1"},
    {"id": "relay_2", "expander": "relay_exp", "pin": 1, "active_high": false, "label": "Реле 2"},
    {"id": "relay_3", "expander": "relay_exp", "pin": 2, "active_high": false, "label": "Реле 3"},
    {"id": "relay_4", "expander": "relay_exp", "pin": 3, "active_high": false, "label": "Реле 4"},
    {"id": "relay_5", "expander": "relay_exp", "pin": 4, "active_high": false, "label": "Реле 5"},
    {"id": "relay_6", "expander": "relay_exp", "pin": 5, "active_high": false, "label": "Реле 6"}
  ],
  "expander_inputs": [
    {"id": "din_1", "expander": "input_exp", "pin": 0, "invert": true, "label": "Вхід 1"},
    {"id": "din_2", "expander": "input_exp", "pin": 1, "invert": true, "label": "Вхід 2"},
    {"id": "din_3", "expander": "input_exp", "pin": 2, "invert": true, "label": "Вхід 3"},
    {"id": "din_4", "expander": "input_exp", "pin": 3, "invert": true, "label": "Вхід 4"},
    {"id": "din_5", "expander": "input_exp", "pin": 4, "invert": true, "label": "Вхід 5"},
    {"id": "din_6", "expander": "input_exp", "pin": 5, "invert": true, "label": "Вхід 6"}
  ],
  "onewire_buses": [
    {"id": "ow_1", "gpio": 32, "label": "OneWire шина 1"},
    {"id": "ow_2", "gpio": 33, "label": "OneWire шина 2"}
  ],
  "adc_channels": [
    {"id": "adc_1", "gpio": 36, "atten": 3, "label": "ADC 1 (0-3.3V)"},
    {"id": "adc_2", "gpio": 39, "atten": 3, "label": "ADC 2 (0-3.3V)"},
    {"id": "adc_3", "gpio": 34, "atten": 3, "label": "ADC 3 (0-3.3V)"},
    {"id": "adc_4", "gpio": 35, "atten": 3, "label": "ADC 4 (0-3.3V)"}
  ]
}
```

### 1.4 Зв'язок hardware_type

| Секція board.json | hardware_type | Сумісні драйвери |
|-------------------|---------------|------------------|
| `gpio_outputs` | `gpio_output` | relay |
| `gpio_inputs` | `gpio_input` | digital_input |
| `onewire_buses` | `onewire_bus` | ds18b20 |
| `adc_channels` | `adc_channel` | ntc |
| `expander_outputs` | `i2c_expander_output` | pcf8574_relay |
| `expander_inputs` | `i2c_expander_input` | pcf8574_input |

---

## 2. Driver Manifest (`drivers/<name>/manifest.json`)

Driver manifest описує один апаратний драйвер: що він вміє, які ресурси потребує.

### 2.1 Обов'язкові поля

| Поле | Тип | Опис |
|------|-----|------|
| `manifest_version` | int | Версія формату (завжди `1`) |
| `driver` | string | Унікальне ім'я драйвера |
| `description` | string | Опис драйвера |
| `category` | enum | `"sensor"` або `"actuator"` |
| `hardware_type` | string | Тип апаратного ресурсу (з board.json) |
| `requires_address` | bool | Чи потрібна адреса (ROM для DS18B20) |
| `multiple_per_bus` | bool | Чи кілька пристроїв на одній шині |
| `provides` | object | Що надає драйвер |
| `settings` | array | Налаштування драйвера |

### 2.2 Поле `provides`

Для сенсорів:
```json
"provides": {"type": "float", "unit": "°C", "range": [-55, 125]}
```

Для актуаторів:
```json
"provides": {"type": "bool"}
```

### 2.3 Поле `settings`

Кожен елемент масиву `settings`:

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `key` | string | так | Ключ налаштування |
| `type` | string | так | `"int"`, `"float"`, `"bool"` |
| `default` | varies | так | Значення за замовчуванням |
| `min` | number | для int/float | Мінімум |
| `max` | number | для int/float | Максимум |
| `step` | number | для int/float | Крок |
| `unit` | string | ні | Одиниця вимірювання |
| `description` | string | так | Опис |
| `persist` | bool | ні | Зберігати в NVS |

### 2.4 Зареєстровані драйвери (6 штук)

#### `ds18b20` — Dallas DS18B20 цифровий датчик температури

```json
{
  "manifest_version": 1,
  "driver": "ds18b20",
  "description": "Dallas DS18B20 цифровий датчик температури",
  "category": "sensor",
  "hardware_type": "onewire_bus",
  "requires_address": true,
  "multiple_per_bus": true,
  "provides": {"type": "float", "unit": "°C", "range": [-55, 125]},
  "settings": [
    {"key": "read_interval_ms", "type": "int",   "default": 1000, "min": 500, "max": 60000, "step": 100, "unit": "мс", "description": "Інтервал опитування", "persist": true},
    {"key": "offset",           "type": "float", "default": 0.0,  "min": -5.0, "max": 5.0, "step": 0.1, "unit": "°C", "description": "Корекція показань", "persist": true},
    {"key": "resolution",       "type": "int",   "default": 12,   "min": 9,    "max": 12,  "step": 1,   "unit": "біт", "description": "Роздільна здатність", "persist": true}
  ]
}
```

Особливості:
- MATCH_ROM для мульти-сенсорів на одній шині
- SEARCH_ROM scan через GET `/api/onewire/scan`
- CRC8 валідація ROM адрес
- `requires_address: true` — кожен binding потребує поле `address`

#### `relay` — GPIO реле

```json
{
  "manifest_version": 1,
  "driver": "relay",
  "description": "GPIO реле (on/off)",
  "category": "actuator",
  "hardware_type": "gpio_output",
  "requires_address": false,
  "multiple_per_bus": false,
  "provides": {"type": "bool"},
  "settings": []
}
```

#### `ntc` — NTC термістор через ADC

```json
{
  "manifest_version": 1,
  "driver": "ntc",
  "description": "NTC термістор через ADC (B-parameter equation)",
  "category": "sensor",
  "hardware_type": "adc_channel",
  "requires_address": false,
  "multiple_per_bus": false,
  "provides": {"type": "float", "unit": "°C", "range": [-40, 125]},
  "settings": [
    {"key": "beta",             "type": "int",   "default": 3950,  "min": 2000, "max": 5000, "step": 1,   "description": "B-coefficient NTC"},
    {"key": "r_series",         "type": "int",   "default": 10000, "min": 1000, "max": 100000, "step": 100, "unit": "Ом", "description": "Послідовний резистор"},
    {"key": "r_nominal",        "type": "int",   "default": 10000, "min": 1000, "max": 100000, "step": 100, "unit": "Ом", "description": "Номінальний опір NTC при 25°C"},
    {"key": "read_interval_ms", "type": "int",   "default": 1000,  "min": 100,  "max": 60000,  "step": 100, "unit": "мс", "description": "Інтервал опитування"},
    {"key": "offset",           "type": "float", "default": 0.0,   "min": -5.0, "max": 5.0,    "step": 0.1, "unit": "°C", "description": "Корекція показань"}
  ]
}
```

#### `digital_input` — GPIO дискретний вхід

```json
{
  "manifest_version": 1,
  "driver": "digital_input",
  "description": "GPIO дискретний вхід (контакт дверей, кінцевик)",
  "category": "sensor",
  "hardware_type": "gpio_input",
  "requires_address": false,
  "multiple_per_bus": false,
  "provides": {"type": "bool"},
  "settings": [
    {"key": "invert", "type": "bool", "default": false, "description": "Інверсія логіки (NC/NO)", "persist": true}
  ]
}
```

Особливості: 50ms debounce для стабільності.

#### `pcf8574_relay` — Реле через I2C PCF8574

```json
{
  "manifest_version": 1,
  "driver": "pcf8574_relay",
  "description": "Relay via I2C expander PCF8574",
  "category": "actuator",
  "hardware_type": "i2c_expander_output",
  "requires_address": false,
  "multiple_per_bus": true,
  "provides": {"type": "bool"},
  "settings": []
}
```

#### `pcf8574_input` — Вхід через I2C PCF8574

```json
{
  "manifest_version": 1,
  "driver": "pcf8574_input",
  "description": "Digital input via I2C expander PCF8574",
  "category": "sensor",
  "hardware_type": "i2c_expander_input",
  "requires_address": false,
  "multiple_per_bus": true,
  "provides": {"type": "bool"},
  "settings": [
    {"key": "invert", "type": "bool", "default": false, "description": "Invert logic"}
  ]
}
```

### 2.5 Driver UI (PLANNED)

> **Статус:** ЗАПЛАНОВАНО. В поточній версії driver UI (секція `ui` в ds18b20/manifest.json)
> визначена в маніфесті, але ще не генерується в data/ui.json.
> Коли буде реалізовано — drivers зможуть мати власну сторінку налаштувань
> (`page_id: "driver_settings"`) з instance_per_binding картками.

### 2.6 Discovery/Scan (PLANNED)

> **Статус:** ЧАСТКОВО РЕАЛІЗОВАНО. DS18B20 має `discovery` секцію в маніфесті,
> і GET `/api/onewire/scan` працює (SEARCH_ROM + температура).
> WebUI BindingsEditor використовує цей endpoint для OneWire Discovery.
> Повна система автоматичного discovery з маніфесту ще не реалізована.

---

## 3. Module Manifest (`modules/<name>/manifest.json`)

Module manifest — найбагатший тип маніфесту. Описує бізнес-логіку, UI, MQTT, features, constraints.

### 3.1 Обов'язкові поля верхнього рівня

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `manifest_version` | int | так | Завжди `1` |
| `module` | string | так | Унікальне ім'я модуля |
| `description` | string | так | Опис модуля |
| `state` | object | так | State keys (центральне сховище) |
| `requires` | array | лише equipment | Hardware roles (тільки для Equipment Manager) |
| `inputs` | object | ні | Ключі з інших модулів, які цей модуль читає |
| `features` | object | ні | Feature flags (Phase 10.5) |
| `constraints` | object | ні | Enum filtering по features |
| `ui` | object | ні | UI schema (page, cards, widgets) |
| `mqtt` | object | ні | MQTT publish/subscribe |
| `display` | object | ні | LCD/OLED display data |
| `dependencies` | object | ні | Зарезервовано (не використовується) |

### 3.1.1 Поле `requires` (тільки Equipment Manager)

**ВАЖЛИВО:** Тільки модуль `equipment` має секцію `requires`. Всі інші бізнес-модулі
працюють через SharedState (читають `equipment.*` ключі, публікують `<module>.req.*` запити).

```json
"requires": [
  {"role": "air_temp",       "type": "sensor",   "driver": ["ds18b20", "ntc"], "label": "Темп. камери"},
  {"role": "compressor",     "type": "actuator", "driver": ["relay", "pcf8574_relay"], "label": "Компресор"},
  {"role": "evap_temp",      "type": "sensor",   "driver": ["ds18b20", "ntc"], "label": "Темп. випарника",      "optional": true},
  {"role": "condenser_temp", "type": "sensor",   "driver": ["ds18b20", "ntc"], "label": "Темп. конденсатора",   "optional": true},
  {"role": "defrost_relay",  "type": "actuator", "driver": ["relay", "pcf8574_relay"], "label": "Реле відтайки",        "optional": true},
  {"role": "evap_fan",       "type": "actuator", "driver": ["relay", "pcf8574_relay"], "label": "Вент. випарника",      "optional": true},
  {"role": "cond_fan",       "type": "actuator", "driver": ["relay", "pcf8574_relay"], "label": "Вент. конденсатора",   "optional": true},
  {"role": "door_contact",   "type": "sensor",   "driver": ["digital_input", "pcf8574_input"], "label": "Контакт дверей",       "optional": true},
  {"role": "night_input",    "type": "sensor",   "driver": ["digital_input", "pcf8574_input"], "label": "Вхід нічного режиму",  "optional": true}
]
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `role` | string | так | Функціональна роль (наприклад `"compressor"`) |
| `type` | enum | так | `"sensor"` або `"actuator"` |
| `driver` | array | так | Список сумісних драйверів |
| `label` | string | ні | Людино-читабельна назва |
| `optional` | bool | ні | Чи обов'язковий (default: false) |

**Поточні ролі:**

| Роль | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `air_temp` | sensor | так | Температура камери (основний датчик) |
| `compressor` | actuator | так | Компресор |
| `evap_temp` | sensor | ні | Температура випарника |
| `condenser_temp` | sensor | ні | Температура конденсатора |
| `defrost_relay` | actuator | ні | Єдине реле відтайки (тен або клапан ГГ) |
| `evap_fan` | actuator | ні | Вентилятор випарника |
| `cond_fan` | actuator | ні | Вентилятор конденсатора |
| `door_contact` | sensor | ні | Контакт дверей |
| `night_input` | sensor | ні | Дискретний вхід нічного режиму |

> **Role Context (PLANNED):** у майбутньому roles можуть мати контекстну інформацію
> (наприклад min_switch_ms для конкретної ролі). Зараз це керується в C++ коді.

### 3.2 Секція `state` — State keys

State keys — основний механізм обміну даними між модулями через SharedState.

#### Формат

```json
"state": {
  "module.key_name": {
    "type": "float|int|bool|string",
    "access": "read|readwrite",
    "unit": "°C",
    "min": -50.0,
    "max": 50.0,
    "step": 0.5,
    "default": 4.0,
    "persist": true,
    "description": "Опис ключа",
    "enum": ["idle", "cooling", "safety_run"],
    "options": [
      {"value": 0, "label": "Постійно"},
      {"value": 1, "label": "З компресором"}
    ]
  }
}
```

#### Поля state key

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `type` | enum | так | `"float"`, `"int"`, `"bool"`, `"string"` |
| `access` | enum | так | `"read"` або `"readwrite"` |
| `unit` | string | ні | Одиниця вимірювання (`"°C"`, `"хв"`, `"с"`, `"год"`) |
| `min` | number | * | Мінімальне значення |
| `max` | number | * | Максимальне значення |
| `step` | number | * | Крок зміни |
| `default` | varies | ** | Значення за замовчуванням |
| `persist` | bool | ні | Зберігати в NVS (через PersistService) |
| `description` | string | так | Опис ключа (українською) |
| `enum` | array | ні | Допустимі значення для read-only string keys |
| `options` | array | ні | Варіанти для select widget (замість min/max/step) |

**\* `min`, `max`, `step`** — обов'язкові для `readwrite` ключів типу `float` або `int`,
ЯКЩО немає поля `options`. Якщо є `options` — min/max/step НЕ потрібні.

**\*\* `default`** — обов'язково для `readwrite` ключів з `persist: true`.

#### Правило іменування

State keys мають формат `<module>.<key>`:
- `thermostat.setpoint`
- `equipment.air_temp`
- `defrost.req.compressor`

Вінятки: модуль може читати ключі інших модулів через `inputs` (наприклад thermostat
читає `equipment.air_temp`, `protection.lockout`).

#### Поле `options` (select widgets)

Коли state key має поле `options` — це enum-type setting, який в UI рендериться як `select` widget.

```json
"thermostat.evap_fan_mode": {
  "type": "int",
  "access": "readwrite",
  "default": 1,
  "persist": true,
  "options": [
    {"value": 0, "label": "Постійно"},
    {"value": 1, "label": "З компресором"},
    {"value": 2, "label": "За температурою випарника"}
  ],
  "description": "Режим вент. випарника"
}
```

**Правила:**
- `options[].value` ЗАВЖДИ int (V17 validation)
- `options[].label` — людино-читабельна назва
- Якщо state key має `options` — UI widget ПОВИНЕН бути `"select"` (V18 validation)
- `min`/`max`/`step` НЕ вказуються для keys з `options`

### 3.3 Секція `ui` — UI schema

UI секція визначає як модуль виглядає у WebUI.

#### Структура

```json
"ui": {
  "page": "Холодильна камера",
  "page_id": "thermostat",
  "icon": "snowflake",
  "order": 1,
  "cards": [...]
}
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `page` | string | так | Назва сторінки (для навігації) |
| `page_id` | string | так | Унікальний ID сторінки |
| `icon` | string | так | Іконка Lucide (snowflake, flame, activity, ...) |
| `order` | int | так | Порядок в навігації |
| `cards` | array | так | Масив карток |

#### Cards

```json
{
  "title": "Гарячий газ",
  "group": "settings",
  "collapsible": true,
  "visible_when": {"key": "defrost.type", "eq": 2},
  "widgets": [...]
}
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `title` | string | так | Назва картки |
| `group` | string | ні | Група (`"settings"` для згортання) |
| `collapsible` | bool | ні | Чи можна згортати |
| `visible_when` | object | ні | Умова видимості (див. нижче) |
| `widgets` | array | так | Масив віджетів |

#### Widgets

```json
{
  "key": "thermostat.fan_stop_temp",
  "widget": "number_input",
  "label": "T зупинки вентилятора",
  "visible_when": {"key": "thermostat.evap_fan_mode", "eq": 2}
}
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `key` | string | так | State key |
| `widget` | string | так | Тип віджету (див. таблицю нижче) |
| `label` | string | ні | Override назви (інакше з description) |
| `visible_when` | object | ні | Умова видимості |
| `format` | string | ні | Формат відображення (`"duration"`) |
| `on_label` / `off_label` | string | ні | Тексти для indicator |
| `on_color` / `off_color` | string | ні | Кольори для indicator |
| `data_source` | string | ні | URL для chart widget |
| `default_hours` | int | ні | Початковий діапазон для chart |

#### `visible_when` — Умовна видимість

Картки та віджети можуть ховатися/показуватися на основі значень state keys.

**Формати:**

```json
{"key": "defrost.type", "eq": 2}
```
Видимий коли `defrost.type == 2`

```json
{"key": "thermostat.evap_fan_mode", "neq": 0}
```
Видимий коли `thermostat.evap_fan_mode != 0`

```json
{"key": "thermostat.night_mode", "in": [1, 2, 3]}
```
Видимий коли `thermostat.night_mode` дорівнює 1, 2 або 3

**Правила:**
- `key` — обов'язковий, string, посилається на state key
- Рівно ОДИН оператор: `eq`, `neq` або `in`
- `in` — масив допустимих значень
- Валідується V19
- WebUI: `isVisible(vw, $state)` утиліта з `webui/src/lib/visibility.js`

#### Таблиця типів віджетів (21 штука)

| Widget | Тип state | Опис | Особливості |
|--------|-----------|------|-------------|
| `value` | float, int, bool, string | Відображення значення | format="duration" для секунд |
| `slider` | float, int | Повзунок | Потребує min/max/step |
| `number_input` | float, int | Числове поле | Потребує min/max/step |
| `indicator` | bool | Індикатор ON/OFF | on_label, off_label, on_color, off_color |
| `status_text` | string | Текстовий статус | - |
| `toggle` | bool | Перемикач ON/OFF | - |
| `select` | int, string | Випадаючий список | Потребує options в state key |
| `text_input` | string | Текстове поле | - |
| `password_input` | string | Поле пароля | Маскує символи |
| `button` | bool | Кнопка (trigger) | Встановлює true при натисканні |
| `chart` | - | Графік температури | data_source, default_hours |
| `firmware_upload` | - | OTA завантаження | POST /api/ota |
| `file_upload` | - | Завантаження файлів | - |
| `wifi_save` | - | Збереження WiFi STA | - |
| `wifi_scan` | - | Сканування WiFi | GET /api/wifi/scan |
| `ap_save` | - | Збереження WiFi AP | - |
| `mqtt_save` | - | Збереження MQTT | - |
| `time_save` | - | Збереження часу | - |
| `datetime_input` | - | Введення дати/часу | - |
| `timezone_select` | - | Вибір часового поясу | - |
| `defrost_toggle` | bool | Кнопка ручної відтайки | Спеціальна логіка start/stop |

### 3.4 Секція `inputs` — Вхідні дані з інших модулів

Описує які state keys модуль читає з інших модулів. Це декларативний опис залежностей.

```json
"inputs": {
  "equipment.air_temp": {
    "source_module": "equipment",
    "type": "float",
    "description": "Температура камери від Equipment Manager",
    "optional": false
  },
  "defrost.active": {
    "source_module": "defrost",
    "type": "bool",
    "description": "Чи активна розморозка",
    "optional": true
  }
}
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `source_module` | string | так | Модуль-джерело |
| `type` | string | так | Очікуваний тип даних |
| `description` | string | так | Опис |
| `optional` | bool | ні | Чи обов'язковий (default: false) |

Валідація: cross-module перевірка (V15) — source_module повинен існувати в project.json,
і відповідний state key повинен бути визначений в source module.

### 3.5 Секція `mqtt` — MQTT topics

```json
"mqtt": {
  "publish": [
    "thermostat.temperature",
    "thermostat.req.compressor",
    "thermostat.state"
  ],
  "subscribe": [
    "thermostat.setpoint",
    "thermostat.differential"
  ]
}
```

- `publish` — state keys, які публікуються в MQTT при зміні
- `subscribe` — state keys, на які модуль підписаний (зміна через MQTT → SharedState)
- Всі ключі ПОВИННІ існувати в `state` секції цього модуля

Topic формат: `modesp/<device_name>/<state_key>` (наприклад `modesp/ModESP/thermostat.setpoint`)

Генерується в `generated/mqtt_topics.h` як constexpr масиви.

### 3.6 Секція `display` — LCD/OLED

```json
"display": {
  "main_value": {
    "key": "thermostat.temperature",
    "format": "%.1f°C"
  },
  "menu_items": [
    {"label": "Уставка",     "key": "thermostat.setpoint"},
    {"label": "Диференціал", "key": "thermostat.differential"}
  ]
}
```

- `main_value` — головне значення для дисплея
- `menu_items` — пункти сервісного меню (кнопки + LCD)

Генерується в `generated/display_screens.h`.

### 3.7 Секція `features` — Feature flags (Phase 10.5)

Feature flags дозволяють модулям декларувати, які функції потребують певного обладнання.
Генератор перевіряє bindings.json під час build і визначає active/inactive features.

#### Структура

```json
"features": {
  "feature_name": {
    "description": "Людино-читабельний опис",
    "requires_roles": ["role_name"],
    "always_active": true,
    "controls_settings": ["module.setting1", "module.setting2"]
  }
}
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `description` | string | так | Опис feature |
| `requires_roles` | array | так | Ролі з equipment.requires, необхідні для активації |
| `always_active` | bool | ні | Якщо true — feature завжди активна (default: false) |
| `controls_settings` | array | так | Які state keys контролює ця feature |

#### Логіка активації

1. **always_active: true** — feature завжди активна, незалежно від bindings
2. **requires_roles: ["role"]** — feature активна ТІЛЬКИ якщо всі вказані ролі
   прив'язані в bindings.json
3. **requires_roles: []** + без `always_active` — активна завжди (пустий масив = немає вимог)

#### Генерація

**FeatureResolver** (Python клас в generate_ui.py) при build:
1. Зчитує bindings.json → визначає які ролі прив'язані
2. Для кожної feature перевіряє requires_roles проти прив'язаних ролей
3. Результат: `{feature_name: bool}` — active чи inactive

**features_config.h** — згенерований C++ header:

```cpp
struct FeatureConfig {
    const char* module;
    const char* feature;
    bool active;
};

static constexpr FeatureConfig FEATURES[] = {
    {"defrost", "defrost_by_sensor", true},
    {"defrost", "defrost_electric", true},
    {"thermostat", "basic_cooling", true},
    {"thermostat", "fan_control", true},
    // ...
};

inline bool is_feature_active(const char* module, const char* feature) { ... }
```

**C++ модулі** використовують `has_feature("name")` (метод BaseModule) для runtime guards:

```cpp
void ThermostatModule::on_update() {
    if (has_feature("fan_control")) {
        // керування вентилятором випарника
    }
}
```

**UI:** якщо feature неактивна → всі widgets з controls_settings показуються як `disabled`
з повідомленням "Потрібно: role_name".

#### Реальні приклади

**Thermostat features:**

```json
"features": {
  "basic_cooling": {
    "description": "Базове охолодження з асиметричним диференціалом",
    "requires_roles": [],
    "always_active": true,
    "controls_settings": [
      "thermostat.setpoint", "thermostat.differential",
      "thermostat.min_off_time", "thermostat.min_on_time",
      "thermostat.startup_delay",
      "thermostat.safety_run_on", "thermostat.safety_run_off"
    ]
  },
  "fan_control": {
    "description": "Керування вентилятором випарника",
    "requires_roles": ["evap_fan"],
    "controls_settings": ["thermostat.evap_fan_mode"]
  },
  "fan_temp_control": {
    "description": "Керування вентилятором за температурою випарника",
    "requires_roles": ["evap_fan", "evap_temp"],
    "controls_settings": ["thermostat.fan_stop_temp", "thermostat.fan_stop_hyst"]
  },
  "condenser_fan": {
    "description": "Керування вентилятором конденсатора",
    "requires_roles": ["cond_fan"],
    "controls_settings": ["thermostat.cond_fan_delay"]
  },
  "night_setback": {
    "description": "Нічний режим зі зміщенням уставки",
    "requires_roles": [],
    "always_active": true,
    "controls_settings": [
      "thermostat.night_setback", "thermostat.night_mode",
      "thermostat.night_start", "thermostat.night_end"
    ]
  },
  "night_di": {
    "description": "Нічний режим через дискретний вхід",
    "requires_roles": ["night_input"],
    "controls_settings": []
  },
  "display_defrost": {
    "description": "Відображення температури під час відтайки",
    "requires_roles": [],
    "always_active": true,
    "controls_settings": ["thermostat.display_defrost"]
  }
}
```

**Defrost features:**

```json
"features": {
  "defrost_timer": {
    "description": "Розморозка зупинкою компресора по таймеру",
    "requires_roles": [],
    "always_active": true,
    "controls_settings": [
      "defrost.interval", "defrost.counter_mode",
      "defrost.max_duration", "defrost.drip_time", "defrost.fan_delay",
      "defrost.initiation", "defrost.termination"
    ]
  },
  "defrost_by_sensor": {
    "description": "Завершення розморозки по датчику випарника",
    "requires_roles": ["evap_temp"],
    "controls_settings": [
      "defrost.end_temp", "defrost.fad_temp", "defrost.demand_temp"
    ]
  },
  "defrost_electric": {
    "description": "Розморозка електричним теном",
    "requires_roles": ["defrost_relay"],
    "controls_settings": []
  },
  "defrost_hot_gas": {
    "description": "Розморозка гарячим газом",
    "requires_roles": ["defrost_relay"],
    "controls_settings": [
      "defrost.stabilize_time", "defrost.valve_delay", "defrost.equalize_time"
    ]
  }
}
```

### 3.8 Секція `constraints` — Enum filtering по features

Constraints дозволяють обмежувати доступність окремих options в select widgets
залежно від наявності обладнання (features).

#### Структура

```json
"constraints": {
  "state.key.with.options": {
    "type": "enum_filter",
    "values": {
      "0": {"always": true},
      "1": {"requires_feature": "feature_name", "disabled_hint": "Потрібен ..."}
    }
  }
}
```

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `type` | enum | так | Тільки `"enum_filter"` |
| `values` | object | так | Обмеження для кожного option value |

Для кожного option value (як string ключ):
- `{"always": true}` — option завжди доступний
- `{"requires_feature": "name", "disabled_hint": "text"}` — потребує feature

#### Ланцюг резолюції

```
constraint.requires_feature → features[name].requires_roles → bindings.json roles
                                                                      ↓
FEATURE_TO_STATE mapping ← feature name → equipment.has_* state key
                                                                      ↓
ui.json option: requires_state → WebUI: $state[requires_state] → enabled/disabled
```

**FEATURE_TO_STATE** маппінг (в generate_ui.py):

| Feature name | State key |
|--------------|-----------|
| `defrost_electric` | `equipment.has_defrost_relay` |
| `defrost_hot_gas` | `equipment.has_defrost_relay` |
| `defrost_by_sensor` | `equipment.has_evap_temp` |
| `fan_temp_control` | `equipment.has_evap_temp` |
| `night_di` | `equipment.has_night_input` |

Генератор додає `requires_state` та `disabled_hint` до кожного option в ui.json.
Всі options ЗАВЖДИ присутні в ui.json — недоступні показуються як disabled з підказкою.

WebUI: `SelectWidget` перевіряє `$state[opt.requires_state]` реактивно.

#### Реальні приклади

**Defrost constraints:**

```json
"constraints": {
  "defrost.type": {
    "type": "enum_filter",
    "values": {
      "0": {"always": true},
      "1": {"requires_feature": "defrost_electric", "disabled_hint": "Потрібне реле відтайки (defrost_relay)"},
      "2": {"requires_feature": "defrost_hot_gas", "disabled_hint": "Потрібне реле відтайки (defrost_relay)"}
    }
  },
  "defrost.initiation": {
    "type": "enum_filter",
    "values": {
      "0": {"always": true},
      "1": {"requires_feature": "defrost_by_sensor", "disabled_hint": "Потрібен датчик випарника (evap_temp)"},
      "2": {"requires_feature": "defrost_by_sensor", "disabled_hint": "Потрібен датчик випарника (evap_temp)"},
      "3": {"always": true}
    }
  },
  "defrost.termination": {
    "type": "enum_filter",
    "values": {
      "0": {"requires_feature": "defrost_by_sensor", "disabled_hint": "Потрібен датчик випарника (evap_temp)"},
      "1": {"always": true}
    }
  }
}
```

**Thermostat constraints:**

```json
"constraints": {
  "thermostat.evap_fan_mode": {
    "type": "enum_filter",
    "values": {
      "0": {"always": true},
      "1": {"always": true},
      "2": {"requires_feature": "fan_temp_control", "disabled_hint": "Потрібен датчик випарника (evap_temp)"}
    }
  },
  "thermostat.night_mode": {
    "type": "enum_filter",
    "values": {
      "0": {"always": true},
      "1": {"always": true},
      "2": {"requires_feature": "night_di", "disabled_hint": "Потрібен дискретний вхід (night_input)"},
      "3": {"always": true}
    }
  }
}
```

#### Runtime ланцюг

1. Користувач підключає hardware в Bindings page → Save → ESP restart
2. EquipmentModule публікує `equipment.has_*` = true для підключеного обладнання
3. SelectWidget реактивно перевіряє `$state[opt.requires_state]`
4. Option стає enabled/disabled в реальному часі

### 3.9 Секція `dependencies` (зарезервовано)

> **Статус:** ЗАРЕЗЕРВОВАНО, не використовується на практиці.
> Модулі в ModESP v4 комунікують виключно через SharedState.
> Equipment Manager арбітрує запити від бізнес-модулів.
> Прямих залежностей між модулями немає.

---

## 4. Bindings Manifest (`data/bindings.json`)

Bindings з'єднують hardware ресурси (з board.json) з ролями (з equipment manifest)
через конкретні драйвери.

### 4.1 Структура

```json
{
  "manifest_version": 1,
  "bindings": [
    {"hardware": "relay_1", "driver": "pcf8574_relay", "role": "compressor", "module": "equipment"},
    {"hardware": "ow_1",    "driver": "ds18b20",       "role": "air_temp",   "module": "equipment", "address": "28:8C:5E:45:D4:08:44:09"},
    {"hardware": "din_1",   "driver": "pcf8574_input",  "role": "door_contact", "module": "equipment"}
  ]
}
```

### 4.2 Поля binding

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `hardware` | string | так | ID з board.json (gpio_outputs, onewire_buses, expander_outputs, etc.) |
| `driver` | string | так | Ім'я драйвера (з drivers/<name>/manifest.json) |
| `role` | string | так | Функціональна роль (з equipment.requires[].role) |
| `module` | string | так | **ЗАВЖДИ `"equipment"`** |
| `address` | string | ні | Адреса пристрою (ROM для DS18B20) |

### 4.3 Важливі правила

1. **Поле `module` ЗАВЖДИ = `"equipment"`.**
   Equipment Manager — єдиний модуль з прямим доступом до HAL.
   Всі інші бізнес-модулі (thermostat, defrost, protection) працюють через SharedState.

2. **`hardware` ID повинен існувати** в одній з секцій board.json
   (gpio_outputs, gpio_inputs, onewire_buses, adc_channels, expander_outputs, expander_inputs).

3. **`driver` повинен бути сумісний** з hardware_type ресурсу.

4. **`role` повинна існувати** в equipment.requires[].role.

5. **`address` обов'язкова** для драйверів з `requires_address: true` (ds18b20).
   Формат: `"28:XX:XX:XX:XX:XX:XX:XX"` (8-байтний ROM).

6. **Один hardware** може мати тільки один binding.
   Виняток: onewire_buses з різними address (декілька DS18B20 на одній шині).

### 4.4 Валідація

Генератор перевіряє при build:
- hardware ID існує в board.json
- driver manifest існує
- hardware_type сумісний з driver
- role існує в equipment.requires
- address присутня якщо requires_address: true

### 4.5 Повний приклад bindings.json

```json
{
  "manifest_version": 1,
  "bindings": [
    {"hardware": "relay_1", "driver": "pcf8574_relay", "role": "compressor",    "module": "equipment"},
    {"hardware": "relay_2", "driver": "pcf8574_relay", "role": "evap_fan",      "module": "equipment"},
    {"hardware": "relay_3", "driver": "pcf8574_relay", "role": "cond_fan",      "module": "equipment"},
    {"hardware": "relay_4", "driver": "pcf8574_relay", "role": "defrost_relay", "module": "equipment"},
    {"hardware": "ow_1",    "driver": "ds18b20",       "role": "air_temp",      "module": "equipment", "address": "28:8C:5E:45:D4:08:44:09"},
    {"hardware": "ow_1",    "driver": "ds18b20",       "role": "evap_temp",     "module": "equipment", "address": "28:40:0A:45:D4:72:7E:F0"},
    {"hardware": "din_1",   "driver": "pcf8574_input",  "role": "door_contact", "module": "equipment"}
  ]
}
```

---

## 5. Project Manifest (`project.json`)

Project manifest визначає які модулі активні та системні налаштування.

### 5.1 Структура

```json
{
  "project": "cold_room_v1",
  "version": "1.0.0",
  "description": "Холодильна камера — базова конфігурація",
  "modules": [
    "equipment",
    "protection",
    "thermostat",
    "defrost",
    "datalogger"
  ],
  "system": {
    "device_name": "ModESP",
    "pages": {
      "dashboard": true,
      "network": true,
      "firmware": true,
      "system": true
    }
  }
}
```

### 5.2 Поля

| Поле | Тип | Обов'язково | Опис |
|------|-----|-------------|------|
| `project` | string | так | Ім'я проекту |
| `version` | string | так | Версія проекту |
| `description` | string | так | Опис |
| `modules` | array | так | Активні модулі (порядок = порядок завантаження) |
| `system.device_name` | string | ні | Ім'я пристрою (для WiFi AP, MQTT) |
| `system.pages` | object | ні | Які системні сторінки увімкнені |

### 5.3 Активні модулі (5 штук)

| Модуль | Пріоритет | Опис |
|--------|-----------|------|
| `equipment` | CRITICAL (0) | HAL owner, арбітрація реле, сенсори |
| `protection` | HIGH (1) | 5 моніторів аварій |
| `thermostat` | NORMAL (2) | On/off термостат з асиметричним диференціалом |
| `defrost` | NORMAL (2) | 7-фазна відтайка (3 типи) |
| `datalogger` | LOW (3) | 6-канальний logger + events |

Порядок update: Equipment(0) → Protection(1) → Thermostat(2) + Defrost(2) → DataLogger(3)

---

## 6. Валідація маніфестів

Генератор `tools/generate_ui.py` автоматично валідує маніфести при build.

### 6.1 Single-manifest validation

| ID | Правило | Severity |
|----|---------|----------|
| V01 | `manifest_version` обов'язковий і = 1 | error |
| V02 | `module` і `state` обов'язкові | error |
| V03 | State key має `type` і `access` | error |
| V04 | Readwrite float/int без options → `min`, `max`, `step` обов'язкові | error |
| V05 | UI widget key існує в `state` або `inputs` | error |
| V06 | Widget type сумісний з state type | error |
| V07 | MQTT publish/subscribe keys існують в `state` | error |
| V08 | Display keys існують в `state` | error |
| V09 | Chart widget пропускає перевірку key | - |
| V10 | - | - |
| V11 | - | - |
| V12 | - | - |
| V13 | State key починається з `<module>.` | warning |
| V14 | `features.*.controls_settings` keys існують в `state` | error |
| V15 | `features.requires_roles` існують в `equipment.requires[].role` | error |
| V16 | `constraints.*.values.*.requires_feature` існує в `features` | error |
| V17 | `options[].value` ПОВИНЕН бути int | error |
| V18 | State key з `options` + UI widget != `"select"` | warning |
| V19 | `visible_when` формат: object з `key` + рівно один оператор (`eq`/`neq`/`in`) | error |

### 6.2 Cross-module validation

| ID | Правило | Severity |
|----|---------|----------|
| X01 | Duplicate state keys між модулями | error |
| X02 | `inputs.source_module` існує в project.json | warning |
| X03 | Input key існує в source module state | warning |
| V15 | Features requires_roles існують в equipment.requires | error |

### 6.3 Widget type compatibility

| Widget | Сумісні типи state |
|--------|--------------------|
| `slider` | float, int |
| `number_input` | float, int |
| `select` | int, string |
| `indicator` | bool |
| `toggle` | bool |
| `button` | bool |
| `status_text` | string |
| `value` | float, int, bool, string |
| `chart` | float |

---

## 7. Приклад: створення нового модуля

### Крок 1: Manifest

Створюємо `modules/my_module/manifest.json`:

```json
{
  "manifest_version": 1,
  "module": "my_module",
  "description": "My custom module",

  "inputs": {
    "equipment.air_temp": {
      "source_module": "equipment",
      "type": "float",
      "description": "Температура камери",
      "optional": false
    }
  },

  "features": {
    "basic": {
      "description": "Базова функціональність",
      "requires_roles": [],
      "always_active": true,
      "controls_settings": ["my_module.threshold"]
    },
    "advanced": {
      "description": "Розширена функціональність",
      "requires_roles": ["evap_temp"],
      "controls_settings": ["my_module.evap_threshold"]
    }
  },

  "constraints": {
    "my_module.mode": {
      "type": "enum_filter",
      "values": {
        "0": {"always": true},
        "1": {"requires_feature": "advanced", "disabled_hint": "Потрібен датчик випарника"}
      }
    }
  },

  "state": {
    "my_module.value": {
      "type": "float",
      "access": "read",
      "unit": "°C",
      "description": "Поточне значення"
    },
    "my_module.threshold": {
      "type": "float",
      "access": "readwrite",
      "min": -50.0,
      "max": 50.0,
      "step": 0.5,
      "default": 10.0,
      "persist": true,
      "description": "Поріг"
    },
    "my_module.evap_threshold": {
      "type": "float",
      "access": "readwrite",
      "min": -40.0,
      "max": 10.0,
      "step": 0.5,
      "default": -5.0,
      "persist": true,
      "description": "Поріг випарника"
    },
    "my_module.mode": {
      "type": "int",
      "access": "readwrite",
      "default": 0,
      "persist": true,
      "options": [
        {"value": 0, "label": "Базовий"},
        {"value": 1, "label": "Розширений"}
      ],
      "description": "Режим роботи"
    },
    "my_module.active": {
      "type": "bool",
      "access": "read",
      "description": "Модуль активний"
    }
  },

  "ui": {
    "page": "Мій модуль",
    "page_id": "my_module",
    "icon": "settings",
    "order": 5,
    "cards": [
      {
        "title": "Стан",
        "widgets": [
          {"key": "my_module.value", "widget": "value"},
          {"key": "my_module.active", "widget": "indicator",
           "on_label": "Активний", "off_label": "Неактивний"}
        ]
      },
      {
        "title": "Налаштування",
        "collapsible": true,
        "widgets": [
          {"key": "my_module.mode", "widget": "select"},
          {"key": "my_module.threshold", "widget": "number_input"},
          {"key": "my_module.evap_threshold", "widget": "number_input",
           "visible_when": {"key": "my_module.mode", "eq": 1}}
        ]
      }
    ]
  },

  "mqtt": {
    "publish": ["my_module.value", "my_module.active"],
    "subscribe": ["my_module.threshold", "my_module.evap_threshold", "my_module.mode"]
  }
}
```

### Крок 2: Додати в project.json

```json
"modules": ["equipment", "protection", "thermostat", "defrost", "datalogger", "my_module"]
```

### Крок 3: Hardware bindings

Якщо модуль потребує hardware — ролі визначаються в equipment manifest, а bindings
завжди з `"module": "equipment"`:

```json
{"hardware": "ow_1", "driver": "ds18b20", "role": "evap_temp", "module": "equipment", "address": "28:..."}
```

**ВАЖЛИВО:** Бізнес-модулі НЕ мають прямого доступу до HAL.
Вони читають сенсорні дані з SharedState (`equipment.*` keys)
і публікують запити (`my_module.req.compressor` тощо).
Equipment Manager арбітрує запити і керує реле.

### Крок 4: C++ реалізація

```cpp
// components/my_module/src/my_module.cpp
#include "base_module.h"

static const char* TAG = "MyModule";

class MyModule : public modesp::BaseModule {
public:
    MyModule() : BaseModule("my_module", ModulePriority::NORMAL) {}

    void on_init() override {
        ESP_LOGI(TAG, "Ініціалізація MyModule");
    }

    void on_update() override {
        auto temp = state_->get_float("equipment.air_temp");
        auto threshold = state_->get_float("my_module.threshold");

        if (temp && threshold) {
            bool active = *temp > *threshold;
            state_->set("my_module.active", active);

            // Feature guard — розширена логіка тільки з evap_temp
            if (has_feature("advanced")) {
                auto evap = state_->get_float("equipment.evap_temp");
                // ...
            }
        }
    }

    void on_stop() override {
        ESP_LOGI(TAG, "Зупинка MyModule");
    }
};
```

### Крок 5: Build та перевірка

```bash
idf.py build
```

Генератор автоматично:
1. Валідує маніфест (V01-V19)
2. Генерує UI widget в ui.json
3. Додає state keys в state_meta.h
4. Додає MQTT topics в mqtt_topics.h
5. Додає features в features_config.h

---

## 8. Приклад: створення нового драйвера

### Крок 1: Driver Manifest

Створюємо `drivers/my_sensor/manifest.json`:

```json
{
  "manifest_version": 1,
  "driver": "my_sensor",
  "description": "Мій кастомний сенсор",
  "category": "sensor",
  "hardware_type": "adc_channel",
  "requires_address": false,
  "multiple_per_bus": false,
  "provides": {"type": "float", "unit": "°C", "range": [-40, 125]},
  "settings": [
    {"key": "read_interval_ms", "type": "int",   "default": 1000, "min": 100, "max": 60000, "step": 100, "unit": "мс", "description": "Інтервал опитування"},
    {"key": "offset",           "type": "float", "default": 0.0,  "min": -5.0, "max": 5.0, "step": 0.1, "unit": "°C", "description": "Корекція показань"}
  ]
}
```

### Крок 2: Додати до equipment.requires

В `modules/equipment/manifest.json` додати драйвер до відповідної ролі:

```json
{"role": "air_temp", "type": "sensor", "driver": ["ds18b20", "ntc", "my_sensor"], "label": "Темп. камери"}
```

### Крок 3: Board manifest

Якщо потрібен новий hardware_type — додати секцію в board.json.
Якщо використовується існуючий (adc_channel) — нічого додавати не треба.

### Крок 4: Binding

```json
{"hardware": "adc_1", "driver": "my_sensor", "role": "air_temp", "module": "equipment"}
```

### Крок 5: C++ реалізація

Драйвер реалізує інтерфейс `IDriver` з modesp_hal:

```cpp
class MySensorDriver : public ISensorDriver {
public:
    float read() override;
    bool is_healthy() override;
    void configure(const DriverConfig& cfg) override;
};
```

Реєстрація в DriverManager — автоматична за ім'ям з маніфесту.

---

## 9. Зведена таблиця маніфестів

### 9.1 Hardware types та сумісні драйвери

| Hardware type | Board section | Драйвери | Категорія |
|---------------|---------------|----------|-----------|
| `gpio_output` | `gpio_outputs` | relay | actuator |
| `gpio_input` | `gpio_inputs` | digital_input | sensor |
| `onewire_bus` | `onewire_buses` | ds18b20 | sensor |
| `adc_channel` | `adc_channels` | ntc | sensor |
| `i2c_expander_output` | `expander_outputs` | pcf8574_relay | actuator |
| `i2c_expander_input` | `expander_inputs` | pcf8574_input | sensor |

### 9.2 Всі драйвери

| Драйвер | Категорія | hardware_type | Статус |
|---------|-----------|---------------|--------|
| ds18b20 | sensor | onewire_bus | MATCH_ROM multi-sensor, SEARCH_ROM scan |
| relay | actuator | gpio_output | GPIO реле з min on/off time |
| ntc | sensor | adc_channel | NTC через ADC (B-parameter equation) |
| digital_input | sensor | gpio_input | GPIO input з 50ms debounce |
| pcf8574_relay | actuator | i2c_expander_output | I2C PCF8574 реле (KC868-A6) |
| pcf8574_input | sensor | i2c_expander_input | I2C PCF8574 вхід (KC868-A6) |

### 9.3 Всі модулі

| Модуль | Пріоритет | State keys | Persist | Features |
|--------|-----------|------------|---------|----------|
| equipment | CRITICAL | 23 | 5 | - |
| protection | HIGH | 14 | 7 | 2 (basic_protection, door_protection) |
| thermostat | NORMAL | 25 | 16 | 7 (basic_cooling, fan_control, fan_temp_control, condenser_fan, night_setback, night_di, display_defrost) |
| defrost | NORMAL | 25 | 13+2 runtime | 4 (defrost_timer, defrost_by_sensor, defrost_electric, defrost_hot_gas) |
| datalogger | LOW | 10 | 7 | - |

### 9.4 Згенеровані файли

| Файл | Що містить | Звідки |
|------|-----------|--------|
| `data/ui.json` | UI schema (pages, cards, widgets з options/visible_when/disabled) | Module manifests |
| `generated/state_meta.h` | constexpr STATE_META array (key, type, access, min, max, step, persist) | Module manifests |
| `generated/mqtt_topics.h` | constexpr MQTT_PUB/MQTT_SUB arrays | Module manifests mqtt section |
| `generated/display_screens.h` | LCD/OLED display data (main_value, menu_items) | Module manifests display section |
| `generated/features_config.h` | constexpr FEATURES array + `is_feature_active()` | Module manifests features + bindings |

### 9.5 Equipment `has_*` state keys

Equipment Manager публікує ці boolean ключі при boot на основі bindings.json:

| State key | Що означає |
|-----------|-----------|
| `equipment.has_defrost_relay` | Реле відтайки підключене |
| `equipment.has_cond_fan` | Вентилятор конденсатора підключений |
| `equipment.has_door_contact` | Контакт дверей підключений |
| `equipment.has_evap_temp` | Датчик випарника підключений |
| `equipment.has_cond_temp` | Датчик конденсатора підключений |
| `equipment.has_night_input` | Вхід нічного режиму підключений |
| `equipment.has_ntc_driver` | Використовується NTC драйвер |
| `equipment.has_ds18b20_driver` | Використовується DS18B20 драйвер |

Ці ключі використовуються:
- В `visible_when` для показу/приховування UI елементів
- В `requires_state` для enable/disable options в select widgets
- В C++ коді через `state_->get_bool("equipment.has_*")`

---

## 10. FAQ / Часті помилки

### Q: Чому всі bindings мають module="equipment"?

**A:** Equipment Manager — єдиний модуль з прямим доступом до HAL (драйверів, GPIO, I2C).
Бізнес-модулі (thermostat, defrost, protection) працюють ТІЛЬКИ через SharedState:
- Читають `equipment.air_temp`, `equipment.compressor` тощо
- Публікують запити: `thermostat.req.compressor`, `defrost.req.defrost_relay`
- Equipment Manager арбітрує запити (Protection lockout > Defrost > Thermostat)

### Q: Як додати новий тип обладнання?

1. Додати роль в `modules/equipment/manifest.json` requires
2. Створити feature в бізнес-модулі з `requires_roles: ["new_role"]`
3. Додати binding в bindings.json
4. Додати FEATURE_TO_STATE маппінг в generate_ui.py (якщо потрібен runtime UI control)
5. Equipment Module публікує `equipment.has_new_role`

### Q: Як працює visible_when з features?

`visible_when` і `features` — різні механізми:
- **features** — build-time (перевіряється при генерації, disabled widgets)
- **visible_when** — runtime (перевіряється в WebUI по поточному state)

Приклад: `visible_when: {"key": "equipment.has_cond_fan", "eq": true}` — widget
видимий тільки коли вентилятор конденсатора підключений (runtime).

### Q: Чому options.value завжди int?

Для сумісності з SharedState (etl::variant) і NVS persistence. String options
використовують int value як ідентифікатор, label — для відображення.

### Q: Як працює defrost_relay замість heater/hg_valve?

Раніше було дві окремі ролі: `defrost_heater` (тен) і `hg_valve` (клапан гарячого газу).
Тепер є ОДНА роль `defrost_relay` — це єдине реле, яке використовується для будь-якого
типу відтайки (електричної або гарячим газом). Тип відтайки визначається налаштуванням
`defrost.type` (0=зупинка, 1=тен, 2=гарячий газ). Фізично до цього реле підключається
або тен, або клапан — залежно від установки.

---

## Changelog

- v2.0 (2026-03-01) — Повне переписування: features, constraints, visible_when, options,
  PCF8574 drivers, defrost_relay (замість heater/hg_valve), air_temp (замість chamber_temp),
  всі bindings до equipment. 6 драйверів, 21 widget, 19 валідаторів.
  I2C/PCF8574 board sections. FEATURE_TO_STATE маппінг.
- v1.0 (2026-02-16) — Початкова версія стандарту маніфестів.
