# `modesp_aws` — AWS IoT Core backend (alternative to MQTT)

> 📖 **Українською:** [documentation/uk/03-framework-reference/components/modesp_aws.md](../../../uk/03-framework-reference/components/modesp_aws.md)

`modesp_aws` is а drop-in alternative to `modesp_mqtt` що connects to
Amazon AWS IoT Core instead of а generic MQTT broker. Same publish /
subscribe contract for business modules — declarations у the `mqtt`
manifest section work identically. Differences are у the transport
layer: mTLS із per-device certificates, AWS-specific topic prefixes
(`$aws/things/...`), і IoT Things integration.

REQUIRES: `modesp_core`, `modesp_services`, `modesp_net`, `mbedtls`,
AWS IoT root CA + device cert + device private key (uploaded via WebUI).

## Cloud backend selection

Compile-time choice через Kconfig:

```ini
# sdkconfig.defaults
CONFIG_MODESP_CLOUD_MQTT=n
CONFIG_MODESP_CLOUD_AWS=y
```

Only one cloud backend builds into the firmware. `main.cpp` includes one
of `modesp/net/mqtt_service.h` або `modesp/net/aws_iot_service.h` based
on the flag.

## `AwsIotService` — module class

```cpp
class AwsIotService : public modesp::BaseModule {
public:
    AwsIotService();

    void set_state(SharedState* state);
    void set_http_server(httpd_handle_t server);

    bool is_connected() const;
};
```

Init priority: HIGH (1), after WiFi. Same shape as MqttService.

## What's different from `modesp_mqtt`

| Aspect | modesp_mqtt | modesp_aws |
|---|---|---|
| Transport | Plain or TLS | mTLS only (cert-required) |
| Broker | Configurable host | AWS IoT endpoint (Kconfig + Thing name) |
| Auth | Username/password або cert | X.509 device cert + private key |
| Topics | `modesp/v1/<tenant>/<id>/...` | `<thing_name>/state/...`, `$aws/things/<thing_name>/shadow/...` |
| HA discovery | Optional | N/A (HA не consumes AWS IoT directly) |
| Shadow document | N/A | Synced із SharedState (Stage 1.5) |

## Configuration

Certificates uploaded via WebUI Network → Cloud → AWS:

- Root CA (Amazon's PEM, public).
- Device cert (issued by IoT Core when registering the Thing).
- Device private key (matches the cert).

Stored у NVS namespace `aws` (key/value, base64 PEM). HTTP `POST /api/cert`
із `type=aws_root_ca`, `aws_device_cert`, `aws_device_key`.

Endpoint configuration у `sdkconfig.defaults`:

```ini
CONFIG_MODESP_AWS_ENDPOINT="a1b2c3d4e5f6-ats.iot.us-east-1.amazonaws.com"
CONFIG_MODESP_AWS_THING_NAME_PREFIX="modesp"  # actual = <prefix>_<device_id>
```

## Topic structure

```
<thing_name>/state/<key_path>         publish (e.g., simple_thermo/temperature)
<thing_name>/cmd/<state_key>          subscribe (write SharedState)
$aws/things/<thing_name>/shadow/...   IoT Thing Shadow (Stage 1.5)
```

`<thing_name>` defaults to `<prefix>_<device_id_lowercase>` (е.g.,
`modesp_a1b2c3`). Configurable.

Shadow document syncing (planned, Stage 1.5):
- SharedState writes mirror to `shadow/update` із reported state.
- Shadow desired-state updates flow back через `shadow/update/delta`
  as commands.
- Enables remote setpoint control through AWS dashboard без а custom
  pub/sub topic.

## State keys

| Key | Notes |
|---|---|
| `aws.connected` | true if IoT Core session active. |
| `aws.thing_name` | This device's Thing name (read-only, derived). |
| `aws.endpoint` | Current IoT endpoint. |
| `aws.publish_count` | Cumulative publishes since boot. |

## Memory і resources

| Resource | Cost |
|---|---|
| AWS SDK client (esp-aws-iot або custom) | ~10 KB heap |
| mTLS session | ~12 KB heap (cert + key + session state) |
| Certificate strings у NVS | ~4-8 KB |

Total ~25 KB. AWS backend is heavier than generic MQTT due to mTLS і
the more verbose Amazon SDK. Plan budget accordingly.

## When to choose AWS over generic MQTT

**AWS:** managed broker із built-in device fleet management, OTA pipelines,
shadow documents, rule engine for cloud-side processing, IAM-based
access control. Heavier RAM cost; mTLS mandatory.

**Generic MQTT:** local broker (Mosquitto, EMQX, HA Mosquitto add-on),
lightweight, full control. Optional TLS.

For а production deployment що integrates із AWS Lambda / IoT Analytics /
Greengrass — choose AWS. For local-only або self-hosted Home Assistant —
choose generic MQTT.

## Author-side perspective

Identical to MQTT. Manifest declarations work the same:

```json
"mqtt": {
  "publish": ["my_module.value"],
  "subscribe": ["my_module.setpoint"]
}
```

Framework picks the active backend і wires accordingly. Business modules
don't care which.

## Common pitfalls

**Cert/key mismatch:** uploaded cert і key don't pair. AWS IoT rejects
connection із `MQTT_ERROR_TLS_HANDSHAKE_FAILED`. Re-generate і re-upload
both from AWS IoT console.

**Endpoint typo:** mistyped IoT endpoint URL. Connection times out
silently. Verify через `aws iot describe-endpoint --endpoint-type
iot:Data-ATS`.

**Thing name mismatch:** policy attached to wrong Thing → connection
accepted but publishes rejected (Forbidden). Check IoT Core console для
attached policy і resource ARNs.

**Memory budget:** building із both modesp_aws і а scenario engine під
WROOM-32 may run tight. Profile із `idf.py size-components`.

## Next steps

- **[components/modesp_mqtt.md](modesp_mqtt.md)** — generic MQTT
  alternative.
- **[02-module-author-guide/mqtt.md](../../02-module-author-guide/mqtt.md)** —
  manifest declarations apply to both backends.

## Source

- [`components/modesp_aws/`](../../../../components/modesp_aws/) — implementation.
