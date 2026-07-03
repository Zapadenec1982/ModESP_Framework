# `modesp_mqtt` — MQTT client із TLS і HA discovery

> 📖 **Українською:** [documentation/uk/03-framework-reference/components/modesp_mqtt.md](../../../uk/03-framework-reference/components/modesp_mqtt.md)

`modesp_mqtt` is the default cloud backend. It wraps ESP-IDF's
`esp-mqtt` client AND ties it to SharedState through manifest-declared
publish / subscribe lists. Optional TLS, optional Home Assistant auto
discovery і Last-Will-Testament — all configured
through manifests without explicit MQTT code from business modules.

REQUIRES: `modesp_core`, `modesp_services`, `modesp_net`, `mqtt`,
optionally `mbedtls` для TLS.

Author-side perspective: [02-module-author-guide/mqtt.md](../../02-module-author-guide/mqtt.md).
That page covers the manifest API і usage patterns. This page documents
the implementation внутрішньо.

## Component layout

```
components/modesp_mqtt/include/modesp/net/
├── mqtt_service.h
└── ota_handler.h        (shared із services)
```

Compile-time choice: `CONFIG_MODESP_CLOUD_MQTT` (default) selects це
component; `CONFIG_MODESP_CLOUD_AWS` selects modesp_aws instead. Only
one builds into а given firmware.

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

Init priority: HIGH (1) — runs у Phase 2, after WiFi.

### Timing constants

```cpp
static constexpr uint32_t PUBLISH_INTERVAL_MS = 1000;          // 1 s
static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 30000;       // 30 s
```

Adjustable via Kconfig if needed. Defaults are conservative — chatty
deployments may want faster publish intervals.

### Topic format

```
modesp/v1/<tenant>/<device_id>/<state_key_path>
modesp/v1/<tenant>/<device_id>/cmd/<state_key>
modesp/v1/<tenant>/<device_id>/status
```

- `<tenant>` defaults to `default` (Kconfig `CONFIG_MODESP_MQTT_TENANT`).
- `<device_id>` derived з MAC lowercase last 6 chars (e.g., `a1b2c3`).
- `<state_key_path>` із dots replaced by slashes
  (`simple_thermo.temperature` → `simple_thermo/temperature`).
- `cmd/` topic preserves the dot in the key (legacy format; will
  normalise у Stage 1.5).

### Connection lifecycle

1. **on_init:** reads broker config з NVS (`mqtt` namespace) і у
   compile-time defaults. Doesn't connect yet — waits for WiFi.
2. **on_update:** polls `wifi.connected`. Once true, starts esp-mqtt
   client із LWT registered (`<base>/status` = `"offline"`).
3. **MQTT_EVENT_CONNECTED:** publishes `"online"` to status topic (QoS 1,
   retained), subscribes to `<base>/cmd/+`, publishes HA discovery
   (if enabled).
4. **MQTT_EVENT_DATA:** parses topic, validates key against allowlist,
   writes до SharedState.
5. **MQTT_EVENT_DISCONNECTED:** backs off, retries. Reconnect attempts
   exponential up to ~60 s.

### Publish loop

Every `PUBLISH_INTERVAL_MS`:

1. Iterate the merged publish allowlist (з `mqtt_topics.h`).
2. For each key, check SharedState's changed-keys set.
3. Publish changed keys із QoS 0 (delta semantics).

Heartbeat runs on its own timer. Reliable delivery (QoS 1 + retain) for
alarm-class keys returns as a manifest-driven flag in Phase 2 of the
universality roadmap; the old hardcoded 'protection.' prefix matched
nothing and was removed.

### HA discovery

Якщо `CONFIG_MODESP_MQTT_HA_DISCOVERY=y`, on connect MqttService publishes
а discovery payload per state key:

```
homeassistant/sensor/modesp_a1b2c3_simple_thermo_temperature/config
```

Payload includes unit (`°C`, `%`, etc.), device class (`temperature`,
`humidity`, …), state topic, value template, і а common `device` block
що groups all entities від one device.

HA auto-creates entities. No manual setup needed.

### TLS і AWS contrast

Generic MQTT can run plain TCP (port 1883) або TLS (port 8883) із а
configurable CA cert. AWS IoT is а separate component (modesp_aws) із
mTLS і Amazon-specific topic prefixes — same publish/subscribe contract
від module-author perspective.

## HTTP API integration

| Method + path | Purpose |
|---|---|
| `GET /api/mqtt` | Return current broker config (sans password). |
| `POST /api/mqtt` | Update broker host/port/credentials. Reconnects. |

Cert upload (TLS): `POST /api/cert` із PEM payload, type=`mqtt_ca`.

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
| TLS session (if enabled) | ~8 KB heap |

Total: ~7 KB without TLS, ~15 KB із TLS. Be mindful on
RAM-constrained boards.

## Common pitfalls

**Broker hostname unresolvable:** ensure mDNS or local DNS works.
Hardcode IPs in production.

**Subscribe key not whitelisted:** missing `mqtt_subscribe: true` flag.
Generator's `mqtt_topics.h` won't include the key у allowlist; MQTT
ignores incoming commands.

**HA entities not appearing:** check `CONFIG_MODESP_MQTT_HA_DISCOVERY`
enabled AND broker subscribes to `homeassistant/#`. HA must restart to
pick up new discovery topics if they arrived before HA started.

**TLS handshake failures:** check certificate validity і system clock
(SNTP sync). Expired або pre-epoch time fails TLS auth.

## Next steps

- **[02-module-author-guide/mqtt.md](../../02-module-author-guide/mqtt.md)** —
  manifest-side reference.
- **[components/modesp_aws.md](modesp_aws.md)** — AWS IoT alternative.
- **[components/modesp_net.md](modesp_net.md)** — networking prerequisite.

## Source

- [`components/modesp_mqtt/`](../../../../components/modesp_mqtt/) — implementation.
- `mqtt_service.cpp` ~1000 LOC including HA discovery і HTTP handlers.
