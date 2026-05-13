# MQTT publish і subscribe

> 📖 **In English:** [documentation/en/02-module-author-guide/mqtt.md](../../en/02-module-author-guide/mqtt.md)

ModESP включає built-in MQTT client що bridge-ить SharedState ↔ MQTT broker
автоматично. Ви декларуєте які state keys публікуються і які приймають
writes у `mqtt` секції вашого маніфесту, і фреймворк handles connection,
topic naming, delta detection, throttling, і Last-Will-Testament — без
custom MQTT коду від вас.

Ця сторінка пояснює publish/subscribe модель, конвенції topic, delta
семантику, alarm retention, і інтеграцію з Home Assistant discovery.

## Ментальна модель

```
   Module пише state_set("simple_thermo.temp", 22.5)
                  │
                  ▼
   SharedState fires change tracking
                  │
                  ▼ (кожні 1 с за замовчуванням)
   MqttService публікує changed keys
                  │
                  ▼
   Broker (Mosquitto / AWS IoT / тощо)
                  │
                  ▼
   External subscriber отримує modesp/v1/<device>/simple_thermo/temp = 22.5
```

Reverse direction (commands):

```
   External publisher → modesp/v1/<device>/cmd/simple_thermo.setpoint = 24
                  │
                  ▼
   MqttService отримує
                  │
                  ▼
   SharedState set("simple_thermo.setpoint", 24)
                  │
                  ▼
   Module читає new setpoint наступний tick
```

## Manifest декларація

```json
"mqtt": {
  "publish": [
    "simple_thermo.temperature",
    "simple_thermo.state",
    "simple_thermo.output"
  ],
  "subscribe": [
    "simple_thermo.setpoint",
    "simple_thermo.differential"
  ]
}
```

| Поле | Notes |
|---|---|
| `publish` | Array state keys для broadcast до MQTT. Публікується лише при зміні (delta), не періодично. |
| `subscribe` | Array state keys що приймають MQTT-pushed writes. Кожен key повинен мати `access: "readwrite"` AND `mqtt_subscribe: true` у його `state` декларації. |

### Вимога `mqtt_subscribe: true`

У вашій state декларації:

```json
"simple_thermo.setpoint": {
  "type": "float",
  "access": "readwrite",
  "mqtt_subscribe": true,         // ← required для MQTT writes
  "min": 5, "max": 40
}
```

Цей double-opt-in запобігає випадково exposing internal config keys до
external write. Генератор валідує: будь-який key listed у `mqtt.subscribe`
повинен мати флаг.

## Topic format

```
<base>/<state_key_path>
```

Де:
- `<base>` follows pattern `modesp/v1/<tenant>/<device_id>` (configurable
  через Kconfig). `<device_id>` defaults до lowercase MAC suffix.
- `<state_key_path>` — state key з dots replaced на slashes:
  `simple_thermo.temperature` → `simple_thermo/temperature`.

Приклад для пристрою з MAC закінчуючись `A1B2C3` і default tenant:

```
modesp/v1/default/a1b2c3/simple_thermo/temperature
modesp/v1/default/a1b2c3/simple_thermo/state
modesp/v1/default/a1b2c3/cmd/simple_thermo.setpoint
```

Subscribe (commands) використовує wildcard:

```
modesp/v1/default/a1b2c3/cmd/+
```

Single device subscribes до одного wildcard, handles усі `cmd/<key>`
writes через one topic замість N subscriptions. Stage 1.5 plans більш
granular subscription patterns якщо треба.

## Delta семантика

`MqttService` runs на standard module update loop. Кожні
`PUBLISH_INTERVAL_MS` (default 1000 мс) він:

1. Iterates `publish` list across усіх модулів.
2. Для кожного key, checks `SharedState`'s changed-keys vector.
3. Публікує ЛИШЕ keys що змінились після last tick.

Результуючий traffic pattern: idle device публікує нічого. Active device
публікує одне message per changed key per second max. Brokers не бачать
flood навіть з 100 keys у publish list якщо нічого не змінюється.

## Heartbeat

Кожні `HEARTBEAT_INTERVAL_MS` (default 30 000 мс = 30 секунд) service
публікує heartbeat до `<base>/status`:

```
modesp/v1/default/a1b2c3/status  →  "online" (retained, QoS 1)
```

Plus Last-Will-Testament (LWT) configured to publish `"offline"`
автоматично коли broker detects connection loss (60-second keepalive).

Це дає external systems cheap "пристрій alive?" check без parse-ння state
stream.

## Retained alarms

Critical alarms (over-temperature, sensor fault, тощо) get **republished
every 5 minutes з retain flag**. Reasoning: transient subscriber що
connect-иться пізно повинен immediately бачити active alarms, не чекати
наступної зміни. Periodic republish ensures retained value stays fresh.

Alarm subsystem фреймворку (Stage 1.5) marks specific keys як "alarm-class"
— вони йдуть через цей pathway. Regular state keys publish normally без
retain.

## Home Assistant integration

Якщо `CONFIG_MODESP_MQTT_HA_DISCOVERY=y` (Kconfig), service публікує HA
discovery messages при first connect:

```
homeassistant/sensor/modesp_a1b2c3_simple_thermo_temperature/config
```

Кожен `mqtt.publish` key emits HA discovery payload з proper unit, device
class, і device grouping. Home Assistant auto-creates entities — без manual
configuration на HA side.

Це Stage 1 feature; coverage деталі у
[components/modesp_mqtt.md](../03-framework-reference/components/modesp_mqtt.md)
*(planned)*.

## Cloud backend selection

Compile-time choice через Kconfig: `CONFIG_MODESP_CLOUD_MQTT` (default)
або `CONFIG_MODESP_CLOUD_AWS`. Обидва implement той самий publish/subscribe
контракт з module author's perspective — ваші manifest declarations
залишаються identical:

| Backend | Component | Notes |
|---|---|---|
| Generic MQTT | `modesp_mqtt` | Plain or TLS-encrypted broker, configurable. |
| AWS IoT Core | `modesp_aws` | Cert-based auth, AWS-specific topic prefixes, IoT Things integration. |

Switch backends без зміни маніфестів; лише один builds у firmware одночасно.

## Command path детально

Коли command надходить на `<base>/cmd/<key>`:

1. Service парсить topic щоб extract `<key>`.
2. Валідує `<key>` у merged `mqtt.subscribe` set across усіх модулів
   (згенерований `mqtt_topics.h` має allowlist).
3. Парсить payload відповідно до state key's type:
   - `int` → `atoi(payload)`
   - `float` → `atof(payload)`
   - `bool` → `"true"`/`"1"` → true, else false
   - `string` → as-is (clamped до 32 chars)
4. Викликає `SharedState::set(key, parsed_value)`.

Payload format — plain ASCII, не JSON. Subscribe до integer setpoint:
publish `"24"` не `"{"value": 24}"`. Просто, без parser dependency.

Якщо parsing fails або key не у allowlist, service логує warning і drops
command. Без error reply path назад (MQTT — fire-and-forget by design).

## QoS і retention

| Topic type | QoS | Retain |
|---|---|---|
| State publish (delta) | 0 | No |
| Heartbeat | 1 | Yes |
| LWT (offline) | 1 | Yes |
| Alarm publish | 1 | Yes |
| HA discovery | 0 | Yes |
| Commands (subscribe) | 0 | — |

QoS 0 для high-frequency state — втрата одного update не має значення
(next delta reflect-ить current state). QoS 1 для status / alarms —
гарантує delivery once.

## Що модулі не повинні робити

Модулі пишуть state keys через `state_set`. Це все. Фреймворк:

- Manages MQTT connection (reconnect, TLS, keepalive).
- Decides що publish based на маніфестах.
- Generates topic strings (ви ніколи не construct one).
- Throttles до delta-only.
- Handles incoming commands (subscribed keys auto-write SharedState).
- Maintains LWT / heartbeat.
- Publishes HA discovery (якщо enabled).
- Republishes alarms періодично.

Ви декларуєте intent (publish / subscribe lists) і пишете state. Без
client init, без topic format strings, без callbacks. Той самий код
runs offline (без broker) — publish calls просто стають no-ops.

## Конфігурація через WebUI / API

Broker connection settings живуть у NVS і можуть бути edited:

```bash
curl -u admin:modesp -X POST http://192.168.1.85/api/mqtt \
  -d '{"host": "mqtt.example.com", "port": 1883, "user": "iot", "password": "..."}'
```

WebUI's "Network → MQTT" page wraps це у `mqtt_save` widget. Після save,
service reconnects автоматично.

TLS і AWS IoT setup додають certificate uploads через `cert_upload` widget.

## Поширені помилки

**Забутий `mqtt_subscribe: true`:** key у `mqtt.subscribe` але missing
флаг — генератор rejects при build з clear error.

**Subscribing на read-only key:** `access: "read"` keys не можуть accept
writes. Якщо key both reports state і accepts commands, declare його
`readwrite` AND distinguish через окремий "actual" key
(`equipment.compressor` vs. `equipment.req_compressor`).

**Очікування JSON payload:** commands take plain ASCII. Publishing
`{"value": 24}` для встановлення setpoint fails to parse — atof returns
0.0.

**Confused topic format:** dots стають slashes, але НЕ у `cmd/` direction.
`cmd/simple_thermo.setpoint` тримає dot (це literal state key name).
Inconsistency з compatibility з попередньою version; Stage 1.5 буде
normalise.

**Trusting MQTT для atomic transactions:** MQTT delivers messages
asynchronously. Writing 3 keys у quick succession не гарантує що вони
arrive together на subscriber. Для atomic state transitions use scenario
recipe замість, або single composite key (наприклад, JSON-encoded string
у одному key, parsed вашим модулем).

## Debug і diagnostics

Monitor traffic з dev machine:

```bash
mosquitto_sub -h <broker-ip> -t 'modesp/v1/+/+/#' -v
```

Filters усі device messages. Use specific topics щоб narrow:

```bash
mosquitto_sub -h <broker-ip> -t 'modesp/v1/default/a1b2c3/simple_thermo/+' -v
```

Manual command publish:

```bash
mosquitto_pub -h <broker-ip> -t 'modesp/v1/default/a1b2c3/cmd/simple_thermo.setpoint' -m '23.5'
```

Watch monitor пристрою для confirmation log lines.

## Що далі

- **[manifest.md](manifest.md#section-mqtt-service-modules)** — manifest
  reference для `mqtt` section.
- **[shared-state.md](shared-state.md)** — що публікується і subscribe-иться.
- **[persistence.md](persistence.md)** *(planned)* — MQTT writes що
  повинні survive reboot потребують `persist: true`.
- **[components/modesp_mqtt.md](../03-framework-reference/components/modesp_mqtt.md)**
  *(planned)* — internal MQTT service implementation і tuning options.
- **[components/modesp_aws.md](../03-framework-reference/components/modesp_aws.md)**
  *(planned)* — AWS IoT alternative backend.

## Source

- [`components/modesp_mqtt/src/mqtt_service.cpp`](../../../components/modesp_mqtt/src/mqtt_service.cpp)
  — implementation. Constants: `PUBLISH_INTERVAL_MS`,
  `HEARTBEAT_INTERVAL_MS`, `ALARM_REPUBLISH_INTERVAL_MS` у header.
- [`modules/simple_thermo/manifest.json`](../../../modules/simple_thermo/manifest.json)
  — typical pub/sub приклад.
