# `modesp_aws` — бекенд AWS IoT Core (альтернатива MQTT)

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_aws.md](../../../en/03-framework-reference/components/modesp_aws.md)

`modesp_aws` — це повноцінна заміна `modesp_mqtt`, яка з'єднується з
Amazon AWS IoT Core замість звичайного MQTT-брокера. Той самий
контракт publish / subscribe для бізнес-модулів — оголошення у секції
`mqtt` маніфесту працюють однаково. Відмінності — у транспортному
рівні: mTLS з сертифікатами на кожен пристрій, специфічні для AWS
префікси топіків (`$aws/things/...`) та інтеграція з IoT Things.

ЗАЛЕЖНОСТІ: `modesp_core`, `modesp_services`, `modesp_net`, `mbedtls`,
кореневий CA AWS IoT + сертифікат пристрою + приватний ключ пристрою
(завантажуються через WebUI).

## Вибір хмарного бекенду

Вибір на етапі компіляції через Kconfig:

```ini
# sdkconfig.defaults
CONFIG_MODESP_CLOUD_MQTT=n
CONFIG_MODESP_CLOUD_AWS=y
```

У прошивку збирається лише один хмарний бекенд. `main.cpp` підключає
або `modesp/net/mqtt_service.h`, або `modesp/net/aws_iot_service.h`
залежно від прапорця.

## `AwsIotService` — клас модуля

```cpp
class AwsIotService : public modesp::BaseModule {
public:
    AwsIotService();

    void set_state(SharedState* state);
    void set_http_server(httpd_handle_t server);

    bool is_connected() const;
};
```

Пріоритет ініціалізації: HIGH (1), після Wi-Fi. Та сама форма, що й у
MqttService.

## Чим відрізняється від `modesp_mqtt`

| Аспект | modesp_mqtt | modesp_aws |
|---|---|---|
| Транспорт | Звичайний або TLS | Лише mTLS (потрібен сертифікат) |
| Брокер | Налаштовуваний хост | Кінцева точка AWS IoT (Kconfig + ім'я Thing) |
| Автентифікація | Логін/пароль або сертифікат | Сертифікат пристрою X.509 + приватний ключ |
| Топіки | `modesp/v1/<tenant>/<id>/...` | `<thing_name>/state/...`, `$aws/things/<thing_name>/shadow/...` |
| HA discovery | Опціонально | Н/Д (HA не споживає AWS IoT напряму) |
| Shadow-документ | Н/Д | Синхронізується з SharedState (Stage 1.5) |

## Налаштування

Сертифікати завантажуються через WebUI Network → Cloud → AWS:

- Кореневий CA (PEM від Amazon, публічний).
- Сертифікат пристрою (видається IoT Core при реєстрації Thing).
- Приватний ключ пристрою (відповідає сертифікату).

Зберігаються у просторі імен NVS `aws` (ключ/значення, PEM у base64).
HTTP `POST /api/cert` з `type=aws_root_ca`, `aws_device_cert`,
`aws_device_key`.

Налаштування кінцевої точки у `sdkconfig.defaults`:

```ini
CONFIG_MODESP_AWS_ENDPOINT="a1b2c3d4e5f6-ats.iot.us-east-1.amazonaws.com"
CONFIG_MODESP_AWS_THING_NAME_PREFIX="modesp"  # actual = <prefix>_<device_id>
```

## Структура топіків

```
<thing_name>/state/<key_path>         publish (наприклад simple_thermo/temperature)
<thing_name>/cmd/<state_key>          subscribe (запис у SharedState)
$aws/things/<thing_name>/shadow/...   IoT Thing Shadow (Stage 1.5)
```

`<thing_name>` за замовчуванням дорівнює `<prefix>_<device_id_lowercase>`
(наприклад, `modesp_a1b2c3`). Налаштовується.

Синхронізація shadow-документа (заплановано, Stage 1.5):
- Записи у SharedState віддзеркалюються у `shadow/update` як reported state.
- Оновлення desired-state у shadow повертаються через
  `shadow/update/delta` як команди.
- Дозволяє віддалене керування уставками через AWS dashboard без
  власного топіка pub/sub.

## Ключі стану

| Ключ | Примітки |
|---|---|
| `aws.connected` | true, якщо сесія IoT Core активна. |
| `aws.thing_name` | Ім'я Thing цього пристрою (read-only, похідне). |
| `aws.endpoint` | Поточна кінцева точка IoT. |
| `aws.publish_count` | Сумарна кількість публікацій від завантаження. |

## Пам'ять і ресурси

| Ресурс | Вартість |
|---|---|
| Клієнт AWS SDK (esp-aws-iot або власний) | ~10 КБ купи |
| Сесія mTLS | ~12 КБ купи (сертифікат + ключ + стан сесії) |
| Рядки сертифікатів у NVS | ~4-8 КБ |

Загалом ~25 КБ. Бекенд AWS важчий за звичайний MQTT через mTLS і
багатослівніший Amazon SDK. Плануйте бюджет відповідно.

## Коли обирати AWS замість звичайного MQTT

**AWS:** керований брокер з вбудованим керуванням флотом пристроїв,
конвеєрами OTA, shadow-документами, рушієм правил для обробки на
стороні хмари, контролем доступу на основі IAM. Більша вартість у
RAM; mTLS обов'язковий.

**Звичайний MQTT:** локальний брокер (Mosquitto, EMQX, додаток
Mosquitto для HA), легкий, повний контроль. Опціональний TLS.

Для продакшен-розгортання, що інтегрується з AWS Lambda / IoT
Analytics / Greengrass — обирайте AWS. Для лише локального або
самохостингового Home Assistant — обирайте звичайний MQTT.

## Погляд з боку автора

Ідентичний до MQTT. Оголошення у маніфесті працюють так само:

```json
"mqtt": {
  "publish": ["my_module.value"],
  "subscribe": ["my_module.setpoint"]
}
```

Фреймворк обирає активний бекенд і підключає відповідно. Бізнес-модулі
не звертають уваги, який саме.

## Типові помилки

**Невідповідність сертифіката і ключа:** завантажений сертифікат і
ключ не утворюють пару. AWS IoT відхиляє з'єднання з
`MQTT_ERROR_TLS_HANDSHAKE_FAILED`. Перегенеруйте і повторно завантажте
обидва з консолі AWS IoT.

**Опечатка у кінцевій точці:** неправильно введений URL кінцевої
точки IoT. З'єднання тихо тайм-аутиться. Перевірте через
`aws iot describe-endpoint --endpoint-type iot:Data-ATS`.

**Невідповідність імені Thing:** політика прив'язана до не того
Thing → з'єднання приймається, але публікації відхиляються
(Forbidden). Перевірте у консолі IoT Core прив'язану політику і ARN
ресурсів.

**Бюджет пам'яті:** збірка з одночасно modesp_aws і рушієм сценаріїв
під WROOM-32 може бути тісною. Профілюйте через
`idf.py size-components`.

## Що далі

- **[components/modesp_mqtt.md](modesp_mqtt.md)** — альтернатива зі
  звичайним MQTT.
- **[02-module-author-guide/mqtt.md](../../02-module-author-guide/mqtt.md)** —
  оголошення у маніфесті стосуються обох бекендів.

## Джерела

- [`components/modesp_aws/`](../../../../components/modesp_aws/) — реалізація.
