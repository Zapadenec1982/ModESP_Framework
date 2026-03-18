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

### Equipment Arbitration

```
  Your Module A        Your Module B       Protection
      │                    │                   │
      │ req.compressor     │ req.relay_2       │ lockout / block
      ▼                    ▼                   ▼
  ┌──────────────────────────────────────────────────┐
  │           Equipment Manager                       │
  │  Priority: Protection > Module B > Module A       │
  │  Reads requests from SharedState                  │
  │  Drives hardware through HAL                      │
  └──────────────────────────────────────────────────┘
```

Modules publish requests to SharedState. Equipment Manager arbitrates and drives hardware. Modules never touch GPIO directly.

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
| **Equipment** | `modules/equipment/` | HAL owner, arbitration, interlocks |
| **DataLogger** | `modules/datalogger/` | Temperature + event logging, LittleFS, SVG chart |
| **Generator** | `tools/generate_ui.py` | Manifest → 5 artifacts + i18n validation |
| **WebUI** | `webui/` | Svelte 4 SPA, 24 widget types, dark/light theme |

### Included Drivers

| Driver | Type | Description |
|--------|------|-------------|
| `ds18b20` | Sensor | Dallas OneWire temperature (auto SEARCH_ROM) |
| `ntc` | Sensor | NTC thermistor via ADC (B-parameter model) |
| `relay` | Actuator | GPIO relay with min on/off protection |
| `digital_input` | Sensor | GPIO contact (door, switch) |
| `pcf8574_relay` | Actuator | I2C PCF8574 relay (e.g. KC868-A6) |
| `pcf8574_input` | Sensor | I2C PCF8574 digital input |

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

### 3. Register in main.cpp

```cpp
#include "your_module.h"
static YourModule your_module;
// ...
app.modules().register_module(your_module);
```

### 4. Add to project.json

```json
{
  "modules": ["equipment", "datalogger", "your_module"]
}
```

### 5. Build

```bash
idf.py build    # Generator runs automatically
idf.py -p COM9 flash monitor
```

Your module's parameters appear in WebUI, MQTT, state engine, and NVS persistence — automatically.

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

---

## Key Features

| Feature | Details |
|---------|---------|
| **Manifest-driven** | JSON → generates UI, metadata, MQTT, features at build time |
| **Zero heap in hot path** | ETL containers (etl::string, etl::vector), no std::string/new/malloc |
| **SharedState** | Typed key-value store, 34+ keys, compile-time metadata |
| **Equipment arbitration** | Priority-based relay control, safety interlocks |
| **WebUI** | Svelte 4, 80KB gzip, dark/light, 4 languages, WebSocket real-time |
| **MQTT + TLS** | Delta-publish, heartbeat, LWT, tenant-aware topics, HA discovery |
| **HTTP REST** | 23 endpoints: state, settings, WiFi, OTA, logs, backup/restore |
| **OTA** | Dual partition, SHA-256 check, board compatibility, auto-rollback |
| **DataLogger** | 6-ch temperature, 18 event types, LittleFS, SVG chart, CSV export |
| **i18n** | 4 languages (UK/EN/DE/PL), lazy-load packs, add language = JSON only |
| **WiFi** | STA + AP fallback, AP→STA probe, mDNS, STA watchdog |
| **Cloud** | MQTT broker or AWS IoT Core (compile-time Kconfig switch) |

---

## Project Structure

```
components/
├── modesp_core/        # BaseModule, ModuleManager, SharedState
├── modesp_services/    # Config, Persist, Error, Watchdog, Logger, NVS
├── modesp_hal/         # HAL, DriverManager, driver interfaces
├── modesp_net/         # WiFi, HTTP (23 endpoints), WebSocket
├── modesp_mqtt/        # MQTT + TLS, delta-publish, OTA handler
├── modesp_aws/         # AWS IoT Core (optional)
└── modesp_json/        # jsmn JSON parser
modules/
├── equipment/          # HAL owner, arbitration (framework)
└── datalogger/         # Temperature + event logging (framework)
drivers/
├── ds18b20/            # Dallas OneWire temperature
├── ntc/                # NTC thermistor via ADC
├── relay/              # GPIO relay
├── digital_input/      # GPIO contact input
├── pcf8574_relay/      # I2C relay (KC868-A6)
└── pcf8574_input/      # I2C input (KC868-A6)
tools/
├── generate_ui.py      # Manifest → 5 artifacts + i18n
└── tests/              # pytest
webui/                  # Svelte 4 SPA (24 widget components)
tests/host/             # C++ doctest (host-compiled)
```

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
