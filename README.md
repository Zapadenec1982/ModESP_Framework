# ModESP Framework

**Manifest-driven ESP32 firmware framework for industrial controllers**

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-blue?logo=espressif)](https://docs.espressif.com/projects/esp-idf/en/v5.5/)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)](https://isocpp.org/)
[![Svelte](https://img.shields.io/badge/Svelte-4-FF3E00?logo=svelte)](https://svelte.dev/)
[![License](https://img.shields.io/badge/License-Source%20Available-blue)](LICENSE)

> JSON manifests → code generator → firmware + WebUI + MQTT topics.
> Add a module manifest, rebuild — new parameters appear everywhere automatically.

---

## What Is This

ModESP Framework is the **reusable core** extracted from [ModESP v4](https://github.com/Zapadenec1982/ModESP_v4) — a production ESP32 refrigeration controller. The framework provides everything needed to build manifest-driven IoT controllers for any domain: HVAC, agriculture, industrial automation, smart home.

**You provide:** business logic modules (manifests + C++)
**Framework provides:** HAL, drivers, WebUI, MQTT, HTTP API, OTA, persistence, code generation

---

## Architecture

```
┌──────────────────────────────────────────────────────┐
│                  YOUR PRODUCT                         │
│  modules/thermostat/   modules/protection/   ...     │
│  (manifest.json + C++ module)                        │
├──────────────────────────────────────────────────────┤
│              ModESP FRAMEWORK                         │
│                                                      │
│  Equipment Manager ─── HAL ─── Drivers               │
│  SharedState ─── PersistService ─── NVS              │
│  WiFi ─── HTTP (REST) ─── WebSocket                  │
│  MQTT + TLS ─── OTA ─── DataLogger                   │
│  Code Generator ─── Svelte WebUI                     │
└──────────────────────────────────────────────────────┘
│              ESP-IDF v5.5 + FreeRTOS                  │
└──────────────────────────────────────────────────────┘
```

### Code Generation Pipeline

```
 module manifests ──┐
 driver manifests ──┼──▶  generate_ui.py  ──▶  ui.json        (WebUI schema)
 board.json        ──┤                     ──▶  state_meta.h   (C++ metadata)
 bindings.json     ──┘                     ──▶  mqtt_topics.h  (pub/sub arrays)
                                           ──▶  features_config.h
                                           ──▶  i18n packs     (4 languages)
```

### Equipment Base + Product Override

```
Framework (EquipmentBase):                   Product (your override):
  ├── bind_drivers()  ← generic              ├── apply_arbitration() ← YOUR LOGIC
  ├── read_sensors()  ← EMA + publish        │   ├── Priority rules
  ├── apply_outputs() ← set relays           │   ├── Interlocks
  └── publish_states()← actual states        │   └── Anti-short-cycle
```

```cpp
class MyEquipment : public EquipmentBase {
protected:
    void apply_arbitration(uint32_t dt_ms) override {
        bool lockout = read_bool("protection.lockout");
        if (lockout) { set_actuator("pump", false); return; }
        set_actuator("pump", read_bool("controller.req.pump"));
    }
};
```

Framework provides driver binding, sensor reading, state publishing.
Product provides arbitration logic — **business logic belongs in C++, not JSON**.

---

## Framework Components

| Component | Path | Description |
|-----------|------|-------------|
| **modesp_core** | `components/modesp_core/` | BaseModule, ModuleManager, SharedState, App |
| **modesp_hal** | `components/modesp_hal/` | HAL, DriverManager, driver interfaces |
| **modesp_services** | `components/modesp_services/` | Config, Persist, Error, Watchdog, Logger, NVS helpers |
| **modesp_net** | `components/modesp_net/` | WiFi (STA+AP), HTTP (REST), WebSocket |
| **modesp_mqtt** | `components/modesp_mqtt/` | MQTT + TLS, delta-publish, heartbeat, LWT, OTA |
| **modesp_aws** | `components/modesp_aws/` | AWS IoT Core (mTLS, Shadow, Jobs) — optional |
| **modesp_ble** | `components/modesp_ble/` | BLE transport (NimBLE): observer scan + central-connect, per-transport decoders, runtime device registry |
| **modesp_scenario** | `components/modesp_scenario/` | ISA-88 recipe/scenario engine — compiled `.modr` state machines, tracks, arbitration |
| **Equipment** | `components/modesp_equipment/` (base) + `modules/equipment/` (demo) | HAL owner, arbitration, interlocks |
| **DataLogger** | `modules/datalogger/` | Manifest-driven temperature + event logging, LittleFS, SVG chart |
| **Generator** | `tools/generate_ui.py` | Manifest → 7 artifacts + i18n validation |
| **WebUI** | `webui/` | Svelte 4 SPA, 24 widget types, dark/light theme |

### Included Drivers

A role never names a driver — it declares a **capability** (`temperature`, `relay_out`, `display`…),
and any driver that provides that capability can fill it, over any transport. See
[rules.md R0.1](documentation/uk/03-framework-reference/rules.md). 13 drivers ship today:

| Driver | Capability | Transport | Description |
|--------|-----------|-----------|-------------|
| `ds18b20` | `temperature` | wired · OneWire | Dallas temperature, scan-assigned ROM |
| `ntc` | `temperature` | wired · ADC | NTC thermistor (B-parameter model) |
| `ld2410b` | `presence`/`distance`/`energy` | wired · UART | mmWave presence radar |
| `digital_input` | `binary_in` | wired · GPIO | Contact input (door, switch), SW debounce |
| `pcf8574_input` | `binary_in` | wired · I2C | PCF8574 digital input |
| `relay` | `relay_out` | wired · GPIO | GPIO relay, min on/off protection |
| `pcf8574_relay` | `relay_out` | wired · I2C | PCF8574 relay (e.g. KC868-A6) |
| `amt630a` | `display` | wired · I2C | AMT630A OSD/menu display |
| `at7456e` | `display` | wired · SPI | AT7456E OSD/menu display |
| `max98357a` | `audio` | wired · I2S | I2S audio amplifier / DAC |
| `ble_xiaomi_th` | `temperature`/`humidity`/`battery` | **BLE** · observer | Xiaomi / BTHome sensor |
| `ble_nrf_tilt` | `angle`/`accel`/`battery` | **BLE** · observer | nRF52 tilt / orientation beacon |
| `ble_led_panel` | `panel` | **BLE** · connect | iPixel LED matrix |

**Remote (BLE) devices** are not hardcoded in `board.json` — the operator scans + subscribes them at
runtime on the **Devices** page (`/data/devices.json`); roles then bind to them by device id, exactly
like a wired sensor, with no MAC on the role. The registry is transport-generic
(`RemoteDeviceConfig{transport, identity, name}`) — LoRa/MQTT/ESP-NOW are future transports, not
rewrites. See [ble_xiaomi_th](documentation/en/03-framework-reference/drivers/ble_xiaomi_th.md).

---

## Creating Your Product

### 1. Create a module manifest

```
modules/your_module/manifest.json
```

```json
{
  "module": "your_module",
  "version": "1.0.0",
  "state": {
    "your_module.temperature": {
      "type": "float", "access": "ro",
      "description": "Current temperature", "unit": "°C"
    },
    "your_module.setpoint": {
      "type": "float", "access": "rw", "default": 20.0,
      "min": -30, "max": 50, "step": 0.5,
      "description": "Target temperature", "unit": "°C",
      "persist": true, "mqtt_subscribe": true
    }
  },
  "ui": {
    "page": "Your Module",
    "icon": "thermometer",
    "cards": [
      {
        "title": "Status",
        "widgets": [
          {"key": "your_module.temperature", "type": "value"},
          {"key": "your_module.setpoint", "type": "slider"}
        ]
      }
    ]
  }
}
```

### 2. Create C++ module

```cpp
// modules/your_module/src/your_module.cpp
#include "modesp/base_module.h"

class YourModule : public modesp::BaseModule {
public:
    YourModule() : BaseModule("your_module", 2) {}  // name, priority

    void on_init(modesp::SharedState& state) override {
        // Read persisted settings, init hardware references
    }

    void on_update(uint32_t dt_ms) override {
        float temp = read_float("equipment.air_temp");
        float sp = read_float("your_module.setpoint");
        // Your business logic here
        state_set("your_module.temperature", temp);
    }
};
```

### 3. Add to project.json

```json
{
  "modules": ["equipment", "datalogger", "your_module"]
}
```

The generator auto-creates includes, instances, registration code and the
CMake dependency list (`generated/modules.cmake` → `main/CMakeLists.txt`) —
no manual CMake edits.

### 4. Build

```bash
idf.py build    # Generator runs automatically → includes, instances, registration
idf.py -p COM9 flash monitor
```

Your module's parameters appear in WebUI, MQTT, state engine, and NVS persistence — automatically.

### Naming Convention

Generator maps module name → C++ class automatically:

| Module name | Header | Class | Instance |
|-------------|--------|-------|----------|
| `thermostat` | `thermostat_module.h` | `ThermostatModule` | `thermostat` |
| `your_module` | `your_module_module.h` | `YourModuleModule` | `your_module` |

For non-standard names, add `"class_name": "MyClass"` to manifest.json.

### Module Naming Rules

- Only `a-z`, `0-9`, `_` (valid C++ identifier)
- Must start with a letter: `my_module`, not `2module`
- No hyphens: `heat_pump`, not `heat-pump`
- Lowercase only: `thermostat`, not `Thermostat`
- Must match folder name: `modules/thermostat/` → `"thermostat"`
- State keys prefixed with module name: `thermostat.setpoint`

### Adding / Removing Modules

```bash
# Add:
# 1. Add module name to project.json
# 2. Add module name to main/CMakeLists.txt PRIV_REQUIRES
# 3. idf.py build

# Remove:
# 1. Remove from project.json + main/CMakeLists.txt PRIV_REQUIRES
# 2. Delete modules/xxx/
# 3. idf.py build
```

### Adding a New Driver

Drivers are auto-discovered from `drivers/` directory. No project.json changes needed.

```
1. Create drivers/my_sensor/
   ├── CMakeLists.txt         # via modesp_driver_component() → auto Kconfig toggle
   ├── manifest.json          # capability, hardware_type, transport, settings
   ├── include/my_sensor_driver.h
   └── src/my_sensor_driver.cpp  # factory + MODESP_REGISTER_SENSOR(my_sensor, &factory)

2. Use in bindings.json (a role that declares the same capability picks it up):
   {"hardware": "adc_1", "driver": "my_sensor", "role": "pressure", "module": "equipment"}

3. idf.py build
```

Drivers are a **library** — every driver is compiled (menuconfig can gate optional ones),
`bindings.json` selects which are active, and a role matches by **capability**, not by driver
name. Full guide: [writing-a-driver](documentation/en/02-module-author-guide/writing-a-driver.md).

### DataLogger Integration

Modules declare what they can provide for logging via `loggable` section in manifest:

```json
// modules/your_module/manifest.json
{
  "loggable": {
    "channels": {
      "your_module.temperature": {
        "type": "temperature",
        "label": "Process temperature",
        "default": true
      }
    },
    "events": {
      "your_module.alarm": {
        "id": 20,
        "edge": "rising",
        "label": "Process alarm"
      }
    }
  }
}
```

Generator collects all `loggable` sections → produces `datalogger_channels.h` + `datalogger_events.h`. DataLogger reads generated tables — zero hardcoded state keys.

- **Channels**: temperature values sampled periodically, toggleable in WebUI Settings
- **Events**: edge-detect on bool state keys, logged with explicit IDs (stable across builds)
- **Adding logging** = add `loggable` to your module manifest → rebuild → appears in DataLogger

---

## Board Configuration

One firmware codebase — different hardware via JSON config:

```
data/
├── board.json       # PCB definition: GPIO pins, buses, expanders
└── bindings.json    # Role mapping: air_temp → DS18B20 on GPIO 15
```

| Board | Use Case |
|-------|----------|
| ESP32-DevKit | Development, direct GPIO relays + OneWire |
| KC868-A6 | Production, I2C PCF8574 relays + 4 ADC + RS-485 |
| Custom PCB | Your hardware — define in board.json |

Switch board → rebuild → same firmware runs on different hardware.

**Full guide:** [documentation/en/04-hardware/board-config.md](documentation/en/04-hardware/board-config.md) — schema reference, board examples, GPIO/I2C/OneWire/ADC bus declarations. Bilingual ([UK](documentation/uk/04-hardware/board-config.md)).

---

## Key Features

| Feature | Details |
|---------|---------|
| **Manifest-driven** | JSON → generates UI, metadata, MQTT, features at build time |
| **Role = capability** | A role declares a capability (`temperature`, `relay_out`, `display`…), never a driver — any source (wired / BLE / future LoRa·MQTT) fills it; sensors **and** actuators |
| **Transport-generic devices** | `RemoteDeviceConfig{transport, identity, name}`; BLE observer + connect today, subscribed at runtime on the Devices page; identity lives on the device, never on the role |
| **Zero heap in hot path** | ETL containers (etl::string, etl::vector), no std::string/new/malloc |
| **SharedState** | Typed key-value store (~69 keys), compile-time metadata |
| **Equipment arbitration** | Priority-based relay control, safety interlocks |
| **WebUI** | Svelte 4, dark/light, 4 languages, WebSocket real-time, Bindings + Devices pages |
| **MQTT + TLS** | Delta-publish, heartbeat, LWT, tenant-aware topics, HA discovery |
| **HTTP REST** | 35 endpoints: state, settings, WiFi, OTA, logs, bindings, devices, BLE scan |
| **OTA** | Dual partition, SHA-256 check, board compatibility, auto-rollback |
| **DataLogger** | Manifest-driven channels + events, LittleFS, SVG chart, CSV export |
| **i18n** | 4 languages (UK/EN/DE/PL), lazy-load packs, add language = JSON only |
| **WiFi** | STA + AP fallback, AP→STA probe, mDNS, STA watchdog |
| **Cloud** | MQTT broker or AWS IoT Core (compile-time Kconfig switch) |

---

## Project Structure

```
components/
├── modesp_core/        # BaseModule, ModuleManager, SharedState
├── modesp_services/    # Config, Persist, Error, Watchdog, Logger, NVS
├── modesp_hal/         # HAL, DriverManager, driver interfaces, RemoteDeviceConfig
├── modesp_equipment/   # EquipmentBase (arbitration seam)
├── modesp_net/         # WiFi, HTTP (35 endpoints), WebSocket
├── modesp_mqtt/        # MQTT + TLS, delta-publish, OTA handler
├── modesp_aws/         # AWS IoT Core (optional)
├── modesp_ble/         # BLE transport: observer scan + connect, device registry
└── modesp_scenario/    # ISA-88 recipe/scenario engine (.modr)
modules/                # 8 modules (equipment, datalogger, display, panel,
│                       #  player, presence, simple_thermo, abs_test)
drivers/                # 13 drivers — wired (temp/relay/input/display/audio)
│                       #  + BLE (ble_xiaomi_th, ble_nrf_tilt, ble_led_panel)
tools/
├── generate_ui.py      # Manifests + capabilities.json → ui.json/headers/i18n
├── capabilities.json   # SSOT capability vocabulary
└── tests/              # pytest (incl. test_docs_coverage doc-lint)
webui/                  # Svelte 4 SPA (Bindings + Devices pages, widgets)
tests/host/             # C++ doctest (host-compiled)
```

---

## Documentation

> 📖 **Docs:** [`documentation/`](documentation/README.md) — bilingual
> (EN + UK), written to a single quality standard
> ([STYLE.md](documentation/STYLE.md)). ~150 pages covering everything from
> the 10-minute quickstart to ADRs of the scenario engine.
>
> 📜 **CHANGELOG:** [EN](documentation/en/CHANGELOG.md) · [UK](documentation/uk/CHANGELOG.md).

### Start here

| Path | What you get |
|---|---|
| ⭐ [rules.md](documentation/uk/03-framework-reference/rules.md) | **The framework's rules** — one numbered index (R0 role=capability … R9 docs) of what not to break. A doc-lint keeps every driver/module documented. |
| [documentation/en/01-getting-started/quickstart.md](documentation/en/01-getting-started/quickstart.md) | Flash, configure, run the reference scenario in under 10 minutes |
| [documentation/en/01-getting-started/installation.md](documentation/en/01-getting-started/installation.md) | ESP-IDF toolchain, repo clone, first build |
| [documentation/en/01-getting-started/concepts.md](documentation/en/01-getting-started/concepts.md) | Four mental models — manifests, modules/drivers, SharedState, scenarios |
| [documentation/en/02-module-author-guide/overview.md](documentation/en/02-module-author-guide/overview.md) | Module Author Guide entry — anatomy of a module folder |
| [documentation/uk/](documentation/uk/README.md) | Українська версія (mirror) |

### Reference

| Section | Content |
|---|---|
| [Module Author Guide](documentation/en/02-module-author-guide/) | 13 pages: manifest schema, writing modules/drivers, SharedState, UI widgets, MQTT, persistence, recipe authoring, continuous behaviors, debugging, best practices |
| [Framework Reference](documentation/en/03-framework-reference/) | ⭐ [rules.md](documentation/en/03-framework-reference/rules.md) + [project-hierarchy](documentation/en/03-framework-reference/project-hierarchy.md) + [capability-roadmap](documentation/uk/03-framework-reference/capability-roadmap.md) + architecture + component pages + **8 module** + **13 driver** reference pages (one per component, doc-lint enforced) |
| [Scenario Engine deep dive](documentation/en/03-framework-reference/scenario-engine/) | 11 architectural docs + 8 ADRs + 3 usage guides + 2 worked examples |
| [Hardware](documentation/en/04-hardware/) | `board.json` schema, `bindings.json`, OTA flow, deployment |
| [Tools](documentation/en/05-tools/) | `generate_ui.py`, `compile_scenario.py`, `dump_modr.py` |
| [Contributing](documentation/en/06-contributing/) | Development setup, host + HIL testing, C++ style, docs style |

---

## Quick Start

```bash
# Prerequisites: ESP-IDF v5.5, Python 3.8+, Node.js 18+

# Build (generator runs automatically via CMake)
idf.py build

# Flash
idf.py -p /dev/ttyUSB0 flash monitor

# Build WebUI (optional — pre-built bundle included)
cd webui && npm install && npm run build && npm run deploy
```

---

## Product Example

See [ModESP v4](https://github.com/Zapadenec1982/ModESP_v4) — a complete refrigeration controller product built on this framework:
- 5 business modules (thermostat, defrost, protection + equipment, datalogger)
- 126 state keys, 491 tests
- Cloud integration via [ModESP Cloud](https://github.com/Zapadenec1982/ModESP_Cloud)

---

## License

**Source-available** under [PolyForm Noncommercial License 1.0.0](LICENSE).

Free to use, study, and modify for personal and non-commercial purposes.
Commercial licensing available — contact [tepliuk.yurii@gmail.com](mailto:tepliuk.yurii@gmail.com).

---

## Author

**Yurii Tepliuk** — Embedded Systems Engineer, Ukraine

- ESP-IDF / FreeRTOS / C++17 / ETL — zero-heap embedded development
- Full-stack IoT: firmware → MQTT → cloud → Svelte WebUI

[![GitHub](https://img.shields.io/badge/GitHub-Zapadenec1982-181717?logo=github)](https://github.com/Zapadenec1982)
