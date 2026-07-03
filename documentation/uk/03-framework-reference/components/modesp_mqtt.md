# `modesp_mqtt` — MQTT-клієнт з TLS і HA discovery

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_mqtt.md](../../../en/03-framework-reference/components/modesp_mqtt.md)

`modesp_mqtt` — типовий хмарний бекенд. Він обгортає клієнт `esp-mqtt`
з ESP-IDF І прив'язує його до SharedState через оголошені у маніфестах
списки publish / subscribe. Опціональний TLS, опціональний автопошук
Home Assistant, збережені аварійні сповіщення та Last-Will-Testament —
все налаштовується через маніфести, без явного MQTT-коду з боку
бізнес-модулів.

ЗАЛЕЖНОСТІ: `modesp_core`, `modesp_services`, `modesp_net`, `mqtt`,
опціонально `mbedtls` для TLS.

Погляд з боку автора модуля: [02-module-author-guide/mqtt.md](../../02-module-author-guide/mqtt.md).
Та сторінка описує API маніфесту та шаблони використання. Ця сторінка
документує внутрішню реалізацію.

## Структура компонента

```
components/modesp_mqtt/include/modesp/net/
├── mqtt_service.h
└── ota_handler.h        (shared із services)
```

Вибір на етапі компіляції: `CONFIG_MODESP_CLOUD_MQTT` (типово) обирає
цей компонент; `CONFIG_MODESP_CLOUD_AWS` обирає modesp_aws натомість.
Тільки один з них збирається у конкретну прошивку.

## `MqttService` — клас модуля

```cpp
class MqttService : public modesp::BaseModule {
public:
    MqttService();

    void set_state(SharedState* state);
    void set_http_server(httpd_handle_t server);

    bool is_connected() const;
};
```

Пріоритет ініціалізації: HIGH (1) — запускається у Phase 2, після Wi-Fi.

### Часові константи

```cpp
static constexpr uint32_t PUBLISH_INTERVAL_MS = 1000;          // 1 с
static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 30000;       // 30 с
```

Налаштовуються через Kconfig за потреби. Типові значення обережні —
балакучі розгортання можуть захотіти швидших інтервалів публікації.

### Формат топіка

```
modesp/v1/<tenant>/<device_id>/<state_key_path>
modesp/v1/<tenant>/<device_id>/cmd/<state_key>
modesp/v1/<tenant>/<device_id>/status
```

- `<tenant>` за замовчуванням дорівнює `default` (Kconfig `CONFIG_MODESP_MQTT_TENANT`).
- `<device_id>` походить з останніх 6 символів MAC у нижньому регістрі (напр., `a1b2c3`).
- `<state_key_path>` — крапки замінені на слеші
  (`simple_thermo.temperature` → `simple_thermo/temperature`).
- Топік `cmd/` зберігає крапку у ключі (застарілий формат; буде
  нормалізовано на Stage 1.5).

### Життєвий цикл з'єднання

1. **on_init:** читає конфігурацію брокера з NVS (простір імен `mqtt`) і
   зі значень за замовчуванням на етапі компіляції. Ще не з'єднується —
   чекає на Wi-Fi.
2. **on_update:** опитує `wifi.connected`. Щойно true, запускає клієнт
   esp-mqtt із зареєстрованим LWT (`<base>/status` = `"offline"`).
3. **MQTT_EVENT_CONNECTED:** публікує `"online"` у топік статусу (QoS 1,
   збережене), підписується на `<base>/cmd/+`, публікує HA discovery
   (якщо ввімкнено).
4. **MQTT_EVENT_DATA:** розбирає топік, перевіряє ключ за списком
   дозволів, записує до SharedState.
5. **MQTT_EVENT_DISCONNECTED:** робить паузу та повторює спробу. Спроби
   повторного з'єднання експоненційні, до ~60 с.

### Цикл публікації

Кожні `PUBLISH_INTERVAL_MS`:

1. Ітерується об'єднаний список дозволів на публікацію (з `mqtt_topics.h`).
2. Для кожного ключа перевіряється набір змінених ключів SharedState.
3. Змінені ключі публікуються з QoS 0 (дельта-семантика).

Heartbeat працює на власному таймері. Ключі з секції `mqtt.alarm`
маніфеста публікуються з QoS 1 + retain (генерований
`gen::MQTT_PUBLISH_ALARM[]`); невдала постановка аларму в чергу
повторюється наступним тиком (кеш не оновлюється). Решта ключів —
QoS 0 без retain.

### HA discovery

При кожному з'єднанні MqttService публікує discovery для кожної entity,
задекларованої в маніфестах модулів (`mqtt.ha` → генерований
`gen::HA_ENTITIES[]`):

```
homeassistant/sensor/<topic_root>_a1b2c3/simple_thermo_setpoint/config
```

Корисне навантаження містить одиницю (типово зі state-ключа), клас
пристрою, state class, топіки стану/доступності та спільний блок
`device`. Корінь топіків/ідентифікаторів — `system.mqtt_topic_root`
у project.json.

HA автоматично створює сутності. Ручне налаштування не потрібне.
УВАГА: якщо entity зникає з маніфеста, її старий retained discovery-конфіг
лишається на брокері — очистіть раз: `mosquitto_pub -r -n -t <топік>`.

### TLS і відмінність від AWS

Звичайний MQTT може працювати через звичайний TCP (порт 1883) або TLS
(порт 8883) з налаштовуваним сертифікатом CA. AWS IoT — окремий
компонент (modesp_aws) з mTLS і специфічними для Amazon префіксами
топіків — той самий контракт publish/subscribe з погляду автора модуля.

## Інтеграція з HTTP API

| Метод + шлях | Призначення |
|---|---|
| `GET /api/mqtt` | Повертає поточну конфігурацію брокера (без пароля). |
| `POST /api/mqtt` | Оновлює хост/порт/облікові дані брокера. Перепідключається. |

Завантаження сертифіката (TLS): `POST /api/cert` з корисним
навантаженням PEM, type=`mqtt_ca`.

## Ключі стану

| Ключ | Тип | Примітки |
|---|---|---|
| `mqtt.connected` | bool | Статус з'єднання з брокером. |
| `mqtt.broker_host` | string | Поточне ім'я хоста брокера. |
| `mqtt.broker_port` | int | Поточний порт. |
| `mqtt.publish_count` | int | Сумарна кількість публікацій від завантаження. |
| `mqtt.last_disconnect_s` | int | Секунд від останньої події відключення. |
| `mqtt.subscribe_topic` | string | Поточний шаблон підписки (`<base>/cmd/+`). |

## Пам'ять і ресурси

| Ресурс | Вартість |
|---|---|
| Клієнт esp-mqtt | ~6 КБ купи + власний task |
| Префікс топіка і списки дозволів | ~1 КБ у `mqtt_topics.h` |
| Сесія TLS (якщо ввімкнено) | ~8 КБ купи |

Загалом: ~7 КБ без TLS, ~15 КБ з TLS. Будьте уважні на платах з
обмеженою RAM.

## Типові помилки

**Не вдається розв'язати ім'я хоста брокера:** переконайтеся, що
працює mDNS або локальний DNS. У продакшені прописуйте IP-адреси
жорстко.

**Ключ підписки не у списку дозволів:** відсутній прапорець
`mqtt_subscribe: true`. Згенерований `mqtt_topics.h` не включить ключ
у список дозволів; MQTT ігноруватиме вхідні команди.

**Сутності HA не з'являються:** перевірте, що ввімкнено
`CONFIG_MODESP_MQTT_HA_DISCOVERY` І брокер підписаний на
`homeassistant/#`. HA треба перезапустити, щоб підхопити нові топіки
discovery, якщо вони з'явилися раніше за HA.

**Помилки рукостискання TLS:** перевірте дійсність сертифіката та
системний годинник (синхронізацію SNTP). Прострочений або
дореепохальний час провалює автентифікацію TLS.

## Що далі

- **[02-module-author-guide/mqtt.md](../../02-module-author-guide/mqtt.md)** —
  довідник з боку маніфесту.
- **[components/modesp_aws.md](modesp_aws.md)** — альтернатива AWS IoT.
- **[components/modesp_net.md](modesp_net.md)** — мережна передумова.

## Джерела

- [`components/modesp_mqtt/`](../../../../components/modesp_mqtt/) — реалізація.
- `mqtt_service.cpp` ~1000 рядків коду, включно з HA discovery і HTTP-обробниками.
