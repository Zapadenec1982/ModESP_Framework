# ModESP Framework — System Architecture

> Universal manifest-driven firmware framework for industrial ESP32 controllers.
> Provides HAL, drivers, code generation, WebUI, MQTT, OTA — product adds business logic modules.

## System Overview

```
┌──────────────────────────────────────────────────────┐
│                  PRODUCT LAYER                        │
│  modules/your_module/ (manifest.json + C++ logic)    │
├──────────────────────────────────────────────────────┤
│              FRAMEWORK LAYER                          │
│                                                      │
│  EquipmentBase ─── HAL ─── Drivers (6 types)         │
│  SharedState ─── PersistService ─── NVS              │
│  WiFi ─── HTTP (23 endpoints) ─── WebSocket          │
│  MQTT + TLS ─── OTA ─── DataLogger                   │
│  Code Generator ─── Svelte WebUI (24 widgets)        │
├──────────────────────────────────────────────────────┤
│              PLATFORM LAYER                           │
│  ESP-IDF v5.5 ─── FreeRTOS ─── LittleFS             │
└──────────────────────────────────────────────────────┘
```

## Code Generation Pipeline

```
module manifests ──┐
driver manifests ──┼──▶  generate_ui.py  ──▶  ui.json          (WebUI schema)
board.json        ──┤                     ──▶  state_meta.h     (C++ metadata)
bindings.json     ──┤                     ──▶  mqtt_topics.h    (pub/sub arrays)
project.json      ──┘                     ──▶  features_config.h
                                          ──▶  module_includes.h (auto-registration)
                                          ──▶  module_instances.h
                                          ──▶  module_register.h
                                          ──▶  modules.cmake
                                          ──▶  datalogger_channels.h
                                          ──▶  datalogger_events.h
                                          ──▶  i18n language packs (4 languages)
```

## Core Components

### SharedState
Typed key-value store. All inter-module communication goes through SharedState.
Modules never access each other directly — only read/write state keys.

### EquipmentBase
Universal HAL owner. Binds drivers by role name, reads sensors with EMA filter,
publishes state, applies actuator outputs.

Product creates: `class MyEquipment : public EquipmentBase`
Override: `apply_arbitration(uint32_t dt_ms)` — business-specific priority logic.

### DataLogger
Manifest-driven temperature + event logging.
- `loggable.channels` in module manifests → temperature sampling
- `loggable.events` with explicit IDs → edge-detect polling
- LittleFS storage, streaming JSON API, SVG chart, CSV export

### ModuleManager
Manages lifecycle: register → init → update → stop.
Modules sorted by priority (lower = earlier in update loop).

## Module System

### Creating a Module
1. `modules/xxx/manifest.json` — state keys, UI, MQTT, loggable
2. `modules/xxx/src/xxx_module.cpp` — C++ business logic (extends BaseModule)
3. Add to `project.json` + `main/CMakeLists.txt` PRIV_REQUIRES
4. `idf.py build` — auto-registration via generated headers

### Naming Convention
Module `"heat_pump"` → class `HeatPumpModule`, header `heat_pump_module.h`

### Module Priority
- 0 = CRITICAL (Equipment)
- 1 = HIGH (Protection, WiFi)
- 2 = NORMAL (business modules)
- 3 = LOW (DataLogger, HTTP, WS)

## Driver System

6 driver types: ds18b20, ntc, relay, digital_input, pcf8574_relay, pcf8574_input.
Auto-discovered from `drivers/` directory. `bindings.json` maps roles to drivers.

## Board Abstraction

`board.json` — physical hardware (GPIO, I2C, OneWire, ADC).
`bindings.json` — logical role → driver + hardware mapping.
Switch board → rebuild → same code runs on different hardware.

## WebUI

Svelte 4 SPA, 24 widget types, dark/light theme, 4 languages (UK/EN/DE/PL).
Dashboard renders dynamically from ui.json — no hardcoded product logic.
Language packs lazy-loaded from LittleFS.

## Connectivity

- WiFi STA + AP fallback, AP→STA probe, mDNS
- HTTP REST (23 endpoints), WebSocket (real-time state)
- MQTT + TLS, delta-publish, heartbeat, LWT
- OTA dual-partition with rollback + board check
- Optional: AWS IoT Core (compile-time Kconfig switch)
