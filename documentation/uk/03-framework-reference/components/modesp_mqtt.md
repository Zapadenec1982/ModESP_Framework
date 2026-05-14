# `modesp_mqtt` — MQTT client з TLS і HA discovery

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_mqtt.md](../../../en/03-framework-reference/components/modesp_mqtt.md)

`modesp_mqtt` — default cloud backend. Wraps ESP-IDF's `esp-mqtt`
client AND ties його до SharedState через manifest-declared publish /
subscribe lists. Optional TLS, optional Home Assistant auto discovery,
retained alarms, і Last-Will-Testament — все configured через
маніфести без explicit MQTT коду від business modules.

REQUIRES: `modesp_core`, `modesp_services`, `modesp_net`, `mqtt`,
optionally `mbedtls` для TLS.

Author-side perspective: [02-module-author-guide/mqtt.md](../../02-module-author-guide/mqtt.md).
Та сторінка покриває manifest API і usage patterns. Ця сторінка
документує implementation внутрішньо.

## Component layout

```
components/modesp_mqtt/include/modesp/net/
├── mqtt_service.h
└── ota_handler.h        (shared із services)
```

Compile-time choice: `CONFIG_MODESP_CLOUD_MQTT` (default) selects цей
component; `CONFIG_MODESP_CLOUD_AWS` selects modesp_aws замість. Тільки
один builds у given firmware.

## `MqttService` — module class

```cpp
class MqttService : public modesp::BaseModule {
public:
    MqttService();

    void set_state(SharedState* state);
    void set_http_server(httpd_handle_t server);

    bool is_connected() const;
};
```

Init priority: HIGH (1) — runs у Phase 2, після WiFi.

### Timing константи

```cpp
static constexpr uint32_t PUBLISH_INTERVAL_MS = 1000;          // 1 с
static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 30000;       // 30 с
static constexpr uint32_t ALARM_REPUBLISH_INTERVAL_MS = 300000; // 5 хв
```

Adjustable через Kconfig якщо треба. Defaults conservative — chatty
deployments may want faster publish intervals.

### Topic format

```
modesp/v1/<tenant>/<device_id>/<state_key_path>
modesp/v1/<tenant>/<device_id>/cmd/<state_key>
modesp/v1/<tenant>/<device_id>/status
```

- `<tenant>` defaults до `default` (Kconfig `CONFIG_MODESP_MQTT_TENANT`).
- `<device_id>` derived з MAC lowercase last 6 chars (e.g., `a1b2c3`).
- `<state_key_path>` з dots replaced by slashes
  (`simple_thermo.temperature` → `simple_thermo/temperature`).
- `cmd/` topic preserves dot у key (legacy format; буде normalise у
  Stage 1.5).

### Connection lifecycle

1. **on_init:** reads broker config з NVS (`mqtt` namespace) і у
   compile-time defaults. Не connect yet — waits for WiFi.
2. **on_update:** polls `wifi.connected`. Once true, starts esp-mqtt
   client із LWT registered (`<base>/status` = `"offline"`).
3. **MQTT_EVENT_CONNECTED:** publishes `"online"` до status topic (QoS 1,
   retained), subscribes до `<base>/cmd/+`, publishes HA discovery
   (якщо enabled).
4. **MQTT_EVENT_DATA:** parses topic, валідує key проти allowlist,
   writes до SharedState.
5. **MQTT_EVENT_DISCONNECTED:** backs off, retries. Reconnect attempts
   exponential up to ~60 с.

### Publish loop

Кожні `PUBLISH_INTERVAL_MS`:

1. Iterate merged publish allowlist (з `mqtt_topics.h`).
2. Для кожного key, check SharedState's changed-keys set.
3. Publish changed keys із QoS 0 (delta semantics).

Heartbeat і alarm-republish — окремі timers; alarm keys що match list у
`CONFIG_MODESP_MQTT_ALARM_KEYS` (TBD у Stage 1.5) отримують retained
publishes кожні 5 хв.

### HA discovery

Якщо `CONFIG_MODESP_MQTT_HA_DISCOVERY=y`, on connect MqttService publishes
discovery payload per state key:

```
homeassistant/sensor/modesp_a1b2c3_simple_thermo_temperature/config
```

Payload includes unit (`°C`, `%`, etc.), device class (`temperature`,
`humidity`, …), state topic, value template, і common `device` block що
groups усі entities з одного device.

HA auto-creates entities. Без manual setup.

### TLS і AWS contrast

Generic MQTT може run plain TCP (port 1883) або TLS (port 8883) з
configurable CA cert. AWS IoT — окремий component (modesp_aws) з mTLS
і Amazon-specific topic prefixes — той самий publish/subscribe contract
з module-author perspective.

## HTTP API integration

| Method + path | Purpose |
|---|---|
| `GET /api/mqtt` | Return current broker config (sans password). |
| `POST /api/mqtt` | Update broker host/port/credentials. Reconnects. |

Cert upload (TLS): `POST /api/cert` з PEM payload, type=`mqtt_ca`.

## State keys

| Key | Type | Notes |
|---|---|---|
| `mqtt.connected` | bool | Broker connection status. |
| `mqtt.broker_host` | string | Current broker hostname. |
| `mqtt.broker_port` | int | Current port. |
| `mqtt.publish_count` | int | Cumulative publishes since boot. |
| `mqtt.last_disconnect_s` | int | Seconds since last disconnect event. |
| `mqtt.subscribe_topic` | string | Current subscribe wildcard (`<base>/cmd/+`). |

## Memory і resources

| Resource | Cost |
|---|---|
| esp-mqtt client | ~6 KB heap + own task |
| Topic prefix і allowlists | ~1 KB у `mqtt_topics.h` |
| TLS session (якщо enabled) | ~8 KB heap |

Total: ~7 KB без TLS, ~15 KB із TLS. Be mindful на RAM-constrained boards.

## Common pitfalls

**Broker hostname unresolvable:** ensure mDNS або local DNS works.
Hardcode IPs у production.

**Subscribe key not whitelisted:** missing `mqtt_subscribe: true` flag.
Generator's `mqtt_topics.h` не include key у allowlist; MQTT ignores
incoming commands.

**HA entities not appearing:** check `CONFIG_MODESP_MQTT_HA_DISCOVERY`
enabled AND broker subscribes до `homeassistant/#`. HA must restart щоб
pick up new discovery topics якщо вони arrived before HA started.

**TLS handshake failures:** check certificate validity і system clock
(SNTP sync). Expired або pre-epoch time fails TLS auth.

## Що далі

- **[02-module-author-guide/mqtt.md](../../02-module-author-guide/mqtt.md)** —
  manifest-side reference.
- **[components/modesp_aws.md](modesp_aws.md)** — AWS IoT alternative.
- **[components/modesp_net.md](modesp_net.md)** — networking prerequisite.

## Source

- [`components/modesp_mqtt/`](../../../../components/modesp_mqtt/) — implementation.
- `mqtt_service.cpp` ~1000 LOC including HA discovery і HTTP handlers.
