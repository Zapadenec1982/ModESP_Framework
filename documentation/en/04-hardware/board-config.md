# board.json — declaring hardware capabilities

> 📖 **Українською:** [documentation/uk/04-hardware/board-config.md](../../uk/04-hardware/board-config.md)

`board.json` describes the **physical capabilities** of а board — what
GPIO pins are available, what I2C buses exist, which expander chips are
mounted, what 1-Wire і ADC channels are wired. The framework reads це at
build time і uses it to gate `bindings.json` validity (а binding cannot
target а GPIO not declared у board.json).

This page is the reference для writing your own `board.json` when you
build а custom PCB або port the framework to а new dev board. Two reference
boards ship із the framework: `boards/dev/` (ESP32-DevKit із direct GPIO
relays) і `boards/kc868a6/` (Kincony KC868-A6 із PCF8574 I2C expanders).

## What board.json is і isn't

**Is:**
- Hardware capability declaration. "On це board, GPIO 14 drives relay 1,
  GPIO 15 is OneWire bus 1."
- Static at build time. Selected via Kconfig (`CONFIG_MODESP_BOARD`).
- Stored у `boards/<name>/board.json` і copied to LittleFS image at build.

**Isn't:**
- A driver-to-role mapping (that's `bindings.json`).
- А runtime configuration (the file ships read-only on the device).
- А schematic. The framework doesn't know how things are wired beyond the
  fields listed below — board layout, ground planes, power topology stay
  in your PCB design tools.

## Folder layout per board

```
boards/<board_name>/
├── board.json          ← Hardware capabilities (це file)
└── bindings.json       ← Driver assignments (next page)
```

Switch boards без code changes: change `CONFIG_MODESP_BOARD=<name>` у
sdkconfig.defaults і rebuild. The build system copies the selected board's
files до `data/` for LittleFS bundling.

## Top-level fields

| Field | Type | Required | Notes |
|---|---|---|---|
| `manifest_version` | int | yes | Currently `1`. |
| `board` | string | yes | Board identifier. Matches folder name. |
| `version` | string | recommended | Hardware revision (PCB version). |
| `description` | string | recommended | One-line summary including form factor / target use. |

Example header:

```json
{
  "manifest_version": 1,
  "board": "template_dev_v1",
  "version": "1.0.0",
  "description": "Template dev board — ESP32-WROOM, GPIO relays + OneWire"
}
```

## Sections (all optional — declare only what your board has)

### `gpio_outputs`

Direct GPIO pins що drive an output (relay, LED, SSR control). Each entry:

```json
{
  "id": "relay_1",          // Unique ID — bindings.json references це
  "gpio": 14,               // ESP32 GPIO number
  "active_high": true,      // true = GPIO HIGH switches relay ON
  "label": "Relay 1"        // Human label for UI / diagnostics
}
```

Full example:

```json
"gpio_outputs": [
  {"id": "relay_1", "gpio": 14, "active_high": true, "label": "Relay 1"},
  {"id": "relay_2", "gpio": 16, "active_high": true, "label": "Relay 2"},
  {"id": "relay_3", "gpio": 17, "active_high": true, "label": "Relay 3"},
  {"id": "relay_4", "gpio": 18, "active_high": true, "label": "Relay 4"}
]
```

### `gpio_inputs`

Direct GPIO pins що read а contact / switch / sensor digital output:

```json
{
  "id": "din_1",
  "gpio": 34,
  "pull": "up",            // "up" / "down" / "none" — internal pull resistor
  "label": "Digital input 1"
}
```

### `onewire_buses`

1-Wire buses (Dallas DS18B20 і compatible). One bus typically supports
many sensors sharing the same data line.

```json
"onewire_buses": [
  {"id": "ow_1", "gpio": 15, "label": "OneWire bus 1"},
  {"id": "ow_2", "gpio": 32, "label": "OneWire bus 2"}
]
```

GPIO must have а pull-up resistor on the data line (4.7 kΩ typical для
short cables; lower для long). The framework configures internal pull-up
але hardware pull-up is recommended.

### `adc_channels`

Analog input channels (NTC thermistors, 4-20 mA loops після voltage divider,
generic ADC reads).

```json
"adc_channels": [
  {"id": "adc_1", "gpio": 36, "atten": 11, "label": "ADC 1"},
  {"id": "adc_2", "gpio": 39, "atten": 11, "label": "ADC 2"}
]
```

`atten` is the ESP32 ADC attenuation setting (`0`/`2.5`/`6`/`11` dB).
`11 dB` gives the widest range (~0..3.3 V); lower attenuation gives better
resolution but narrower range. Match to your divider design.

Only ADC1 channels (GPIO 32-39) are usable — ADC2 conflicts із Wi-Fi.

### `i2c_buses`

I2C bus declarations. Used by I2C-connected expanders, sensors, displays.

```json
"i2c_buses": [
  {"id": "i2c_0", "sda": 4, "scl": 15, "freq_hz": 100000, "label": "I2C шина"}
]
```

`freq_hz` typically `100000` (standard) or `400000` (fast). Higher speeds
need shorter cables або bus boosters.

### `i2c_expanders`

PCF8574 / similar I/O expanders connected via І²C. One expander provides
8 GPIO pins; multiple expanders share а bus (different addresses).

```json
"i2c_expanders": [
  {"id": "relay_exp",  "bus": "i2c_0", "chip": "pcf8574", "address": "0x24", "pins": 8, "label": "Реле PCF8574"},
  {"id": "input_exp",  "bus": "i2c_0", "chip": "pcf8574", "address": "0x22", "pins": 8, "label": "Входи PCF8574"}
]
```

| Field | Notes |
|---|---|
| `bus` | Must reference an `id` declared у `i2c_buses`. |
| `chip` | Currently only `"pcf8574"`. PCF8575 (16-pin) planned. |
| `address` | I²C address — string format `"0xNN"`. PCF8574: 0x20..0x27 (із pull-up address pins). |
| `pins` | Pin count, usually `8`. |

### `expander_outputs` / `expander_inputs`

Once you've declared expanders, expose their pins individually для bindings:

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

`active_high: false` is typical for PCF8574 relays — the chip's outputs are
open-drain, so writing `0` pulls the line low і turns the relay ON
(opto-isolated relay modules wired through pull-ups).

`invert: true` for inputs adapts the same chip: closed contact pulls line
low, але logically that's "input active" — invert у board.json і your
driver sees clean `true`/`false`.

## What about RS-485, CAN, SPI?

Stage 1.5 hardware roadmap adds:
- `rs485_buses` для serial industrial protocols.
- `spi_buses` для high-speed peripherals.
- `can_buses` для automotive / industrial CAN.

Currently absent from the framework — Stage 1 focused on the most common
ESP32 hardware patterns. Если ви запускаєте такі periphery — file а
request, or contribute а driver.

## Reference examples

### `boards/dev/board.json` — ESP32-DevKit minimal

ESP32-DevKit із 4 direct GPIO relays, OneWire, 1 digital input, 2 ADC
channels. Good для prototyping і unit-testing your bindings.

### `boards/kc868a6/board.json` — Kincony KC868-A6 production

Industrial controller із 6 relays через PCF8574, 6 inputs через PCF8574,
2 OneWire buses, 4 ADC channels. Reference для PCF8574-based boards.

Read both files source-first — це 1-2 minute reads each і clarify the
schema better than any prose.

## Switching boards

In `sdkconfig.defaults`:

```ini
CONFIG_MODESP_BOARD_DEV=y
# or
CONFIG_MODESP_BOARD_KC868A6=y
```

The build system selects the corresponding `boards/<name>/` directory і
copies its `board.json` + `bindings.json` to `data/` for LittleFS bundling.

After change: `idf.py fullclean && idf.py build` (board change is enough
of а structural delta to warrant fullclean).

## Validation і error reporting

`generate_ui.py` runs at build time і validates:

1. Every `id` у each section is unique within the board.
2. References resolve: `expander_outputs[].expander` matches an
   `i2c_expanders[].id`; `i2c_expanders[].bus` matches an `i2c_buses[].id`.
3. GPIO numbers are valid ESP32 GPIOs (not reserved для flash, etc.).
4. ADC channels are on ADC1 (GPIO 32-39).

Failures abort the build із specific messages. Misconfigured board.json
never reaches flash.

## Common mistakes

**Using ADC2 channels:** GPIO 0, 2, 4, 12-15, 25-27 are ADC2. They conflict
із Wi-Fi і give intermittent reads. Stick to ADC1 (GPIO 32-39).

**Forgetting `active_high` for relays:** active-low relays із `active_high:
true` invert your control logic. Test із а multimeter on driver init.

**Address conflicts on I²C:** two devices із the same `address` on the
same bus break communication для both. Read your expander's address-pin
schematic carefully.

**Reserved GPIO usage:** GPIO 6-11 are flash SPI — using them bricks the
chip. GPIO 0 is strapping (boot mode) і should not drive а relay. The
validator catches most of these.

**Mismatched `pins` count:** PCF8574 is 8 pins; PCF8575 is 16. Setting
`pins: 16` on а PCF8574 corrupts indices 8-15 silently. The framework
doesn't probe the chip — trust your schematic.

## Next steps

- **[bindings.md](bindings.md)** — wire drivers to board hardware via
  `bindings.json`.
- **[deployment.md](deployment.md)** *(planned)* — flashing, monitor,
  factory reset, recovery.
- **[modules/equipment.md](../03-framework-reference/modules/equipment.md)**
  *(planned)* — Equipment Manager — module що consumes both board.json і
  bindings.json і exposes `equipment.*` state keys.
- **[writing-a-driver.md](../02-module-author-guide/writing-a-driver.md)**
  — add support для а new sensor / actuator chip not yet supported.
