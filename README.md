# ModESP v4

**Modular ESP32 firmware framework for industrial refrigeration controllers**

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-blue?logo=espressif)](https://docs.espressif.com/projects/esp-idf/en/v5.5/)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)](https://isocpp.org/)
[![Svelte](https://img.shields.io/badge/Svelte-4-FF3E00?logo=svelte)](https://svelte.dev/)
[![Tests](https://img.shields.io/badge/Tests-491%20passed-brightgreen)](tests/)
[![License](https://img.shields.io/badge/License-Source%20Available-blue)](LICENSE)

> Manifest-driven firmware that generates UI, state metadata, MQTT topics, and feature flags from JSON — zero manual sync between firmware, WebUI, and cloud.

<!-- TODO: ![WebUI Screenshot](docs/img/dashboard.png) -->

---

## Key Metrics

| Metric | Value |
|--------|-------|
| State keys | 126 (63 metadata, 50 MQTT pub, 62 MQTT sub) |
| Modules | 5 (equipment, thermostat, defrost, protection, datalogger) |
| Drivers | 6 (DS18B20, NTC, relay, digital input, PCF8574 relay/input) |
| HTTP endpoints | 23 REST + WebSocket + OTA upload |
| Tests | 491 (310 pytest + 181 C++ host / 418 assertions) |
| WebUI | 80 KB gzipped, Svelte 4, dark/light theme, 4 languages (UK/EN/DE/PL) |
| Firmware binary | ~1.2 MB, free heap 77–90 KB operational |
| Target | ESP32-WROOM-32, 4 MB flash, ESP-IDF v5.5 |

> **Full feature list:** [docs/FEATURES.md](docs/FEATURES.md) | [Українською](docs/FEATURES_UA.md)

---

## How It Works

```
 module manifests ──┐
 driver manifests ──┼──▶  generate_ui.py  ──▶  ui.json        (WebUI schema)
 board.json        ──┤                     ──▶  state_meta.h   (C++ metadata)
 bindings.json     ──┘                     ──▶  mqtt_topics.h  (pub/sub arrays)
                                           ──▶  features_config.h
                                           ──▶  display_screens.h
```

Add a module manifest → rebuild → new parameters appear in WebUI, MQTT, state engine, and persistence automatically. No manual wiring.

### Equipment Arbitration

```
  Thermostat          Defrost           Protection
      │                  │                  │
      │ req.compressor   │ req.defrost      │ lockout / block
      ▼                  ▼                  ▼
  ┌──────────────────────────────────────────────┐
  │           Equipment Manager                   │
  │  Priority: Protection > Defrost > Thermostat  │
  │  Interlocks: defrost ↔ compressor             │
  └──────────────────────────────────────────────┘
      │           │           │           │
    Relay       Relay       Relay       Relay
   (comp)    (evap_fan)  (cond_fan)   (defrost)
```

Business modules publish requests to SharedState. Equipment Manager arbitrates and drives hardware. Modules never touch GPIO directly.

---

## Refrigeration Features

**Thermostat** — 4-state FSM, asymmetric differential, night setback (4 modes), safety run on sensor failure, configurable min on/off times

**Defrost** — 7-phase FSM (pre-drip → defrost → drip → equalize → fan delay → stabilize → normal), 3 types (natural, electric, hot-gas), 4 initiations (timer, demand, manual, digital input)

**Protection** — 10 independent alarm monitors with 2-level escalation:

| Monitor | Trigger | Escalation |
|---------|---------|------------|
| High/Low temp | Threshold + delay | Warning → lockout |
| Sensor failure (×2) | No reading | Immediate |
| Door open | Contact + delay | Warning |
| Short/Rapid cycle | Frequency detection | Compressor blocked |
| Continuous run | Max runtime exceeded | Forced off → lockout |
| Pulldown failure | Min drop not met | Warning |
| Rate of change | °C/min threshold | Warning |

**DataLogger** — 6 temperature channels, 18 event types, LittleFS storage with rotation, streaming JSON API, SVG chart, CSV export

---

## Connectivity

- **WiFi** — STA + AP fallback, AP→STA periodic probe (30s–5min backoff, 50 KB heap guard), mDNS, STA watchdog
- **MQTT** — TLS (port 8883), delta-publish (only changed values), heartbeat JSON every 30s, LWT, tenant-aware topic prefix
- **HTTP** — 23 REST endpoints: state, settings, bindings, WiFi, OTA, logs, backup/restore, factory reset
- **WebSocket** — real-time state broadcast, max 3 clients, delta updates
- **Cloud** — [ModESP Cloud](https://github.com/Zapadenec1982/ModESP_Cloud) integration: auto-discovery, signed OTA, HACCP compliance ([details](docs/CLOUD_INTEGRATION.md))

---

## Web Interface

Svelte 4 SPA — 80 KB gzipped, served from ESP32 LittleFS.

- Bento-card dashboard with grouped parameters
- Real-time updates via WebSocket (no polling)
- Light / dark theme, responsive layout
- **4 languages:** Ukrainian, English, German, Polish (lazy-load language packs from LittleFS)
- Progressive disclosure — UI shows only settings for connected hardware
- GroupAccordion with expand/collapse, parameter validation
- Adding a new language = translation files only, no code changes

<!-- TODO: Screenshots -->

---

## Board Configuration

One firmware codebase — different hardware via JSON config files:

```
data/
├── board.json       # PCB definition: GPIO pins, buses, expanders
└── bindings.json    # Role mapping: compressor → relay on GPIO 14
```

**board.json** describes physical hardware — which GPIOs, OneWire buses, I2C expanders, ADC channels the PCB has. **bindings.json** maps logical roles (compressor, evap_fan, air_temp) to specific drivers and pins.

Switch board → rebuild → same firmware runs on different hardware. No code changes.

| Board | GPIO | I2C | Sensors | Relays |
|-------|------|-----|---------|--------|
| ESP32-DevKit (direct GPIO) | 4 relay + 1 OW + 1 DI + 2 ADC | — | DS18B20, NTC, DI | GPIO relay |
| KC868-A6 (I2C expander) | — | PCF8574 ×2 | DS18B20, NTC | PCF8574 relay |
| Custom board | Any combination | Optional | Any supported driver | Any supported driver |

---

## Project Status

**Milestone M3 "Production Ready" achieved.** All core phases complete ✅

| Phase | Name | Status |
|-------|------|--------|
| 1–4 | Core Architecture, HAL, Drivers, Equipment Manager | ✅ Complete |
| 5a | WiFi + HTTP API (23 endpoints) + WebSocket | ✅ Complete |
| 5b | UI Code Generation (manifest → 5 artifacts) | ✅ Complete |
| 6–10 | Thermostat v2, Defrost 7-phase, NTC, DS18B20 SEARCH_ROM | ✅ Complete |
| 11 | MQTT + TLS, OTA with rollback, NVS persist | ✅ Complete |
| 12–13 | Night Setback, KC868-A6 board, WebUI widgets | ✅ Complete |
| 14 | DataLogger (6-ch temperature + events, SVG chart) | ✅ Complete |
| 15 | Manifest Standard v2, feature resolution, progressive disclosure | ✅ Complete |
| 16 | WebUI Premium Redesign (bento cards, dark theme, i18n) | ✅ Complete |
| 17 | Protection System (10 alarms, CompressorTracker, 2-level escalation) | ✅ Complete |
| 18 | WiFi + MQTT hardening (AP→STA probe, TLS, delta-publish, heartbeat) | ✅ Complete |

Full roadmap: [docs/06_roadmap.md](docs/06_roadmap.md) | Future phases: [docs/ROADMAP_NEXT.md](docs/ROADMAP_NEXT.md)

---

## Testing

**491 tests** across two test suites:

| Suite | Tests | Coverage |
|-------|-------|----------|
| **pytest** (tools/tests/) | 310 | Manifests, generator, state validation, MQTT topics, API contracts |
| **C++ doctest** (tests/host/) | 181 (418 assertions) | SharedState, ETL containers, thermostat FSM, defrost FSM, protection monitors |

```bash
python -m pytest tools/tests/ -v                                           # pytest
cd tests/host && cmake -B build && cmake --build build && ctest --test-dir build  # C++ host
```

---

## Tech Stack

| Layer | Technology | Version |
|-------|-----------|---------|
| MCU | ESP32-WROOM-32 | 4 MB flash |
| Framework | ESP-IDF | v5.5 |
| Language | C++17 | ETL (zero heap) |
| RTOS | FreeRTOS | ESP-IDF bundled |
| JSON parser | jsmn | header-only, zero-alloc |
| Filesystem | LittleFS | 960 KB partition |
| WebUI | Svelte 4, Rollup | Lucide icons, i18n |
| Code generation | Python 3 | manifest → 5 artifacts |
| Testing | pytest + doctest | 491 tests |
| Cloud | [ModESP Cloud](https://github.com/Zapadenec1982/ModESP_Cloud) | Node.js, PostgreSQL, Mosquitto |

---

## Project Structure

```
components/
├── modesp_core/        # BaseModule, ModuleManager, SharedState, types
├── modesp_services/    # Error, Watchdog, Config, Persist, Logger, SystemMonitor
├── modesp_hal/         # HAL, DriverManager, driver interfaces
├── modesp_net/         # WiFi, HTTP (23 endpoints), WebSocket
├── modesp_mqtt/        # MQTT client with TLS, delta-publish, HA discovery
└── modesp_json/        # JSON serialization (jsmn-based)
modules/
├── equipment/          # HAL owner, arbitration, interlocks
├── thermostat/         # 4-state FSM, fan control, night setback
├── defrost/            # 7-phase FSM, 3 types, 4 initiations
├── protection/         # 10 monitors, CompressorTracker, motor hours
└── datalogger/         # 6-ch temperature + 18 event types (LittleFS)
drivers/
├── ds18b20/            # Dallas OneWire temperature sensor
├── ntc/                # NTC thermistor via ADC (B-parameter)
├── relay/              # GPIO relay with min on/off protection
├── digital_input/      # GPIO digital input (door contact)
├── pcf8574_relay/      # I2C PCF8574 relay (KC868-A6)
└── pcf8574_input/      # I2C PCF8574 input (KC868-A6)
tools/
├── generate_ui.py      # Manifest → 5 artifacts (~1644 lines)
└── tests/              # 310 pytest tests
webui/                  # Svelte 4 source (24 widget components)
tests/host/             # 181 C++ doctest tests (418 assertions)
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [**FEATURES.md**](docs/FEATURES.md) | **All firmware capabilities at a glance** ([🇺🇦 Українською](docs/FEATURES_UA.md)) |
| [ARCHITECTURE.md](ARCHITECTURE.md) | System architecture deep-dive |
| [CLOUD_INTEGRATION.md](docs/CLOUD_INTEGRATION.md) | ModESP Cloud integration guide |
| [docs/05_cooling_defrost.md](docs/05_cooling_defrost.md) | Thermostat + Defrost technical spec |
| [docs/07_equipment.md](docs/07_equipment.md) | Equipment Manager + Protection module |
| [docs/10_manifest_standard.md](docs/10_manifest_standard.md) | Manifest specification v2.0 |
| [docs/](docs/) | 12 technical documents |

---

## Technical Highlights

### For Reviewers & Hiring Managers

This project demonstrates production-grade embedded engineering across the full IoT stack:

**Firmware Architecture:**
- Manifest-driven code generation — JSON manifests produce 5 C++ headers + UI schema at build time
- Zero heap allocation in runtime loops — ETL containers (etl::string, etl::vector, etl::variant) instead of STL
- SharedState engine with 126 typed keys, compile-time metadata, and automatic NVS persistence
- Equipment arbitration with safety interlocks — Protection always overrides Thermostat and Defrost

**Refrigeration Domain:**
- 4-state thermostat FSM (startup → idle → cooling → safety_run) with asymmetric differential, anti-short-cycle, and safety run on sensor failure
- 7-phase defrost FSM supporting natural, electric, and hot-gas defrost with 4 initiation modes
- 10 independent alarm monitors with 2-level escalation (compressor blocked → system lockout)
- CompressorTracker: motor hours, start counting, continuous-run detection

**Embedded Web & Connectivity:**
- Svelte 4 SPA served from ESP32 LittleFS — 80 KB gzipped with real-time WebSocket updates
- 4-language i18n (UK/EN/DE/PL) with lazy-loaded language packs from LittleFS (~8KB gzip each)
- 23 REST endpoints with JSON validation via jsmn (zero-alloc parser)
- MQTT over TLS with delta-publish (only changed values), heartbeat, LWT, and tenant-aware topics
- WiFi STA + AP with intelligent AP→STA probe (exponential backoff, heap guard, timeout)

**Hardware Abstraction:**
- board.json + bindings.json — same firmware binary runs on different PCBs without code changes
- 6 driver types: DS18B20, NTC, GPIO relay, digital input, PCF8574 relay/input (I2C)
- Dual OTA partitions with SHA-256 verification, board compatibility check, and automatic rollback

**Testing & Quality:**
- 491 tests: 310 pytest (manifests, generator, API contracts) + 181 C++ doctest (FSMs, state engine)
- Host-compiled C++ tests — run on desktop without ESP32 hardware
- Continuous code generation validation — generator output tested against expected artifacts

---

## Quick Start

```bash
# Prerequisites: ESP-IDF v5.5, Python 3.8+, Node.js 18+

# Build firmware (auto-generates code from manifests)
idf.py build

# Flash to ESP32
idf.py -p /dev/ttyUSB0 flash monitor

# Build WebUI (optional — pre-built bundle included)
cd webui && npm install && npm run build && npm run deploy
```

---

## License

**Source-available** under [PolyForm Noncommercial License 1.0.0](LICENSE).

Free to use, study, and modify for personal and non-commercial purposes.
Commercial use requires a separate license — contact via [GitHub](https://github.com/Zapadenec1982) or [email](mailto:tepliuk.yurii@gmail.com).

---

## Author

**Yurii Tepliuk** — Embedded Systems Engineer, Ukraine

- Industrial refrigeration control systems (ESP32 firmware + cloud platform)
- ESP-IDF / FreeRTOS / C++17 / ETL — zero-heap embedded development
- Full-stack IoT: firmware → MQTT → Node.js cloud → Svelte WebUI
- Production deployment & testing (491 firmware tests, 130+ cloud tests)

[![GitHub](https://img.shields.io/badge/GitHub-Zapadenec1982-181717?logo=github)](https://github.com/Zapadenec1982)

---

*Українською:* ModESP v4 — модульний фреймворк для промислових ESP32-контролерів холодильного обладнання. Manifest-driven архітектура, Svelte WebUI, MQTT+TLS, OTA з відкатом. [Документація українською](docs/FEATURES_UA.md).
