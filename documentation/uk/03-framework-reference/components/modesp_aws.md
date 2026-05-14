# `modesp_aws` — AWS IoT Core backend (alternative до MQTT)

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_aws.md](../../../en/03-framework-reference/components/modesp_aws.md)

`modesp_aws` — drop-in alternative до `modesp_mqtt` що connects до
Amazon AWS IoT Core замість generic MQTT broker. Той самий publish /
subscribe contract для business modules — declarations у `mqtt` секції
маніфесту work identically. Differences у transport layer: mTLS з
per-device certificates, AWS-specific topic prefixes (`$aws/things/...`),
і IoT Things integration.

REQUIRES: `modesp_core`, `modesp_services`, `modesp_net`, `mbedtls`,
AWS IoT root CA + device cert + device private key (uploaded через WebUI).

## Cloud backend selection

Compile-time choice через Kconfig:

```ini
# sdkconfig.defaults
CONFIG_MODESP_CLOUD_MQTT=n
CONFIG_MODESP_CLOUD_AWS=y
```

Лише один cloud backend builds у firmware. `main.cpp` includes one з
`modesp/net/mqtt_service.h` або `modesp/net/aws_iot_service.h` based на
flag.

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

Init priority: HIGH (1), після WiFi. Та сама shape як MqttService.

## Що відрізняється від `modesp_mqtt`

| Aspect | modesp_mqtt | modesp_aws |
|---|---|---|
| Transport | Plain or TLS | mTLS лише (cert-required) |
| Broker | Configurable host | AWS IoT endpoint (Kconfig + Thing name) |
| Auth | Username/password або cert | X.509 device cert + private key |
| Topics | `modesp/v1/<tenant>/<id>/...` | `<thing_name>/state/...`, `$aws/things/<thing_name>/shadow/...` |
| HA discovery | Optional | N/A (HA не consumes AWS IoT directly) |
| Shadow document | N/A | Synced з SharedState (Stage 1.5) |

## Configuration

Certificates uploaded через WebUI Network → Cloud → AWS:

- Root CA (Amazon's PEM, public).
- Device cert (issued by IoT Core коли registering Thing).
- Device private key (matches cert).

Stored у NVS namespace `aws` (key/value, base64 PEM). HTTP `POST /api/cert`
з `type=aws_root_ca`, `aws_device_cert`, `aws_device_key`.

Endpoint configuration у `sdkconfig.defaults`:

```ini
CONFIG_MODESP_AWS_ENDPOINT="a1b2c3d4e5f6-ats.iot.us-east-1.amazonaws.com"
CONFIG_MODESP_AWS_THING_NAME_PREFIX="modesp"  # actual = <prefix>_<device_id>
```

## Topic structure

```
<thing_name>/state/<key_path>         publish (наприклад simple_thermo/temperature)
<thing_name>/cmd/<state_key>          subscribe (write SharedState)
$aws/things/<thing_name>/shadow/...   IoT Thing Shadow (Stage 1.5)
```

`<thing_name>` defaults до `<prefix>_<device_id_lowercase>` (наприклад,
`modesp_a1b2c3`). Configurable.

Shadow document syncing (planned, Stage 1.5):
- SharedState writes mirror до `shadow/update` з reported state.
- Shadow desired-state updates flow back через `shadow/update/delta` як
  commands.
- Enables remote setpoint control через AWS dashboard без custom
  pub/sub topic.

## State keys

| Key | Notes |
|---|---|
| `aws.connected` | true якщо IoT Core session active. |
| `aws.thing_name` | This device's Thing name (read-only, derived). |
| `aws.endpoint` | Current IoT endpoint. |
| `aws.publish_count` | Cumulative publishes since boot. |

## Memory і resources

| Resource | Cost |
|---|---|
| AWS SDK client (esp-aws-iot або custom) | ~10 KB heap |
| mTLS session | ~12 KB heap (cert + key + session state) |
| Certificate strings у NVS | ~4-8 KB |

Total ~25 KB. AWS backend heavier ніж generic MQTT через mTLS і more
verbose Amazon SDK. Plan budget accordingly.

## Коли choose AWS над generic MQTT

**AWS:** managed broker з built-in device fleet management, OTA pipelines,
shadow documents, rule engine для cloud-side processing, IAM-based
access control. Heavier RAM cost; mTLS mandatory.

**Generic MQTT:** local broker (Mosquitto, EMQX, HA Mosquitto add-on),
lightweight, full control. Optional TLS.

Для production deployment що integrates з AWS Lambda / IoT Analytics /
Greengrass — choose AWS. Для local-only або self-hosted Home Assistant —
choose generic MQTT.

## Author-side perspective

Identical до MQTT. Manifest declarations work the same:

```json
"mqtt": {
  "publish": ["my_module.value"],
  "subscribe": ["my_module.setpoint"]
}
```

Framework picks active backend і wires accordingly. Business modules не
care which.

## Common pitfalls

**Cert/key mismatch:** uploaded cert і key не pair. AWS IoT rejects
connection з `MQTT_ERROR_TLS_HANDSHAKE_FAILED`. Re-generate і re-upload
обидва з AWS IoT console.

**Endpoint typo:** mistyped IoT endpoint URL. Connection times out
silently. Verify через `aws iot describe-endpoint --endpoint-type
iot:Data-ATS`.

**Thing name mismatch:** policy attached до wrong Thing → connection
accepted але publishes rejected (Forbidden). Check IoT Core console для
attached policy і resource ARNs.

**Memory budget:** building з обома modesp_aws і scenario engine під
WROOM-32 may run tight. Profile через `idf.py size-components`.

## Що далі

- **[components/modesp_mqtt.md](modesp_mqtt.md)** — generic MQTT
  alternative.
- **[02-module-author-guide/mqtt.md](../../02-module-author-guide/mqtt.md)** —
  manifest declarations apply до обох backends.

## Source

- [`components/modesp_aws/`](../../../../components/modesp_aws/) — implementation.
