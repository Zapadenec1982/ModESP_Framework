# Tutorial: Creating Your First Module

This tutorial walks through creating a simple ON/OFF thermostat module from scratch.
By the end, you'll have a working module with WebUI, MQTT, DataLogger integration, and 4-language i18n — all from a single manifest.json.

## Prerequisites

- ModESP Framework cloned and building (`idf.py build` succeeds)
- ESP32 board with at least 1 temperature sensor and 1 relay

## What We're Building

A simple heating thermostat:
- Reads temperature from Equipment Manager
- Turns relay ON when temp drops below (setpoint - differential)
- Turns relay OFF when temp reaches setpoint
- WebUI page with temperature display, setpoint slider, state indicator
- MQTT publish/subscribe for remote monitoring
- DataLogger integration for temperature history

## Step 1: Create Module Directory

```
modules/simple_thermo/
├── CMakeLists.txt
├── manifest.json
├── include/
│   └── simple_thermo_module.h
├── src/
│   └── simple_thermo_module.cpp
└── i18n/
    ├── en.json
    ├── de.json
    └── pl.json
```

## Step 2: Write manifest.json

The manifest is the **single source of truth** for your module. It defines:
- State keys (parameters)
- UI layout (pages, cards, widgets)
- MQTT topics
- DataLogger channels and events
- i18n labels (Ukrainian default in descriptions)

```json
{
  "manifest_version": 1,
  "module": "simple_thermo",
  "version": "1.0.0",
  "description": "Simple ON/OFF thermostat — demo module",
  "priority": 2,

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
      "min": 5, "max": 40, "step": 0.5,
      "unit": "°C",
      "persist": true,
      "mqtt_subscribe": true,
      "description": "Уставка температури"
    },
    "simple_thermo.differential": {
      "type": "float",
      "access": "readwrite",
      "default": 1.0,
      "min": 0.5, "max": 5.0, "step": 0.5,
      "unit": "°C",
      "persist": true,
      "mqtt_subscribe": true,
      "description": "Диференціал (гістерезис)"
    },
    "simple_thermo.state": {
      "type": "string",
      "access": "read",
      "description": "Стан модуля"
    },
    "simple_thermo.output": {
      "type": "bool",
      "access": "read",
      "description": "Запит на реле",
      "on_label": "ON",
      "off_label": "OFF"
    }
  },

  "mqtt": {
    "publish": ["simple_thermo.temperature", "simple_thermo.state", "simple_thermo.output"],
    "subscribe": ["simple_thermo.setpoint", "simple_thermo.differential"]
  },

  "loggable": {
    "channels": {
      "simple_thermo.temperature": {
        "type": "temperature",
        "label": "Температура",
        "default": true
      }
    },
    "events": {
      "simple_thermo.output": {
        "id": 30,
        "edge": "both",
        "label_on": "Нагрів ON",
        "label_off": "Нагрів OFF"
      }
    }
  },

  "ui": {
    "page": "Термостат",
    "icon": "thermometer",
    "cards": [
      {
        "title": "Стан",
        "subtitle": "Температура, режим",
        "widgets": [
          {"key": "simple_thermo.temperature", "widget": "value"},
          {"key": "simple_thermo.state", "widget": "value"},
          {"key": "simple_thermo.output", "widget": "indicator"}
        ]
      },
      {
        "title": "Налаштування",
        "subtitle": "Уставка, диференціал",
        "widgets": [
          {"key": "simple_thermo.setpoint", "widget": "slider"},
          {"key": "simple_thermo.differential", "widget": "number_input"}
        ]
      }
    ]
  }
}
```

### Key Concepts

- **`access: "read"`** — read-only, published by firmware
- **`access: "readwrite"`** — user can change via WebUI/MQTT
- **`persist: true`** — saved to NVS, survives reboot
- **`mqtt_subscribe: true`** — accepts remote commands
- **`widget: "slider"`** — uses min/max/step from state definition
- **`loggable`** — DataLogger automatically samples this value

## Step 3: Write C++ Module

### Header (include/simple_thermo_module.h)

```cpp
#pragma once
#include "modesp/base_module.h"

class SimpleThermoModule : public modesp::BaseModule {
public:
    SimpleThermoModule();
    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

private:
    bool heating_ = false;
};
```

### Implementation (src/simple_thermo_module.cpp)

```cpp
#include "simple_thermo_module.h"
#include "esp_log.h"

static const char* TAG = "SimpleThermo";

SimpleThermoModule::SimpleThermoModule()
    : BaseModule("simple_thermo", 2)  // name must match manifest "module"
{}

bool SimpleThermoModule::on_init() {
    state_set("simple_thermo.temperature", 0.0f);
    state_set("simple_thermo.state", "off");
    state_set("simple_thermo.output", false);

    float sp = read_float("simple_thermo.setpoint", 22.0f);
    ESP_LOGI(TAG, "Initialized (setpoint=%.1f°C)", sp);
    return true;
}

void SimpleThermoModule::on_update(uint32_t dt_ms) {
    (void)dt_ms;

    // Read temperature from Equipment Manager (published by EquipmentBase)
    float temp = read_float("equipment.air_temp", 0.0f);
    state_set("simple_thermo.temperature", temp);

    // Read settings (persisted in NVS, changeable via WebUI/MQTT)
    float setpoint = read_float("simple_thermo.setpoint", 22.0f);
    float diff = read_float("simple_thermo.differential", 1.0f);

    // ON/OFF hysteresis
    if (heating_) {
        if (temp >= setpoint) {
            heating_ = false;
            ESP_LOGI(TAG, "OFF (temp=%.1f >= setpoint=%.1f)", temp, setpoint);
        }
    } else {
        if (temp < (setpoint - diff)) {
            heating_ = true;
            ESP_LOGI(TAG, "ON (temp=%.1f < %.1f)", temp, setpoint - diff);
        }
    }

    // Publish — Equipment Manager reads this and controls the relay
    state_set("simple_thermo.output", heating_);
    state_set("simple_thermo.state", heating_ ? "heating" : "idle");
}
```

### Key Patterns

- **`read_float("key", default)`** — reads from SharedState (set by persist, MQTT, or other modules)
- **`state_set("key", value)`** — publishes to SharedState (triggers WebUI, MQTT, DataLogger)
- Module **never touches GPIO** — Equipment Manager handles relay hardware
- Module name in constructor **must match** manifest `"module"` field

## Step 4: CMakeLists.txt

```cmake
idf_component_register(
    SRCS "src/simple_thermo_module.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core
)
```

## Step 5: Add Translations (i18n)

Create `i18n/en.json`:

```json
{
  "state.simple_thermo.temperature.description": "Current temperature",
  "state.simple_thermo.setpoint.description": "Temperature setpoint",
  "state.simple_thermo.differential.description": "Differential (hysteresis)",
  "state.simple_thermo.state.description": "Module state",
  "state.simple_thermo.output.description": "Relay request",
  "page.simple_thermo.title": "Thermostat",
  "card.simple_thermo.card0.title": "Status",
  "card.simple_thermo.card1.title": "Settings"
}
```

Ukrainian is default (from manifest descriptions). Add DE, PL files for other languages.

## Step 6: Register in project.json

```json
{
  "modules": ["equipment", "datalogger", "simple_thermo"]
}
```

That's it. No changes to `main.cpp` or `CMakeLists.txt` — auto-registration handles everything.

## Step 7: Build and Flash

```bash
idf.py build      # Generator runs: manifest → ui.json + headers + i18n
idf.py -p COM9 flash monitor
```

## What You Get Automatically

From a single manifest.json, the framework generates:

| Feature | How |
|---------|-----|
| **WebUI page** "Термостат" with slider + indicator | `ui.json` from manifest `ui` section |
| **MQTT publish** temperature + state | `mqtt_topics.h` from manifest `mqtt` section |
| **MQTT subscribe** setpoint + differential | `mqtt_topics.h` + STATE_META validation |
| **NVS persistence** setpoint + differential | `state_meta.h` from `persist: true` |
| **DataLogger** temperature channel + output events | `datalogger_channels.h` from `loggable` |
| **4-language WebUI** UK/EN/DE/PL | `i18n/*.json` merged into language packs |
| **Auto-registration** include + instance + register | `module_includes.h` from `project.json` |

**Zero manual wiring between firmware, WebUI, and cloud.**

## Next Steps

- Add Equipment override for your product (see `EquipmentBase` docs)
- Add more state keys (min/max on time, startup delay)
- Add Protection module for alarms
- Connect to [ModESP Cloud](https://github.com/Zapadenec1982/ModESP_Cloud) for remote monitoring
