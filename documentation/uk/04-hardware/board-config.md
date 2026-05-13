# board.json — декларування можливостей плати

> 📖 **In English:** [documentation/en/04-hardware/board-config.md](../../en/04-hardware/board-config.md)

`board.json` описує **фізичні можливості** плати — які GPIO pins доступні,
які I2C buses існують, які expander чіпи встановлені, які 1-Wire і ADC
канали зведені. Фреймворк читає це при build time і використовує щоб gate
`bindings.json` validity (binding не може target-ити GPIO не declared у
board.json).

Ця сторінка — reference для написання вашого власного `board.json` коли ви
build-уєте кастомну PCB або port-уєте фреймворк на нову dev board. Дві
reference boards поставляються з фреймворком: `boards/dev/` (ESP32-DevKit
з прямими GPIO relays) і `boards/kc868a6/` (Kincony KC868-A6 з PCF8574 I2C
expanders).

## Що board.json є і не є

**Є:**
- Декларація hardware capability. "На цій платі GPIO 14 драйвить реле 1,
  GPIO 15 — OneWire bus 1."
- Static при build time. Обирається через Kconfig (`CONFIG_MODESP_BOARD`).
- Зберігається у `boards/<name>/board.json` і копіюється у LittleFS image
  при build.

**Не є:**
- Driver-to-role mapping (це `bindings.json`).
- Runtime конфігурація (file ships read-only на пристрій).
- Schematic. Фреймворк не знає як речі wired beyond поля перераховані
  нижче — board layout, ground planes, power topology stay у PCB design
  tools.

## Структура папки на board

```
boards/<board_name>/
├── board.json          ← Hardware capabilities (це file)
└── bindings.json       ← Driver assignments (наступна сторінка)
```

Перемикання boards без code змін: змінити `CONFIG_MODESP_BOARD=<name>` у
sdkconfig.defaults і rebuild. Build system копіює files обраної board у
`data/` для LittleFS bundling.

## Top-level поля

| Поле | Тип | Обов'язкове | Примітки |
|---|---|---|---|
| `manifest_version` | int | так | Зараз `1`. |
| `board` | string | так | Board identifier. Match-ить ім'я папки. |
| `version` | string | рекомендоване | Hardware revision (PCB version). |
| `description` | string | рекомендоване | Однорядковий summary включно з form factor / target use. |

Приклад header:

```json
{
  "manifest_version": 1,
  "board": "template_dev_v1",
  "version": "1.0.0",
  "description": "Template dev board — ESP32-WROOM, GPIO relays + OneWire"
}
```

## Секції (всі опціональні — декларуйте лише те, що ваша board має)

### `gpio_outputs`

Прямі GPIO pins що драйвлять output (реле, LED, SSR control). Кожен entry:

```json
{
  "id": "relay_1",          // Унікальний ID — bindings.json references це
  "gpio": 14,               // ESP32 GPIO number
  "active_high": true,      // true = GPIO HIGH перемикає реле ON
  "label": "Relay 1"        // Human label для UI / diagnostics
}
```

Повний приклад:

```json
"gpio_outputs": [
  {"id": "relay_1", "gpio": 14, "active_high": true, "label": "Relay 1"},
  {"id": "relay_2", "gpio": 16, "active_high": true, "label": "Relay 2"},
  {"id": "relay_3", "gpio": 17, "active_high": true, "label": "Relay 3"},
  {"id": "relay_4", "gpio": 18, "active_high": true, "label": "Relay 4"}
]
```

### `gpio_inputs`

Прямі GPIO pins що читають контакт / switch / digital output сенсора:

```json
{
  "id": "din_1",
  "gpio": 34,
  "pull": "up",            // "up" / "down" / "none" — внутрішній pull resistor
  "label": "Digital input 1"
}
```

### `onewire_buses`

1-Wire buses (Dallas DS18B20 і compatible). Одна шина типово supports
багато сенсорів що ділять одну data line.

```json
"onewire_buses": [
  {"id": "ow_1", "gpio": 15, "label": "OneWire bus 1"},
  {"id": "ow_2", "gpio": 32, "label": "OneWire bus 2"}
]
```

GPIO повинен мати pull-up resistor на data line (4.7 kΩ типово для
короткого кабелю; нижче для довгого). Фреймворк налаштовує internal
pull-up, але hardware pull-up рекомендований.

### `adc_channels`

Аналогові input канали (NTC термістори, 4-20 mA loops після voltage
divider, generic ADC reads).

```json
"adc_channels": [
  {"id": "adc_1", "gpio": 36, "atten": 11, "label": "ADC 1"},
  {"id": "adc_2", "gpio": 39, "atten": 11, "label": "ADC 2"}
]
```

`atten` — ESP32 ADC attenuation setting (`0`/`2.5`/`6`/`11` dB). `11 dB`
дає найширший range (~0..3.3 V); нижча attenuation дає кращу resolution
але вужчий range. Match до вашого divider design.

Лише ADC1 канали (GPIO 32-39) usable — ADC2 conflict-иться з Wi-Fi.

### `i2c_buses`

I2C bus декларації. Використовуються I2C-connected expanders, сенсорами,
displays.

```json
"i2c_buses": [
  {"id": "i2c_0", "sda": 4, "scl": 15, "freq_hz": 100000, "label": "I2C шина"}
]
```

`freq_hz` типово `100000` (standard) або `400000` (fast). Вищі швидкості
потребують коротших кабелів або bus boosters.

### `i2c_expanders`

PCF8574 / подібні I/O expanders connected через I²C. Один expander
provides 8 GPIO pins; багато expanders ділять одну шину (різні адреси).

```json
"i2c_expanders": [
  {"id": "relay_exp",  "bus": "i2c_0", "chip": "pcf8574", "address": "0x24", "pins": 8, "label": "Реле PCF8574"},
  {"id": "input_exp",  "bus": "i2c_0", "chip": "pcf8574", "address": "0x22", "pins": 8, "label": "Входи PCF8574"}
]
```

| Поле | Примітки |
|---|---|
| `bus` | Повинен reference `id` declared у `i2c_buses`. |
| `chip` | Зараз лише `"pcf8574"`. PCF8575 (16-pin) planned. |
| `address` | I²C address — string format `"0xNN"`. PCF8574: 0x20..0x27 (з pull-up address pins). |
| `pins` | Pin count, зазвичай `8`. |

### `expander_outputs` / `expander_inputs`

Раз ви декларували expanders, expose-ите їхні pins individually для bindings:

```json
"expander_outputs": [
  {"id": "relay_1", "expander": "relay_exp", "pin": 0, "active_high": false, "label": "Реле 1"},
  {"id": "relay_2", "expander": "relay_exp", "pin": 1, "active_high": false, "label": "Реле 2"},
  // ...
],
"expander_inputs": [
  {"id": "din_1", "expander": "input_exp", "pin": 0, "invert": true, "label": "Вхід 1"},
  // ...
]
```

`active_high: false` типово для PCF8574 relays — outputs chip-а open-drain,
тому writing `0` pulls line low і вмикає реле ON (opto-isolated relay
modules wired through pull-ups).

`invert: true` для inputs adapts the same chip: closed contact pulls line
low, але logically це "input active" — invert у board.json і ваш driver
бачить clean `true`/`false`.

## А як же RS-485, CAN, SPI?

Stage 1.5 hardware roadmap додає:
- `rs485_buses` для serial industrial протоколів.
- `spi_buses` для high-speed periferals.
- `can_buses` для automotive / industrial CAN.

Зараз відсутні з фреймворку — Stage 1 фокусувався на найпоширеніших ESP32
hardware патернах. Якщо ви запускаєте таку periphery — file request, або
contribute driver.

## Reference приклади

### `boards/dev/board.json` — ESP32-DevKit мінімальний

ESP32-DevKit з 4 прямими GPIO relays, OneWire, 1 digital input, 2 ADC
канали. Хороший для prototyping і unit-testing ваших bindings.

### `boards/kc868a6/board.json` — Kincony KC868-A6 production

Industrial controller з 6 relays через PCF8574, 6 inputs через PCF8574,
2 OneWire buses, 4 ADC канали. Reference для PCF8574-based boards.

Прочитайте обидва файли source-first — це 1-2 хвилини на кожен і clarify
схему краще за будь-яку прозу.

## Перемикання boards

У `sdkconfig.defaults`:

```ini
CONFIG_MODESP_BOARD_DEV=y
# або
CONFIG_MODESP_BOARD_KC868A6=y
```

Build system обирає відповідну `boards/<name>/` папку і копіює її
`board.json` + `bindings.json` у `data/` для LittleFS bundling.

Після зміни: `idf.py fullclean && idf.py build` (board change достатньо
structural delta щоб warrant fullclean).

## Валідація і error reporting

`generate_ui.py` запускається при build time і валідує:

1. Кожен `id` у кожній секції unique у межах board.
2. References resolve: `expander_outputs[].expander` match-иться
   `i2c_expanders[].id`; `i2c_expanders[].bus` match-иться `i2c_buses[].id`.
3. GPIO numbers — valid ESP32 GPIOs (не reserved для flash, тощо).
4. ADC channels — на ADC1 (GPIO 32-39).

Failures abort build з конкретними повідомленнями. Misconfigured board.json
ніколи не доходить до flash.

## Поширені помилки

**Використання ADC2 каналів:** GPIO 0, 2, 4, 12-15, 25-27 — ADC2. Вони
conflict-яться з Wi-Fi і дають intermittent reads. Дотримуйтесь ADC1
(GPIO 32-39).

**Забутий `active_high` для relays:** active-low relays з `active_high:
true` інвертують вашу control логіку. Тестуйте з multimeter при driver
init.

**Address conflicts на I²C:** два пристрої з тією ж `address` на тій же
шині break communication для обох. Прочитайте expander's address-pin
schematic уважно.

**Reserved GPIO usage:** GPIO 6-11 — flash SPI — використання їх bricks
chip. GPIO 0 — strapping (boot mode) і не повинен драйвити реле.
Валідатор catches більшість цього.

**Mismatched `pins` count:** PCF8574 — 8 pins; PCF8575 — 16. Setting
`pins: 16` на PCF8574 corrupts індекси 8-15 silently. Фреймворк не probe-ить
chip — довіряйте вашій schematic.

## Що далі

- **[bindings.md](bindings.md)** — wire-ити drivers до board hardware
  через `bindings.json`.
- **[deployment.md](deployment.md)** *(planned)* — flashing, monitor,
  factory reset, recovery.
- **[modules/equipment.md](../03-framework-reference/modules/equipment.md)**
  *(planned)* — Equipment Manager — модуль що consumes обидва board.json
  і bindings.json і exposes `equipment.*` state keys.
- **[writing-a-driver.md](../02-module-author-guide/writing-a-driver.md)**
  — додати support для нового сенсора / actuator chip ще не supported.
