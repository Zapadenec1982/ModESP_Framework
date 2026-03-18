# Architecture: ModESP v4

## System Overview

Modular framework for industrial ESP32 controllers (refrigeration).
Replaces expensive Danfoss/Dixell controllers with ESP32 + professional architecture.

## Module Pipeline

```
Equipment(0) → Protection(1) → Thermostat(2) + Defrost(2)
    │                │                │              │
    ├─ reads sensors ├─ monitors      ├─ cooling     ├─ 7-phase cycle
    ├─ publishes T   │  10 alarms     │  state       │  3 types
    └─ arbitrates    └─ lockout/block │  machine     └─ timer/demand
       relay outputs                  └─ night mode
```

## Equipment Layer (priority=CRITICAL)

- Only module with HAL driver access
- Reads sensors → publishes `equipment.air_temp`, `equipment.sensor1_ok`, etc.
- Reads requests from business modules: `thermostat.req.compressor`, `defrost.req.*`, `protection.lockout`
- **Arbitration:** Protection LOCKOUT > compressor_blocked > Defrost active > Thermostat
- **Interlocks:** defrost_relay and compressor NEVER active simultaneously
- **Anti-short-cycle (output-level):** COMP_MIN_OFF_MS=180s, COMP_MIN_ON_MS=120s
- **EMA filter:** alpha = 1/(filter_coeff+1), roundf() to 0.01C
- `equipment.has_*` state keys: has_defrost_relay, has_cond_fan, has_door_contact, has_evap_temp, has_cond_temp, has_night_input, has_ntc_driver, has_ds18b20_driver

## Thermostat v2

- **Asymmetric differential:** ON at T >= SP + differential, OFF at T <= SP
- **State machine:** STARTUP > IDLE <> COOLING, SAFETY_RUN (sensor failure)
- **Night Setback:** 4 modes (0=off, 1=SNTP schedule, 2=DI, 3=manual)
- **Display during defrost:** 3 modes (real T / frozen T / "-d-")
- Requests: `thermostat.req.compressor`, `thermostat.req.evap_fan`, `thermostat.req.cond_fan`

## Protection (10 monitors)

- **Delayed:** High Temp, Low Temp, Door (separate delays in minutes)
- **Instant:** Sensor1, Sensor2
- **Compressor safety:** Short Cycle, Rapid Cycle, Continuous Run, Pulldown, Rate-of-Change
- **Continuous Run Escalation:** Level 1 compressor_blocked > Level 2 lockout
- **Defrost blocking:** High Temp + Rate suppressed during heating phases + post_defrost_delay
- Alarm code priority: lockout > comp_blocked > err1 > rate_rise > high_temp > pulldown > short_cycle > rapid_cycle > low_temp > continuous_run > err2 > door > none

## Defrost (7-phase)

- **Phases:** IDLE > [STABILIZE > VALVE_OPEN >] ACTIVE > [EQUALIZE >] DRIP > FAD > IDLE
- **3 types:** 0=natural (stop), 1=electric heater, 2=hot gas (7 phases)
- **4 initiation modes:** timer, demand (T_evap < demand_temp), combined, manual
- **Termination:** by T_evap >= end_temp (primary), by max_duration timer (safety)
- **defrost_relay** — unified relay for both electric heater and hot gas valve

## Features System

- Manifest-driven feature flags with `requires_roles`
- `FeatureResolver` (Python): checks `bindings.json` → active/inactive features
- `features_config.h`: constexpr array + `is_feature_active()` inline lookup
- C++ modules: `has_feature("name")` for runtime guards

## DataLogger (6 channels)

- Channels: air (always) + evap + cond + setpoint + humidity + reserved
- TempRecord 16 bytes, LittleFS storage (temp.bin + events.bin)
- 18 event types, RAM buffer (16 temp + 32 events), flush every 10 min
- JSON v3: dynamic channels, streaming chunked response

## Key Services

| Service | Role |
|---|---|
| SharedState | Central state store, thread-safe (FreeRTOS mutex), version counter |
| PersistService | Auto-persist settings to NVS, djb2 hash keys, debounce 5s |
| ModuleManager | Lifecycle: register > init_all > update_all > stop_all |
| HttpService | REST API (port 80), 17+ endpoints |
| WsService | Delta broadcasts (1500ms), heartbeat (20s), max 3 clients |
| WiFiService | STA + AP fallback, country UA, AP>STA probe with backoff |
| MqttService | HA Discovery, TLS, LWT, exponential backoff, delta-publish |

## Drivers (6)

| Driver | Type | Hardware |
|---|---|---|
| ds18b20 | sensor | OneWire (MATCH_ROM multi-sensor, SEARCH_ROM scan) |
| ntc | sensor | ADC thermistor (B-parameter equation) |
| digital_input | sensor | GPIO input (50ms debounce) |
| relay | actuator | GPIO relay (min on/off time) |
| pcf8574_relay | actuator | I2C PCF8574 relay (8-bit port expander) |
| pcf8574_input | sensor | I2C PCF8574 digital input |

## AWS IoT Core (feature/aws-iot branch)

Compile-time alternative to Mosquitto via Kconfig `MODESP_CLOUD_BACKEND`.

- **Component:** `components/modesp_aws/` — AwsIotService (BaseModule)
- **mTLS:** AmazonRootCA1 embedded + client cert/key from NVS
- **Topics:** `modesp/{device_id}/state/{key}`, `cmd/{key}`, `status`, `heartbeat`
- **Shadow:** reported (62 writable params) + delta (desired → validate → apply)
- **OTA:** IoT Jobs → ota_handler::start_ota() (provider-agnostic)
- **HTTP:** GET/POST `/api/cloud` (endpoint, thing_name, cert upload)
- **NVS:** 32KB partition, certs in namespace "awscert"
- Docs: [docs/12_aws_iot.md](docs/12_aws_iot.md)

## Svelte WebUI

- Svelte 4, Rollup bundler, Light/Dark theme, UA/EN i18n
- Build: `cd webui && npm run build && npm run deploy`
- Output: `data/www/bundle.js.gz` (~63KB) + `bundle.css.gz` (~13KB)
- Premium dark theme, bento-card dashboard, responsive accordions
