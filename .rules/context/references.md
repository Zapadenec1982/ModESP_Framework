# References: API, State Keys, Project Structure

## HTTP API (port 80)

| Endpoint | Method | Description |
|----------|--------|-------------|
| /api/state | GET | Full SharedState as JSON |
| /api/ui | GET | UI schema (ui.json) |
| /api/board | GET | Board config |
| /api/bindings | GET/POST | Driver bindings (read / save) |
| /api/modules | GET | Module list + status |
| /api/settings | POST | Change readwrite state keys (validates via state_meta) |
| /api/mqtt | GET/POST | MQTT config + status / update (registered by MqttService) |
| /api/wifi | POST | WiFi credentials |
| /api/wifi/scan | GET | WiFi scan results |
| /api/wifi/ap | GET/POST | WiFi AP mode settings |
| /api/time | GET/POST | Current time / NTP settings |
| /api/ota | GET | Firmware version/partition info |
| /api/ota | POST | OTA firmware upload (.bin) |
| /api/onewire/scan | GET | Scan OneWire bus (SEARCH_ROM) |
| /api/log | GET | DataLogger: streaming chunked JSON (?hours=24) |
| /api/log/summary | GET | DataLogger: {hours, temp_count, event_count, flash_kb} |
| /api/backup | GET | Backup configuration (NVS export) |
| /api/restore | POST | Restore configuration (NVS import) |
| /api/auth | GET/POST | Auth settings (read / update) |
| /api/cloud | GET/POST | AWS IoT Core config + cert upload (CONFIG_MODESP_CLOUD_AWS only) |
| /api/factory-reset | POST | Factory reset (NVS clear + restart) |
| /api/restart | POST | ESP restart |
| /ws | WS | Real-time state broadcast (delta, 1500ms) |

## WebSocket Protocol

- Delta broadcasts: only changed keys (~200B vs 3.5KB full)
- Broadcast interval: 1500ms
- Heartbeat PING: every 20s
- Max 3 concurrent clients
- New client gets full state on connect

## MQTT Topics

- Tenant prefix: `modesp/v1/{tenant}/{device_id}/`
- HA Auto-Discovery: `homeassistant/sensor/...`
- LWT: "offline" (retain), heartbeat every 60s
- Delta-publish cache: 16 entries, only changed values
- Alarm republish: retain + QoS1 every 5 min

## Project Structure

```
ModESP_v4/
├── main/main.cpp              # Boot sequence, main loop
├── components/
│   ├── modesp_core/           # BaseModule, ModuleManager, SharedState, types.h
│   ├── modesp_services/       # ErrorService, WatchdogService, Logger, Config, NVS, PersistService
│   ├── modesp_hal/            # HAL, DriverManager, driver interfaces
│   ├── modesp_net/            # WiFiService, HttpService, WsService
│   ├── modesp_mqtt/           # MqttService
│   ├── modesp_json/           # JSON helpers
│   └── jsmn/                  # Lightweight JSON parser (header-only)
├── drivers/                   # 6 drivers (ds18b20, ntc, digital_input, relay, pcf8574_relay, pcf8574_input)
├── modules/                   # 5 modules (equipment, protection, thermostat, defrost, datalogger)
├── tools/
│   ├── generate_ui.py         # Manifest → UI + C++ headers (~1677 lines)
│   └── tests/                 # 310 pytest tests
├── tests/host/                # 108 doctest C++ tests
├── data/
│   ├── board.json             # Active board (KC868-A6)
│   ├── bindings.json          # Active driver bindings
│   ├── ui.json                # GENERATED
│   └── www/                   # Deployed WebUI
├── webui/                     # Svelte 4 source
├── generated/                 # GENERATED C++ headers (5 files)
├── docs/                      # Architecture docs
├── project.json               # Active modules list
└── partitions.csv             # NVS(24K) + app(1.5MB) + data/LittleFS(384K)
```

## Editable vs Generated

| Editable (Source of Truth) | Generated (DO NOT EDIT) |
|---|---|
| `modules/*/manifest.json` | `data/ui.json` |
| `drivers/*/manifest.json` | `generated/state_meta.h` |
| `data/board.json` | `generated/mqtt_topics.h` |
| `data/bindings.json` | `generated/display_screens.h` |
| `tools/generate_ui.py` | `generated/features_config.h` |

## Manifest Features & Options

- Features: `requires_roles` checked against bindings → active/inactive
- Options: `options: [{value, label}]` → select widget in UI
- Constraints: `enum_filter` → `requires_feature` → `requires_state` per option
- `visible_when`: `{"key": "state.key", "eq": value}` — runtime card/widget visibility

## Build Environment

- ESP-IDF build: `powershell -ExecutionPolicy Bypass -File run_build.ps1`
- Host tests: needs `C:/msys64/ucrt64/bin` in PATH for DLLs
- WebUI build: `cd webui && npm run build && npm run deploy`
- Generator: `python tools/generate_ui.py` (auto-runs at CMake build)
