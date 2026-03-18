# ModESP v4 — System Architecture

> Modular firmware framework for industrial ESP32 refrigeration controllers.
> Replaces proprietary Danfoss/Dixell PLCs with an open, cost-effective ESP32 platform
> while maintaining industrial-grade reliability and safety standards.

## System Overview

ModESP v4 is a manifest-driven, real-time control system that runs on commodity
ESP32 hardware. It manages compressor cycling, defrost sequences, alarm monitoring,
and data logging for commercial cold rooms and refrigerated display cases.

| Attribute         | Value                                          |
|-------------------|------------------------------------------------|
| Target MCU        | ESP32-WROOM-32 (Xtensa dual-core, 240 MHz)    |
| Flash             | 4 MB (dual OTA + LittleFS data partition)      |
| Framework         | ESP-IDF v5.5, C++17                            |
| Standard library  | ETL (Embedded Template Library) — zero-heap    |
| Reference board   | Kincony KC868-A6 (6 relays, 6 inputs via I2C)  |
| Web UI            | Svelte 4 SPA, gzipped ~76 KB total             |
| Connectivity      | WiFi STA/AP, HTTP REST, WebSocket, MQTT + TLS  |
| State keys        | 126 runtime keys, 63 with compile-time metadata|

---

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        BUILD TIME (host)                            │
│                                                                     │
│   modules/*/manifest.json ──┐                                       │
│   drivers/*/manifest.json ──┼──► generate_ui.py ──► 5 artifacts     │
│   board.json + bindings.json┘        │                              │
│                                      ├── data/ui.json               │
│                                      ├── generated/state_meta.h     │
│                                      ├── generated/mqtt_topics.h    │
│                                      ├── generated/display_screens.h│
│                                      └── generated/features_config.h│
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                        RUNTIME (ESP32)                              │
│                                                                     │
│  ┌──────────┐   ┌────────────┐   ┌────────────┐   ┌─────────────┐  │
│  │ WiFi STA │   │ HTTP (21   │   │ WebSocket  │   │ MQTT + TLS  │  │
│  │ /AP/APSTA│   │ endpoints) │   │ delta push │   │ HA Discovery│  │
│  └────┬─────┘   └─────┬──────┘   └─────┬──────┘   └──────┬──────┘  │
│       └───────────────┬┴──────────────┬─┘                 │         │
│                       ▼               ▼                   ▼         │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                     SharedState (126 keys)                   │   │
│  │         etl::unordered_map — thread-safe, versioned          │   │
│  └─────┬────────┬─────────┬──────────┬──────────┬──────────┘   │   │
│        ▼        ▼         ▼          ▼          ▼               │   │
│  ┌─────────┐┌────────┐┌────────┐┌──────────┐┌───────────┐      │   │
│  │Equipment││Protect.││Thermo. ││ Defrost  ││DataLogger │      │   │
│  │ P=0     ││ P=1    ││ P=2   ││  P=2     ││  P=3      │      │   │
│  │ (HAL)   ││(alarms)││(temp) ││(7-phase) ││(6-ch log) │      │   │
│  └────┬────┘└────────┘└────────┘└──────────┘└───────────┘      │   │
│       ▼                                                         │   │
│  ┌──────────────────────────────────────────────────────────┐   │   │
│  │                    HAL + DriverManager                    │   │   │
│  │  DS18B20 │ NTC/ADC │ Relay │ DigitalIn │ PCF8574 I/O    │   │   │
│  └──────────────────────────────────────────────────────────┘   │   │
│       ▼                                                         │   │
│  ┌──────────────────────────────────────────────────────────┐   │   │
│  │              GPIO / OneWire / I2C / ADC                   │   │   │
│  └──────────────────────────────────────────────────────────┘   │   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Core Components

### SharedState

The central state store. All inter-module communication flows through it
rather than direct calls — a pattern borrowed from Redux/Elm for embedded systems.

- Fixed-capacity `etl::unordered_map<StateKey, StateValue, 128>` (no heap)
- `StateKey` = `etl::string<32>`, `StateValue` = `etl::variant<int32_t, float, bool, etl::string<32>>`
- FreeRTOS mutex for thread safety
- Monotonic version counter — WebSocket service diffs against it to produce delta broadcasts
- Persist callback fires outside the mutex lock to avoid priority inversion

### ModuleManager

Manages the lifecycle of all modules: `register → init_all → update_all (loop) → stop_all`.

- Priority-sorted execution: CRITICAL(0) → HIGH(1) → NORMAL(2) → LOW(3)
- Idempotent init (skips already-initialized modules)
- `restart_module()` for watchdog-triggered recovery
- Three-phase boot: system services, then WiFi + business modules, then HTTP + WebSocket

### Message Bus

`etl::message_bus<24>` provides decoupled publish/subscribe between modules.
Message ID ranges are partitioned: System 0-49, Services 50-99, HAL 100-109,
Drivers 110-149, Modules 150-249.

### PersistService

Automatic NVS persistence with debounced writes.

- Subscribes to SharedState changes via callback
- Dirty-flag tracking with 5-second debounce before NVS flush
- DJB2 hash-based NVS keys (`"s" + 7 hex chars`) for O(1) lookup
- Batch NVS API: single handle open → write all dirty keys → single commit → close
- Auto-migration from legacy positional keys at boot

---

## Equipment Layer & Arbitration

The `EquipmentModule` (priority CRITICAL) is the sole owner of hardware drivers.
Business modules never touch hardware directly — they publish *requests* to SharedState,
and Equipment arbitrates.

```
  Protection Module          Thermostat Module         Defrost Module
  ─────────────────          ────────────────          ──────────────
  protection.lockout ───┐    thermostat.req.* ──┐     defrost.req.* ──┐
  protection.comp_blocked┤                      │                      │
                         ▼                      ▼                      ▼
                ┌─────────────────────────────────────────────────┐
                │              Equipment Arbitrator               │
                │                                                 │
                │  Priority: LOCKOUT > COMP_BLOCKED > DEFROST     │
                │            > THERMOSTAT                         │
                │                                                 │
                │  Interlocks: defrost_relay ↔ compressor          │
                │              (never active simultaneously)      │
                │                                                 │
                │  Anti-short-cycle: 180s min OFF, 120s min ON    │
                └────────────┬────────────────────────────────────┘
                             ▼
                   Relay / Sensor Drivers
```

- EMA-filtered temperature readings (configurable coefficient, 0.01 C rounding)
- Publishes actual relay state via `get_state()`, not the requested state
- Hardware capability flags (`equipment.has_*`) enable/disable UI features at runtime

---

## Business Modules

### Thermostat

Asymmetric differential control with compressor protection.

- **State machine:** `STARTUP → IDLE ↔ COOLING`, plus `SAFETY_RUN` on sensor failure
- **Asymmetric differential:** Compressor ON at `T >= SP + differential`, OFF at `T <= SP`
- **Safety Run:** Timed cyclic operation (configurable on/off periods) when primary sensor fails
- **Evaporator fan:** 3 modes — continuous, synchronized with compressor, or temperature-controlled with hysteresis
- **Night setback:** 4 modes (off / SNTP schedule / digital input / manual) — raises effective setpoint
- **Display during defrost:** Real temperature, frozen value, or `-d-` indicator
- 16 NVS-persisted parameters

### Defrost

Industrial 7-phase defrost with three heating types.

```
  IDLE ──► STABILIZE ──► VALVE_OPEN ──► ACTIVE ──► EQUALIZE ──► DRIP ──► FAD ──► IDLE
           (hot gas)     (hot gas)       (all)      (hot gas)    (all)    (all)
```

- **3 types:** Natural (compressor stop), electric heater, hot gas
- **4 initiation modes:** Timer (real-time or compressor-hours), demand (evaporator temperature), combined, manual
- **Termination:** Evaporator reaches target temperature, or safety timer expires
- **Smart skip:** Bypasses defrost if evaporator is already above end temperature
- **FAD (Fan After Defrost):** Re-cools evaporator before resuming normal airflow
- 14 persisted parameters + 2 runtime counters preserved across power cycles

### Protection

10 independent alarm monitors with 2-level compressor escalation.

| Monitor          | Trigger                                    | Delay     |
|------------------|--------------------------------------------|-----------|
| High Temperature | `T > high_limit`                           | Configurable (min) |
| Low Temperature  | `T < low_limit`                            | Configurable (min) |
| Sensor 1 Failure | Primary sensor offline                     | Instant   |
| Sensor 2 Failure | Secondary sensor offline                   | Instant   |
| Door Open        | Contact input active                       | Configurable (min) |
| Short Cycling    | 3 consecutive cycles below minimum run     | Instant   |
| Rapid Cycling    | Starts/hour exceeds threshold              | Instant   |
| Continuous Run   | Compressor ON beyond max duration          | Instant   |
| Pulldown Failure | Insufficient temperature drop in time      | Instant   |
| Rate of Change   | EWMA-filtered rise rate exceeds threshold  | Instant   |

- **CompressorTracker:** Ring buffer of 30 starts, sliding 1-hour window, duty cycle calculation
- **Continuous Run Escalation:**
  Level 1 (`compressor_blocked`) — forced compressor off, fans keep running.
  Level 2 (`lockout`) — permanent shutdown of all equipment after repeated triggers.
- **Icing detection:** After forced-off, checks `evap_temp < demand_temp` to trigger emergency defrost
- **Defrost awareness:** High-temp and rate-of-change alarms are suppressed during and after defrost (configurable delay)
- Motor hours tracking (cumulative, persisted)
- 17 NVS-persisted parameters

### DataLogger

6-channel data recorder with on-device storage and streaming API.

- **Channels:** Air temp (always) + evaporator + condenser + setpoint + humidity + reserved
- **16-byte records:** 4-byte timestamp + 6 x int16_t channels; `INT16_MIN` sentinel for inactive channels
- **Storage:** LittleFS with automatic rotation; RAM buffer (16 temp + 32 events) flushed every 10 minutes
- **18 event types:** Compressor on/off, defrost start/end, all 10 alarm types, door, power-on
- **Streaming API:** `GET /api/log?hours=24` returns chunked JSON with dynamic channel headers
- **Client-side CSV export** — zero MCU overhead

---

## Driver Layer

Six drivers behind a HAL abstraction. The same firmware binary runs on both
a dev board (GPIO relays) and the KC868-A6 (I2C PCF8574 expanders) — only
`board.json` changes.

| Driver          | Type     | Interface    | Notes                              |
|-----------------|----------|--------------|-------------------------------------|
| ds18b20         | Sensor   | OneWire      | MATCH_ROM multi-sensor, CRC8, SEARCH_ROM scan |
| ntc             | Sensor   | ADC          | B-parameter thermistor equation     |
| digital_input   | Sensor   | GPIO         | 50 ms debounce, configurable polarity |
| relay           | Actuator | GPIO         | Min on/off time protection          |
| pcf8574_relay   | Actuator | I2C (0x24)   | 8-bit port expander, KC868-A6       |
| pcf8574_input   | Sensor   | I2C (0x22)   | 8-bit port expander, KC868-A6       |

---

## Network Stack

### WiFi

- **STA mode** with AP fallback (`ModESP-XXXX`) after 3 failed attempts
- **AP-to-STA probe:** While in AP mode, periodically attempts STA via `WIFI_MODE_APSTA`
  with exponential backoff (30 s to 5 min). AP remains accessible during probes.
  Solves the cold-start problem where ESP32 boots faster than the router.
- STA watchdog: auto-restarts WiFi if disconnected for >10 minutes cumulative
- RSSI published to SharedState every 10 seconds

### HTTP API

21 REST endpoints on port 80 covering state, settings, configuration, OTA, diagnostics, and data logging.

Key endpoints:

| Endpoint              | Method    | Purpose                                |
|-----------------------|-----------|----------------------------------------|
| `/api/state`          | GET       | Full runtime state as JSON             |
| `/api/ui`             | GET       | Generated UI schema (manifest-driven)  |
| `/api/settings`       | POST      | Write parameters (validated, clamped)  |
| `/api/ota`            | POST      | Over-the-air firmware update           |
| `/api/onewire/scan`   | GET       | Discover DS18B20 sensors on bus        |
| `/api/log`            | GET       | Streaming temperature/event history    |
| `/api/bindings`       | GET/POST  | Hardware role-to-driver mapping        |
| `/ws`                 | WebSocket | Real-time state push                   |

### WebSocket

- **Delta broadcasts:** Only changed keys are sent (~200 bytes vs 3.5 KB full state)
- 1.5-second broadcast interval, 20-second heartbeat ping
- New clients receive full state snapshot on connect
- Maximum 3 concurrent clients (memory-constrained)
- Heap guard: 16 KB minimum free before allocating full-state frames

### MQTT

- **Home Assistant Auto-Discovery** via MQTT Discovery protocol
- **TLS** with automatic detection (port 8883 or `mqtts://` prefix)
- **Last Will & Testament** for offline detection (retained)
- **Exponential backoff** on connection failure: 5 s to 5 min
- **Tenant isolation:** Topic prefix `modesp/v1/{tenant}/{device_id}`
- **Delta-publish cache:** 16-entry LRU — only changed values are published
- **Alarm republish:** Critical alarms re-sent every 5 minutes (retained, QoS 1)

---

## Web UI

The embedded web interface is a Svelte 4 SPA served from LittleFS as gzipped bundles.

- **Manifest-driven rendering:** The UI schema (`ui.json`) defines every page, card,
  and widget. The frontend renders them dynamically — no hardcoded forms.
- **Responsive layout:** Bento-card dashboard with accordion sections (auto-collapsed on mobile)
- **Theme:** Light/dark toggle with CSS custom properties, persisted to `localStorage`
- **Internationalization:** Ukrainian/English toggle (~120 keys per language)
- **Runtime visibility:** `visible_when` rules hide/show UI elements based on live state values.
  Hardware-dependent options (e.g., hot gas defrost) appear disabled with explanatory hints
  until the required driver is bound.
- **Live updates:** WebSocket-driven value display with change-flash animations

Total bundle size: ~63 KB JS + ~13 KB CSS (gzipped).

---

## Code Generation Pipeline

A Python generator (`tools/generate_ui.py`, ~1,700 lines) reads all manifests
at build time and produces 5 artifacts:

```
  Module manifests ──┐
  Driver manifests ──┼──► generate_ui.py ──┬── data/ui.json            (UI schema)
  board.json ────────┤                     ├── generated/state_meta.h   (constexpr metadata)
  bindings.json ─────┘                     ├── generated/mqtt_topics.h  (pub/sub topic arrays)
                                           ├── generated/display_screens.h (LCD data)
                                           └── generated/features_config.h (feature flags)
```

- **FeatureResolver** inspects `bindings.json` to determine which hardware roles are
  satisfied, then activates/deactivates features accordingly
- **Constraint resolution** maps feature requirements to `equipment.has_*` state keys
  so the UI can reactively enable/disable options at runtime
- **7 validation passes** (V14-V19+) catch manifest errors before they reach the firmware
- The generator runs automatically as a CMake pre-build step

---

## Memory & Performance

### Zero-Heap Hot Path

All code in `on_update()` and `on_message()` uses only stack and statically-allocated
ETL containers. No `std::string`, `std::vector`, `new`, or `malloc` in the control loop.

| Container            | Replaces       | Capacity       |
|----------------------|----------------|----------------|
| `etl::string<32>`    | `std::string`  | 32 chars fixed |
| `etl::vector<T, N>`  | `std::vector`  | N elements max |
| `etl::variant`       | `std::variant` | Compile-time   |
| `etl::unordered_map` | `std::unordered_map` | 128 entries |

### Flash Partition Layout (4 MB)

```
  0x009000  NVS           16 KB    Settings persistence (DJB2 hashed keys)
  0x00D000  OTA Data       8 KB    Boot partition selector
  0x00F000  PHY Init       4 KB    RF calibration data
  0x010000  OTA Slot 0     1.5 MB  Active firmware
  0x190000  OTA Slot 1     1.5 MB  Update target (rollback capable)
  0x310000  LittleFS     960 KB    Web UI + data logs
```

### Heap Safeguards

- WebSocket frames gated by 16 KB / 8 KB free-heap checks
- NVS batch API: single handle for all persist writes (avoids 30+ open/close cycles)
- Temperature values rounded to 0.01 C to minimize unnecessary state version bumps
- `track_change=false` on diagnostic/timer keys to suppress WebSocket broadcasts
- `system.heap_largest` monitors heap fragmentation in real time

---

## Testing

### Host-Side C++ Tests (doctest)

108 test cases (454 assertions) that run on the development machine without
an ESP32. Business logic for Thermostat, Defrost, and Protection modules is
tested against mock SharedState and stub HAL interfaces.

### Python Generator Tests (pytest)

254 test cases covering manifest parsing, feature resolution, constraint
propagation, UI schema generation, and 7 validation passes.

### Build Verification

The firmware compiles with `-Werror=all` — every compiler warning is a build failure.

---

## Project Structure

```
ModESP_v4/
├── main/main.cpp                 # Boot sequence, main loop
├── components/
│   ├── modesp_core/              # BaseModule, ModuleManager, SharedState
│   ├── modesp_services/          # Persist, Watchdog, Error, Config, NVS
│   ├── modesp_hal/               # HAL abstraction, DriverManager
│   ├── modesp_net/               # WiFi, HTTP, WebSocket services
│   ├── modesp_mqtt/              # MQTT client (TLS, HA Discovery)
│   └── modesp_json/ + jsmn/      # JSON serialization (streaming + parser)
├── drivers/                      # 6 hardware drivers (sensor + actuator)
├── modules/                      # 5 business modules
│   ├── equipment/                #   HAL owner, arbitration (P=0)
│   ├── protection/               #   10 alarm monitors (P=1)
│   ├── thermostat/               #   Temperature control (P=2)
│   ├── defrost/                  #   7-phase defrost FSM (P=2)
│   └── datalogger/               #   6-channel data logging (P=3)
├── tools/
│   ├── generate_ui.py            # Manifest → artifacts generator
│   └── tests/                    # 254 pytest test cases
├── tests/host/                   # 108 doctest C++ test cases
├── webui/                        # Svelte 4 SPA source
├── data/
│   ├── board.json                # Active board pin configuration
│   ├── bindings.json             # Role → driver → pin mapping
│   ├── ui.json                   # Generated UI schema
│   └── www/                      # Deployed web assets (gzipped)
├── generated/                    # 5 generated C++ headers
└── partitions.csv                # Flash layout (dual OTA + LittleFS)
```

---

## Design Principles

1. **Manifest as single source of truth.** UI, state metadata, MQTT topics, display
   layouts, and feature flags are all generated from JSON manifests. Change the manifest,
   rebuild, and every layer updates consistently.

2. **Hardware abstraction via board.json.** The same firmware binary runs on different
   boards. Swap `board.json` and `bindings.json` to retarget from a dev board with GPIO
   relays to a production KC868-A6 with I2C expanders.

3. **Safety by architecture.** Business modules cannot touch hardware directly.
   The Equipment layer enforces interlocks (defrost relay vs compressor), anti-short-cycle
   timers, and priority-based arbitration. A bug in the thermostat cannot cause a
   compressor short-cycle — the protection is structural, not just logical.

4. **Deterministic memory.** ETL containers with compile-time capacity bounds eliminate
   heap fragmentation. The control loop allocates zero bytes at runtime.

5. **Observable state.** Every sensor reading, relay state, alarm flag, and configuration
   parameter lives in SharedState with a versioned, diffable interface. WebSocket,
   MQTT, HTTP, and the on-device display all consume the same data.
