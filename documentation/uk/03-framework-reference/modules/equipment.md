# Equipment Manager

> 📖 **In English:** [documentation/en/03-framework-reference/modules/equipment.md](../../../en/03-framework-reference/modules/equipment.md)

Equipment Manager — це модуль що **bridge-ить hardware drivers і business
модулі**. Читає `bindings.json` при startup, instantiates drivers, polls
sensors / commands actuators на update loop, і exposes everything як
`equipment.*` state keys. Business модулі ніколи не торкаються drivers
напряму — вони читають і пишуть SharedState, і Equipment Manager handles
все I/O.

Ця сторінка документує відповідальності модуля, state keys exposed,
configuration параметри, і як він інтегрується з ширшим hardware pipeline
(board.json → bindings.json → drivers → SharedState).

## Де Equipment fits

```
   board.json + bindings.json
              │
              ▼
   DriverManager створює driver instances
              │
              ▼
   Equipment Manager володіє drivers
              │   читає sensors, commands actuators
              ▼
   equipment.<role> keys у SharedState
              │
              ▼
   Business модулі (thermostat, defrost, alarms)
   читають equipment.<role>, пишуть equipment.req_<role>
```

Equipment — **service module** (priority `HIGH` = 1, запускається у Phase
2 init). Частина фреймворку і ships у `modules/equipment/`.

## State keys exposed

### Sensor keys (per binding із `category: "sensor"`)

| Key pattern | Type | Notes |
|---|---|---|
| `equipment.<role>` | float | Current sensor reading (з calibration applied). |
| `equipment.<role>_ok` | bool | Health: `true` якщо last read succeeded і driver healthy. |

Приклад: bindings із `role: "air_temp"` і `role: "evap_temp"` створюють:
- `equipment.air_temp` (float)
- `equipment.air_temp_ok` (bool)
- `equipment.evap_temp` (float)
- `equipment.evap_temp_ok` (bool)

### Actuator keys (per binding із `category: "actuator"`)

| Key pattern | Type | Notes |
|---|---|---|
| `equipment.<role>` | bool | Current actual state (read-only, reflects driver state). |
| `equipment.req_<role>` | bool | Requested state (writable — business модулі пишуть це). |

Приклад: bindings із `role: "compressor"` і `role: "evap_fan"` створюють:
- `equipment.compressor`, `equipment.req_compressor`
- `equipment.evap_fan`, `equipment.req_evap_fan`

### Driver-presence keys

Використовуються business модулями для detect-ння які drivers bound
(graceful degradation коли hardware absent):

| Key | Notes |
|---|---|
| `equipment.has_ntc_driver` | `true` якщо at least один NTC sensor binding active. |
| `equipment.has_ds18b20_driver` | `true` якщо at least один DS18B20 binding active. |
| `equipment.<driver>_driver` | Один на кожен supported driver type. |

### Configuration keys (persisted)

| Key | Type | Default | Notes |
|---|---|---|---|
| `equipment.filter_coeff` | int | 4 | Digital filter coefficient для sensor smoothing (0=raw..10=heavy). |
| `equipment.ntc_beta` | int | 3950 | NTC B-coefficient. Applies до всіх NTC sensors. |
| `equipment.ntc_r_series` | int | 10000 | NTC series resistor у Ω. |
| `equipment.ntc_r_nominal` | int | 10000 | NTC nominal resistance при 25 °C (Ω). |
| `equipment.ds18b20_offset` | float | 0.0 | Global DS18B20 calibration offset (°C). |

Усі persisted (`persist: true`) — user adjustments survive reboot. WebUI
exposes це як setpoints; MQTT subscribes accept writes для remote tuning.

## Update flow per tick

1. **Driver poll** — call `driver->update(dt_ms)` для кожного bound driver.
   Sensors читають hardware, actuators apply pending state changes.
2. **Read sensors** — `driver->read(value)` для кожного sensor driver;
   apply digital filter і calibration; write `equipment.<role>` і
   `equipment.<role>_ok`.
3. **Forward actuator requests** — для кожного actuator binding, читає
   `equipment.req_<role>` з SharedState і викликає `driver->set(state)`.
   Driver returns success/failure; mirror back до `equipment.<role>`.
4. **Health monitoring** — track consecutive failures per driver; set `_ok`
   у `false` після threshold; surface alarms через error_service для
   critical sensors.

## Digital filter

Sensors readings можуть бути noisy (особливо NTC ADC reads). Equipment
applies first-order IIR filter:

```
filtered = α × raw + (1 - α) × prev_filtered
α = 1 / (filter_coeff + 1)
```

| `filter_coeff` | α | Поведінка |
|---|---|---|
| 0 | 1.0 | Без фільтра (raw reads pass through). |
| 4 (default) | 0.2 | Moderate smoothing, ~5-tick response. |
| 10 | ~0.09 | Heavy smoothing, slow response. |

Поставте `equipment.filter_coeff = 0` якщо хочете raw access (для власного
фільтра або edge detection).

## Health і failures

Drivers report health через `driver->is_healthy()` і `error_count()`.
Equipment Manager:

- Incrementує consecutive-failure counter при кожному failed read.
- Після 3 consecutive failures, ставить `equipment.<role>_ok = false`.
- Reset-ить counter при first successful read; restore-ить `_ok` у `true`.
- Logs ESP_LOGW при кожному transition (уникає log spam).

Business модулі що потребують fail-safe behavior повинні перевіряти `_ok`
перед trust-ингом reading:

```cpp
float temp = 0.0f;
bool sensor_ok = read_bool("equipment.air_temp_ok", false);
if (sensor_ok) {
    temp = read_float("equipment.air_temp", 0.0f);
} else {
    // Fall back: use last-known-good, emergency-safe value, тощо.
}
```

## Configuration через WebUI / MQTT

`mqtt.subscribe` list дозволяє external clients tune calibration без
recompile:

```bash
# Tune NTC B-coefficient через MQTT
mosquitto_pub -t modesp/<device-id>/equipment/ntc_beta -m "3977"
```

Або через HTTP:

```bash
curl -u admin:modesp -X POST http://192.168.1.85/api/settings \
  -d '{"equipment.ntc_beta": 3977}'
```

Або через WebUI's "Equipment" page (auto-generated з manifest's `ui`
section — не показано тут але живе у equipment manifest).

## Customising requires list

Manifest equipment's `requires` декларує **що за kind of bindings модуль
очікує**:

```json
"requires": [
  {"role": "air_temp",    "type": "sensor",   "driver": ["ds18b20", "ntc"], "label": "Temperature sensor"},
  {"role": "actuator_1",  "type": "actuator", "driver": ["relay", "pcf8574_relay"], "label": "Actuator 1", "optional": true}
]
```

| Поле | Notes |
|---|---|
| `role` | Повинен match `role` binding exactly. |
| `type` | `"sensor"` / `"actuator"`. |
| `driver` | Array allowed driver types. Equipment accepts будь-який з них. |
| `label` | Human-readable name (для UI). |
| `optional` | Якщо `true`, missing binding не abort startup. Default `false`. |

Якщо потрібно більше ролей (multiple compressors, multiple temperature
zones), edit equipment маніфест і додайте `requires` entries. Не додавайте
ролі що bindings не fill-ять — `optional: true` тримає двері відкритими
для майбутнього hardware.

## Display integration

Equipment маніфест включає:

```json
"display": {
  "main_value": {
    "key": "equipment.air_temp",
    "format": "%.1f°C"
  }
}
```

На boards з LCD displays, фреймворк рендерить "main value" widget що
показує primary температуру. Driv-ить default screen content LCD.
[components/modesp_hal.md](../components/modesp_hal.md) *(planned)*
документує display integration глибше.

## Поширені патерни

### Перемикання типів sensor-ів per deployment

Та сама прошивка, різні sensor families per deployment: edit bindings щоб
використовувати `ntc` driver для cheap deployments, `ds18b20` для precision.
Business module's `read_float("equipment.air_temp", ...)` works identically.

### Multiple temperature zones

Для multi-zone refrigeration, додайте bindings для кожної zone:

```json
{"role": "air_temp_zone1", "driver": "ds18b20", ..., "address": "..."},
{"role": "air_temp_zone2", "driver": "ds18b20", ..., "address": "..."}
```

Update equipment manifest's `requires` to match. Ваш business module читає
`equipment.air_temp_zone1` і `equipment.air_temp_zone2` як окремі keys.

### Read-only equipment configuration

Деякі сайти lock down equipment params (filter_coeff, calibration constants)
once commissioned. Два options:

1. Remove `persist: true` з маніфесту — persistent setting forced до
   default при кожному boot.
2. Keep `persist: true` але set `access: "read"` — value loads з NVS при
   boot, але no API path accepts writes. Configure once у service mode,
   потім ship.

## Поширені помилки

**Reading missing keys:** якщо role's binding не deployed, key
`equipment.<role>` не з'являється у SharedState. `read_float` returns
default. Завжди надавайте sensible default АБО check `_ok` first.

**Writing у `equipment.<role>` замість `equipment.req_<role>`:** actuator
state keys — read-only. Writing у них succeeds (SharedState accepts) але
Equipment Manager overwrites на next tick з actual driver state. Завжди
write requests у `req_<role>`.

**Conflating "request" і "actual":** `equipment.req_compressor = true`
asks для compressor ON. `equipment.compressor` reports current state.
Вони diverge коли driver rejects (compressor min switch interval not yet
elapsed). Завжди observe `equipment.<role>` щоб verify, не trust request
immediately.

**Persisting calibration accidentally:** `equipment.ntc_beta` shared
across усіх NTC sensors. Tuning global value для однієї location skews
всі інші. Stage 1.5 plans per-binding calibration overrides.

## Що далі

- **[board-config.md](../../04-hardware/board-config.md)** — hardware
  capabilities declaration (prerequisite для understanding bindings).
- **[bindings.md](../../04-hardware/bindings.md)** — driver-to-role wiring.
- **[writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md)**
  — implement новий driver для hardware ще не supported.
- **[components/modesp_hal.md](../components/modesp_hal.md)** *(planned)*
  — HAL і DriverManager internals.
- **[components/modesp_core.md](../components/modesp_core.md)** *(planned)*
  — SharedState reference.

## Source

- [`modules/equipment/manifest.json`](../../../../modules/equipment/manifest.json)
- [`modules/equipment/include/equipment_module.h`](../../../../modules/equipment/include/equipment_module.h)
- [`modules/equipment/src/equipment_base.cpp`](../../../../modules/equipment/src/equipment_base.cpp)
