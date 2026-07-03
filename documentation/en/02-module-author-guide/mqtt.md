# MQTT publish і subscribe

> 📖 **Українською:** [documentation/uk/02-module-author-guide/mqtt.md](../../uk/02-module-author-guide/mqtt.md)

ModESP includes а built-in MQTT client що bridges SharedState ↔ MQTT broker
automatically. You declare which state keys publish і which accept writes
у your manifest's `mqtt` section, і the framework handles connection,
topic naming, delta detection, throttling, і Last-Will-Testament — no
custom MQTT code from you.

This page explains the publish/subscribe model, topic conventions, delta
semantics, alarm retention, і integration із Home Assistant discovery.

## Mental model

```
   Module writes state_set("simple_thermo.temp", 22.5)
                  │
                  ▼
   SharedState fires change tracking
                  │
                  ▼ (every 1 s by default)
   MqttService publishes changed keys
                  │
                  ▼
   Broker (Mosquitto / AWS IoT / etc.)
                  │
                  ▼
   External subscriber receives modesp/v1/<device>/simple_thermo/temp = 22.5
```

Reverse direction (commands):

```
   External publisher → modesp/v1/<device>/cmd/simple_thermo.setpoint = 24
                  │
                  ▼
   MqttService receives
                  │
                  ▼
   SharedState set("simple_thermo.setpoint", 24)
                  │
                  ▼
   Module reads new setpoint next tick
```

## Manifest declaration

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

| Field | Notes |
|---|---|
| `publish` | Array of state keys to broadcast to MQTT. Published only on change (delta), not periodically. |
| `subscribe` | Array of state keys що accept MQTT-pushed writes. Each key must have `access: "readwrite"` AND `mqtt_subscribe: true` у its `state` declaration. |

### `mqtt_subscribe: true` requirement

In your state declaration:

```json
"simple_thermo.setpoint": {
  "type": "float",
  "access": "readwrite",
  "mqtt_subscribe": true,         // ← required для MQTT writes
  "min": 5, "max": 40
}
```

This double-opt-in prevents accidentally exposing internal config keys to
external write. Generator validates: any key listed у `mqtt.subscribe`
must have the flag.

## Topic format

```
<base>/<state_key_path>
```

Where:
- `<base>` follows pattern `modesp/v1/<tenant>/<device_id>` (configurable
  via Kconfig). `<device_id>` defaults to lowercase MAC suffix.
- `<state_key_path>` is the state key із dots replaced by slashes:
  `simple_thermo.temperature` → `simple_thermo/temperature`.

Example для device із MAC ending `A1B2C3` and default tenant:

```
modesp/v1/default/a1b2c3/simple_thermo/temperature
modesp/v1/default/a1b2c3/simple_thermo/state
modesp/v1/default/a1b2c3/cmd/simple_thermo.setpoint
```

Subscribe (commands) uses а wildcard:

```
modesp/v1/default/a1b2c3/cmd/+
```

Single device subscribes to one wildcard, handles all `cmd/<key>` writes
through one topic instead of N subscriptions. Stage 1.5 plans more granular
subscription patterns if needed.

## Delta semantics

`MqttService` runs on the standard module update loop. Every
`PUBLISH_INTERVAL_MS` (default 1000 ms) it:

1. Iterates the `publish` list across all modules.
2. For each key, checks `SharedState`'s changed-keys vector.
3. Publishes ONLY keys що changed since the last tick.

Resulting traffic pattern: idle device publishes nothing. Active device
publishes одна message per changed key per second max. Brokers don't see
а flood даже з 100 keys у the publish list якщо nothing changes.

## Heartbeat

Every `HEARTBEAT_INTERVAL_MS` (default 30 000 ms = 30 seconds) the service
publishes а heartbeat to `<base>/status`:

```
modesp/v1/default/a1b2c3/status  →  "online" (retained, QoS 1)
```

Plus Last-Will-Testament (LWT) configured to publish `"offline"` automatically
when the broker detects connection loss (60-second keepalive).

This gives external systems а cheap "is the device alive?" check без
parsing the state stream.

## Alarms: QoS 1 + retain (manifest-driven)

Alarm-class keys are declared in the manifest — `mqtt.alarm` section
(a subset of `mqtt.publish`):

```json
"mqtt": {
  "publish": ["my_mod.overheat"],
  "alarm":   ["my_mod.overheat"]
}
```

The generator emits `gen::MQTT_PUBLISH_ALARM[]`; such keys publish with
**QoS 1 + retain** — a subscriber that connects late immediately sees the
active alarm. Regular state keys publish QoS 0 without retain.

## Home Assistant integration

HA discovery is **fully manifest-driven**: the `mqtt.ha` section maps a
publish key to entity metadata (the generator emits `gen::HA_ENTITIES[]`;
`MqttService` publishes discovery on every connect):

```json
"mqtt": {
  "publish": ["my_mod.temp"],
  "ha": {
    "my_mod.temp": {"name": "Temperature", "component": "sensor",
                    "device_class": "temperature", "state_class": "measurement"}
  }
}
```

`component`: `sensor` | `binary_sensor`. `unit` defaults to the state key's
declared unit. The key must be in `mqtt.publish` (validated). The topic /
identifier root comes from `system.mqtt_topic_root` in project.json
(default `modesp`).

## Cloud backend selection

Compile-time choice via Kconfig: `CONFIG_MODESP_CLOUD_MQTT` (default) або
`CONFIG_MODESP_CLOUD_AWS`. Both implement the same publish/subscribe
contract from а module author's perspective — your manifest declarations
stay identical:

| Backend | Component | Notes |
|---|---|---|
| Generic MQTT | `modesp_mqtt` | Plain or TLS-encrypted broker, configurable. |
| AWS IoT Core | `modesp_aws` | Cert-based auth, AWS-specific topic prefixes, IoT Things integration. |

Switch backends without changing manifests; only one builds into the
firmware at а time.

## Command path detail

When а command arrives on `<base>/cmd/<key>`:

1. Service parses the topic to extract `<key>`.
2. Validates `<key>` is у the merged `mqtt.subscribe` set across all
   modules (generated `mqtt_topics.h` has the allowlist).
3. Parses the payload according to the state key's type:
   - `int` → `atoi(payload)`
   - `float` → `atof(payload)`
   - `bool` → `"true"`/`"1"` → true, else false
   - `string` → as-is (clamped to 32 chars)
4. Calls `SharedState::set(key, parsed_value)`.

Payload format is plain ASCII — not JSON. Subscribe to integer setpoint:
publish `"24"` not `"{"value": 24}"`. Simple, no parser dependency.

If parsing fails or key isn't у allowlist, service logs а warning and drops
the command. No error reply path back (MQTT is fire-and-forget by design).

## QoS і retention

| Topic type | QoS | Retain |
|---|---|---|
| State publish (delta) | 0 | No |
| Heartbeat | 1 | Yes |
| LWT (offline) | 1 | Yes |
| Alarm publish | 1 | Yes |
| HA discovery | 0 | Yes |
| Commands (subscribe) | 0 | — |

QoS 0 для high-frequency state — losing один update doesn't matter (next
delta will reflect current state). QoS 1 для status / alarms — guarantees
delivery once.

## What modules don't need to do

Modules write state keys через `state_set`. That's it. The framework:

- Manages the MQTT connection (reconnect, TLS, keepalive).
- Decides what to publish based on manifests.
- Generates topic strings (you never construct one).
- Throttles to delta-only.
- Handles incoming commands (subscribed keys auto-write SharedState).
- Maintains LWT / heartbeat.
- Publishes HA discovery (if enabled).
- Republishes alarms periodically.

You declare intent (publish / subscribe lists) і write state. No client
init, no topic format strings, no callbacks. Same code runs offline (without
а broker) — publish calls just become no-ops.

## Configuration via WebUI / API

Broker connection settings live у NVS і can be edited:

```bash
curl -u admin:modesp -X POST http://192.168.1.85/api/mqtt \
  -d '{"host": "mqtt.example.com", "port": 1883, "user": "iot", "password": "..."}'
```

The WebUI's "Network → MQTT" page wraps це у а `mqtt_save` widget. After
save, service reconnects automatically.

TLS і AWS IoT setup add certificate uploads через `cert_upload` widget.

## Common mistakes

**Forgetting `mqtt_subscribe: true`:** key у `mqtt.subscribe` but missing
the flag — generator rejects at build із а clear error.

**Subscribing to read-only key:** `access: "read"` keys can't accept
writes. If а key both reports state і accepts commands, declare it
`readwrite` AND distinguish via а separate "actual" key (`equipment.compressor`
vs. `equipment.req_compressor`).

**Expecting JSON payload:** commands take plain ASCII. Publishing
`{"value": 24}` to set а setpoint fails to parse — atof returns 0.0.

**Confused topic format:** dots become slashes, but NOT у the `cmd/`
direction. `cmd/simple_thermo.setpoint` keeps the dot (it's the literal
state key name). Inconsistency from compatibility із а previous version;
Stage 1.5 will normalise.

**Trusting MQTT for atomic transactions:** MQTT delivers messages
asynchronously. Writing 3 keys у quick succession doesn't guarantee они
arrive together on the subscriber. For atomic state transitions use а
scenario recipe instead, або а single composite key (е.g., а JSON-encoded
string у one key, parsed by your module).

## Debug і diagnostics

Monitor traffic from а dev machine:

```bash
mosquitto_sub -h <broker-ip> -t 'modesp/v1/+/+/#' -v
```

Filters all device messages. Use specific topics to narrow:

```bash
mosquitto_sub -h <broker-ip> -t 'modesp/v1/default/a1b2c3/simple_thermo/+' -v
```

Manual command publish:

```bash
mosquitto_pub -h <broker-ip> -t 'modesp/v1/default/a1b2c3/cmd/simple_thermo.setpoint' -m '23.5'
```

Watch the device's monitor для confirmation log lines.

## Next steps

- **[manifest.md](manifest.md#section-mqtt-service-modules)** — manifest
  reference для `mqtt` section.
- **[shared-state.md](shared-state.md)** — what's published і subscribed.
- **[persistence.md](persistence.md)** *(planned)* — MQTT writes that
  should survive reboot need `persist: true`.
- **[components/modesp_mqtt.md](../03-framework-reference/components/modesp_mqtt.md)**
  *(planned)* — internal MQTT service implementation і tuning options.
- **[components/modesp_aws.md](../03-framework-reference/components/modesp_aws.md)**
  *(planned)* — AWS IoT alternative backend.

## Source

- [`components/modesp_mqtt/src/mqtt_service.cpp`](../../../components/modesp_mqtt/src/mqtt_service.cpp)
  — implementation. Constants: `PUBLISH_INTERVAL_MS`,
  `HEARTBEAT_INTERVAL_MS`, `ALARM_REPUBLISH_INTERVAL_MS` у the header.
- [`modules/simple_thermo/manifest.json`](../../../modules/simple_thermo/manifest.json)
  — typical pub/sub example.
