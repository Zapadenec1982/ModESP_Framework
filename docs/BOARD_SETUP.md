# Board Setup Guide

How to configure ModESP Framework for your hardware. One firmware codebase — different boards via JSON config files.

## How It Works

```
boards/
├── dev/               ← ESP32 DevKit (direct GPIO)
│   ├── board.json
│   └── bindings.json
├── kc868a6/           ← Kincony KC868-A6 (I2C PCF8574)
│   ├── board.json
│   └── bindings.json
└── my_board/          ← YOUR BOARD
    ├── board.json     # What hardware exists on the PCB
    └── bindings.json  # What roles map to which drivers
```

At build time, CMake copies `board.json` + `bindings.json` from your board directory into `data/`. The firmware reads them at boot.

## Selecting a Board

### Option 1: Kconfig (menuconfig)

```bash
idf.py menuconfig
# → ModESP Configuration → Board → Select your board
idf.py build
```

### Option 2: sdkconfig direct edit

```
# sdkconfig
CONFIG_MODESP_BOARD_DIR="my_board"
```

### Option 3: Default

Without configuration, `boards/dev/` is used.

## Creating a New Board

### Step 1: Create board directory

```bash
mkdir boards/my_board
```

### Step 2: Write board.json — hardware definition

`board.json` describes **what physical hardware exists on your PCB**. It does NOT define what the hardware is used for — that's `bindings.json`.

#### Minimal (1 relay + 1 sensor)

```json
{
  "manifest_version": 1,
  "board": "my_board_v1",
  "version": "1.0.0",
  "description": "My custom board — 1 relay, 1 DS18B20",
  "gpio_outputs": [
    {"id": "relay_1", "gpio": 14, "active_high": true, "label": "Relay 1"}
  ],
  "onewire_buses": [
    {"id": "ow_1", "gpio": 15, "label": "OneWire bus 1"}
  ],
  "gpio_inputs": [],
  "adc_channels": []
}
```

#### Full (4 relays + 2 sensors + DI + ADC)

```json
{
  "manifest_version": 1,
  "board": "industrial_v2",
  "version": "2.0.0",
  "description": "Industrial board — 4 relays, 2 DS18B20, 1 DI, 2 ADC",
  "gpio_outputs": [
    {"id": "relay_1", "gpio": 14, "active_high": true, "label": "Relay 1"},
    {"id": "relay_2", "gpio": 16, "active_high": true, "label": "Relay 2"},
    {"id": "relay_3", "gpio": 17, "active_high": true, "label": "Relay 3"},
    {"id": "relay_4", "gpio": 18, "active_high": true, "label": "Relay 4"}
  ],
  "onewire_buses": [
    {"id": "ow_1", "gpio": 15, "label": "OneWire bus 1"}
  ],
  "gpio_inputs": [
    {"id": "din_1", "gpio": 34, "pull": "up", "label": "Digital input 1"}
  ],
  "adc_channels": [
    {"id": "adc_1", "gpio": 36, "atten": 11, "label": "ADC 1"},
    {"id": "adc_2", "gpio": 39, "atten": 11, "label": "ADC 2"}
  ]
}
```

#### I2C expander (KC868-A6 style)

```json
{
  "manifest_version": 1,
  "board": "kc868_a6",
  "version": "1.0.0",
  "description": "KC868-A6 — 6 relays + 6 inputs via PCF8574 I2C",
  "i2c_buses": [
    {"id": "i2c_0", "sda": 4, "scl": 15, "freq_hz": 100000, "label": "I2C bus"}
  ],
  "i2c_expanders": [
    {"id": "relay_exp", "bus": "i2c_0", "chip": "pcf8574", "address": "0x24", "pins": 8, "label": "Relay PCF8574"},
    {"id": "input_exp", "bus": "i2c_0", "chip": "pcf8574", "address": "0x22", "pins": 8, "label": "Input PCF8574"}
  ],
  "gpio_outputs": [
    {"id": "relay_1", "expander": "relay_exp", "pin": 0, "active_low": true, "label": "Relay 1"},
    {"id": "relay_2", "expander": "relay_exp", "pin": 1, "active_low": true, "label": "Relay 2"},
    {"id": "relay_3", "expander": "relay_exp", "pin": 2, "active_low": true, "label": "Relay 3"},
    {"id": "relay_4", "expander": "relay_exp", "pin": 3, "active_low": true, "label": "Relay 4"}
  ],
  "gpio_inputs": [
    {"id": "din_1", "expander": "input_exp", "pin": 0, "label": "Input 1"},
    {"id": "din_2", "expander": "input_exp", "pin": 1, "label": "Input 2"}
  ],
  "onewire_buses": [
    {"id": "ow_1", "gpio": 32, "label": "OneWire bus 1"},
    {"id": "ow_2", "gpio": 33, "label": "OneWire bus 2"}
  ],
  "adc_channels": [
    {"id": "adc_1", "gpio": 36, "atten": 11, "label": "ADC 1"},
    {"id": "adc_2", "gpio": 39, "atten": 11, "label": "ADC 2"}
  ]
}
```

### board.json field reference

| Section | Field | Required | Description |
|---------|-------|----------|-------------|
| root | `board` | Yes | Board identifier (used in firmware name) |
| root | `version` | Yes | PCB revision |
| root | `description` | No | Human-readable description |
| `gpio_outputs[]` | `id` | Yes | Unique hardware ID ("relay_1") |
| | `gpio` | Yes* | ESP32 GPIO pin number |
| | `expander` | Yes* | I2C expander ID (alternative to gpio) |
| | `pin` | Yes* | Pin on I2C expander (with expander) |
| | `active_high` | No | Default true. Set `active_low: true` for inverted |
| | `label` | No | Display label in WebUI |
| `onewire_buses[]` | `id` | Yes | Bus ID ("ow_1") |
| | `gpio` | Yes | Data pin (4.7K pull-up to 3.3V required) |
| `gpio_inputs[]` | `id` | Yes | Input ID ("din_1") |
| | `gpio` | Yes* | ESP32 GPIO pin |
| | `expander` / `pin` | Yes* | I2C expander alternative |
| | `pull` | No | "up", "down", or "none" (default: none) |
| `adc_channels[]` | `id` | Yes | Channel ID ("adc_1") |
| | `gpio` | Yes | ADC-capable GPIO (32-39 on ESP32) |
| | `atten` | No | Attenuation: 0, 2.5, 6, 11 dB (default: 11) |
| `i2c_buses[]` | `id` | Yes | Bus ID ("i2c_0") |
| | `sda` / `scl` | Yes | I2C GPIO pins |
| | `freq_hz` | No | Clock frequency (default: 100000) |
| `i2c_expanders[]` | `id` | Yes | Expander ID ("relay_exp") |
| | `bus` | Yes | Reference to i2c_bus ID |
| | `chip` | Yes | "pcf8574" |
| | `address` | Yes | I2C address ("0x24") |
| | `pins` | Yes | Number of pins (8 for PCF8574) |

*gpio OR expander+pin — one of the two.

### Step 3: Write bindings.json — role mapping

`bindings.json` maps **logical roles** to **physical hardware + drivers**.

```json
{
  "manifest_version": 1,
  "bindings": [
    {"hardware": "ow_1",    "driver": "ds18b20", "role": "air_temp",   "module": "equipment"},
    {"hardware": "relay_1", "driver": "relay",   "role": "heater",     "module": "equipment"}
  ]
}
```

| Field | Description |
|-------|-------------|
| `hardware` | References `id` from board.json (e.g. "relay_1", "ow_1") |
| `driver` | Driver type: `relay`, `ds18b20`, `ntc`, `digital_input`, `pcf8574_relay`, `pcf8574_input` |
| `role` | Logical name for the module (e.g. "air_temp", "compressor", "heater") |
| `module` | Which module owns this driver (usually "equipment") |
| `address` | DS18B20 ROM address (optional — discovered via WebUI OneWire scan) |

### Step 4: Build

```bash
# Set board in sdkconfig
# CONFIG_MODESP_BOARD_DIR="my_board"

idf.py build
idf.py -p COM9 flash monitor
```

## Switching Boards

```bash
# Edit sdkconfig:
CONFIG_MODESP_BOARD_DIR="kc868a6"

# Rebuild — CMake copies new board.json + bindings.json
idf.py build
```

Same firmware code, different hardware — only JSON changes.

## Available Drivers

| Driver | Type | Hardware | Notes |
|--------|------|----------|-------|
| `ds18b20` | sensor | OneWire temperature | Auto SEARCH_ROM, 4.7K pull-up required |
| `ntc` | sensor | ADC NTC thermistor | B-parameter model, configurable R_series/R_nominal |
| `relay` | actuator | GPIO relay | Active high/low, optional min on/off time |
| `digital_input` | sensor | GPIO contact | Pull-up/down configurable, debounce |
| `pcf8574_relay` | actuator | I2C PCF8574 relay | For KC868-A6 and similar boards |
| `pcf8574_input` | sensor | I2C PCF8574 input | Opto-isolated on KC868-A6 |

## DS18B20 Address Discovery

DS18B20 sensors need a unique ROM address to identify them on the OneWire bus.

1. Flash firmware with empty `address` field in bindings.json
2. Open WebUI → Equipment → OneWire section
3. Click **Scan Bus** — discovered addresses appear
4. Select the correct address for each sensor
5. Address is saved to `bindings.json` in NVS

Or set address manually in bindings.json:
```json
{"hardware": "ow_1", "driver": "ds18b20", "role": "air_temp", "module": "equipment", "address": "28:8C:5E:45:D4:08:44:09"}
```

## Tips

- **GPIO 34-39** on ESP32 are input-only — cannot be used for relays
- **OneWire** needs 4.7KΩ pull-up resistor between DATA and 3.3V
- **ADC attenuation 11dB** gives 0-3.3V range (recommended for NTC)
- **I2C PCF8574** relays are typically active-low (`"active_low": true`)
- **Board name** from board.json appears in firmware info (`_ota.board` state key)
- Labels in board.json are used in WebUI Equipment page
