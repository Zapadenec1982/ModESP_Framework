# Equipment Manager

> 📖 **Українською:** [documentation/uk/03-framework-reference/modules/equipment.md](../../../uk/03-framework-reference/modules/equipment.md)

Equipment Manager is the module що **bridges hardware drivers і business
modules**. It reads `bindings.json` at startup, instantiates drivers, polls
sensors / commands actuators on the update loop, і exposes everything as
`equipment.*` state keys. Business modules never touch drivers directly —
they read і write SharedState, і Equipment Manager handles all the I/O.

This page documents the module's responsibilities, state keys exposed,
configuration parameters, і how it integrates із the broader hardware
pipeline (board.json → bindings.json → drivers → SharedState).

## Where Equipment fits

```
   board.json + bindings.json
              │
              ▼
   DriverManager creates driver instances
              │
              ▼
   Equipment Manager owns the drivers
              │   reads sensors, commands actuators
              ▼
   equipment.<role> keys у SharedState
              │
              ▼
   Business modules (thermostat, defrost, alarms)
   read equipment.<role>, write equipment.req_<role>
```

Equipment is а **service module** (priority `HIGH` = 1, runs у Phase 2 of
init). It's part of the framework і ships у `modules/equipment/`.

## State keys exposed

### Sensor keys (per binding із `category: "sensor"`)

| Key pattern | Type | Notes |
|---|---|---|
| `equipment.<role>` | float | Current sensor reading (з calibration applied). |
| `equipment.<role>_ok` | bool | Health: `true` якщо last read succeeded і driver healthy. |

Example: bindings із `role: "air_temp"` і `role: "evap_temp"` create:
- `equipment.air_temp` (float)
- `equipment.air_temp_ok` (bool)
- `equipment.evap_temp` (float)
- `equipment.evap_temp_ok` (bool)

### Actuator keys (per binding із `category: "actuator"`)

| Key pattern | Type | Notes |
|---|---|---|
| `equipment.<role>` | bool | Current actual state (read-only, reflects driver state). |
| `equipment.req_<role>` | bool | Requested state (writable — business modules write це). |

Example: bindings із `role: "compressor"` і `role: "evap_fan"` create:
- `equipment.compressor`, `equipment.req_compressor`
- `equipment.evap_fan`, `equipment.req_evap_fan`

### Driver-presence keys

Used by business modules to detect which drivers are bound (graceful
degradation when hardware absent):

| Key | Notes |
|---|---|
| `equipment.has_ntc_driver` | `true` якщо at least one NTC sensor binding active. |
| `equipment.has_ds18b20_driver` | `true` якщо at least one DS18B20 binding active. |
| `equipment.<driver>_driver` | One per supported driver type. |

### Configuration keys (persisted)

| Key | Type | Default | Notes |
|---|---|---|---|
| `equipment.filter_coeff` | int | 4 | Digital filter coefficient for sensor smoothing (0=raw..10=heavy). |
| `equipment.ntc_beta` | int | 3950 | NTC B-coefficient. Applies to all NTC sensors. |
| `equipment.ntc_r_series` | int | 10000 | NTC series resistor у Ω. |
| `equipment.ntc_r_nominal` | int | 10000 | NTC nominal resistance at 25 °C (Ω). |
| `equipment.ds18b20_offset` | float | 0.0 | Global DS18B20 calibration offset (°C). |

All persisted (`persist: true`) — user adjustments survive reboot. WebUI
exposes these as setpoints; MQTT subscribes accept writes for remote tuning.

## Update flow per tick

1. **Driver poll** — call `driver->update(dt_ms)` for every bound driver.
   Sensors read hardware, actuators apply pending state changes.
2. **Read sensors** — `driver->read(value)` for each sensor driver; apply
   digital filter і calibration; write `equipment.<role>` і
   `equipment.<role>_ok`.
3. **Forward actuator requests** — for each actuator binding, read
   `equipment.req_<role>` from SharedState і call `driver->set(state)`.
   Driver returns success/failure; mirror back to `equipment.<role>`.
4. **Health monitoring** — track consecutive failures per driver; set
   `_ok` to `false` after threshold; surface alarms через
   error_service for critical sensors.

## Digital filter

Sensors readings can be noisy (especially NTC ADC reads). Equipment applies
а first-order IIR filter:

```
filtered = α × raw + (1 - α) × prev_filtered
α = 1 / (filter_coeff + 1)
```

| `filter_coeff` | α | Behavior |
|---|---|---|
| 0 | 1.0 | No filter (raw reads pass through). |
| 4 (default) | 0.2 | Moderate smoothing, ~5-tick response. |
| 10 | ~0.09 | Heavy smoothing, slow response. |

Set `equipment.filter_coeff = 0` if you want raw access (для own filter або
edge detection).

## Health і failures

Drivers report health через `driver->is_healthy()` і `error_count()`.
Equipment Manager:

- Increments а consecutive-failure counter on each failed read.
- After 3 consecutive failures, sets `equipment.<role>_ok = false`.
- Resets counter on first successful read; restores `_ok` to `true`.
- Logs ESP_LOGW on each transition (avoids log spam).

Business modules що need fail-safe behavior should check `_ok` before
trusting the reading:

```cpp
float temp = 0.0f;
bool sensor_ok = read_bool("equipment.air_temp_ok", false);
if (sensor_ok) {
    temp = read_float("equipment.air_temp", 0.0f);
} else {
    // Fall back: use last-known-good, emergency-safe value, etc.
}
```

## Configuration via WebUI / MQTT

The `mqtt.subscribe` list lets external clients tune calibration без
recompile:

```bash
# Tune NTC B-coefficient via MQTT
mosquitto_pub -t modesp/<device-id>/equipment/ntc_beta -m "3977"
```

Or via HTTP:

```bash
curl -u admin:modesp -X POST http://192.168.1.85/api/settings \
  -d '{"equipment.ntc_beta": 3977}'
```

Or via the WebUI's "Equipment" page (auto-generated from manifest's `ui`
section — not shown here but lives у the equipment manifest).

## Customising the requires list

Equipment manifest's `requires` declares **what kind of bindings the module
expects**:

```json
"requires": [
  {"role": "air_temp",    "type": "sensor",   "driver": ["ds18b20", "ntc"], "label": "Temperature sensor"},
  {"role": "actuator_1",  "type": "actuator", "driver": ["relay", "pcf8574_relay"], "label": "Actuator 1", "optional": true}
]
```

| Field | Notes |
|---|---|
| `role` | Must match а binding's `role` exactly. |
| `type` | `"sensor"` / `"actuator"`. |
| `driver` | Array of allowed driver types. Equipment accepts any of them. |
| `label` | Human-readable name (for UI). |
| `optional` | If `true`, missing binding doesn't abort startup. Default `false`. |

If you need more roles (multiple compressors, multiple temperature zones)
edit equipment's manifest і add `requires` entries. Don't add roles
що bindings won't fill — `optional: true` keeps the door open для future
hardware.

## Display integration

Equipment manifest includes:

```json
"display": {
  "main_value": {
    "key": "equipment.air_temp",
    "format": "%.1f°C"
  }
}
```

On boards із LCD displays, the framework renders а "main value" widget
showing primary temperature. Drives the LCD's default screen content.
[components/modesp_hal.md](../components/modesp_hal.md) *(planned)*
documents display integration deeper.

## Common patterns

### Switching sensor types per deployment

Same firmware, different sensor families per deployment: edit bindings
to use `ntc` driver for cheap deployments, `ds18b20` for precision. Business
module's `read_float("equipment.air_temp", ...)` works identically.

### Multiple temperature zones

For multi-zone refrigeration, add bindings для each zone:

```json
{"role": "air_temp_zone1", "driver": "ds18b20", ..., "address": "..."},
{"role": "air_temp_zone2", "driver": "ds18b20", ..., "address": "..."}
```

Update equipment manifest's `requires` to match. Your business module reads
`equipment.air_temp_zone1` і `equipment.air_temp_zone2` як separate keys.

### Read-only equipment configuration

Some sites lock down equipment params (filter_coeff, calibration constants)
once commissioned. Two options:

1. Remove `persist: true` from manifest — persistent setting forced to
   default on each boot.
2. Keep `persist: true` but set `access: "read"` — value loads from NVS at
   boot, but no API path accepts writes. Configure once у service mode,
   then ship.

## Common mistakes

**Reading missing keys:** if а role's binding isn't deployed, the
`equipment.<role>` key doesn't appear у SharedState. `read_float` returns
default. Always provide а sensible default OR check `_ok` first.

**Writing to `equipment.<role>` instead of `equipment.req_<role>`:**
actuator state keys are read-only. Writing them succeeds (SharedState
accepts) але Equipment Manager overwrites on next tick із the actual
driver state. Always write requests to `req_<role>`.

**Conflating "request" і "actual":** `equipment.req_compressor = true`
asks для compressor ON. `equipment.compressor` reports current state.
They diverge коли driver rejects (compressor min switch interval not yet
elapsed). Always observe `equipment.<role>` to verify, don't trust the
request immediately.

**Persisting calibration accidentally:** `equipment.ntc_beta` is shared
across all NTC sensors. Tuning the global value for one location skews
all others. Stage 1.5 plans per-binding calibration overrides.

## Next steps

- **[board-config.md](../../04-hardware/board-config.md)** — hardware
  capabilities declaration (prerequisite до understanding bindings).
- **[bindings.md](../../04-hardware/bindings.md)** — driver-to-role wiring.
- **[writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md)**
  — implement а new driver for hardware not yet supported.
- **[components/modesp_hal.md](../components/modesp_hal.md)** *(planned)*
  — HAL і DriverManager internals.
- **[components/modesp_core.md](../components/modesp_core.md)** *(planned)*
  — SharedState reference.

## Source

- [`modules/equipment/manifest.json`](../../../../modules/equipment/manifest.json)
- [`modules/equipment/include/equipment_module.h`](../../../../modules/equipment/include/equipment_module.h)
- [`modules/equipment/src/equipment_base.cpp`](../../../../modules/equipment/src/equipment_base.cpp)
