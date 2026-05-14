# `modesp_net` — Wi-Fi, HTTP, WebSocket

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_net.md](../../../en/03-framework-reference/components/modesp_net.md)

`modesp_net` надає три модулі, що відповідають за мережеву поверхню:
WiFiService (STA + резервний AP), HttpService (~30 REST-точок доступу +
видача статичних файлів) і WsService (трансляція стану через WebSocket у
реальному часі). Разом вони відкривають увесь фреймворк для зовнішніх
клієнтів — WebUI, помічників MQTT, інструментів CLI.

Бізнес-модулі не взаємодіють з цим шаром напряму; натомість їхні
маніфести оголошують UI / ключі стану, а цей компонент їх відображає. Ця
сторінка описує, що виставлено назовні та як працює конвеєр загалом.

REQUIRES: `modesp_core`, `modesp_services`, `modesp_hal`,
`esp_wifi esp_http_server esp_netif esp_event nvs_flash`.

## Розташування компонента

```
components/modesp_net/include/modesp/net/
├── wifi_service.h
├── http_service.h
└── ws_service.h
```

## `WiFiService` — керування з'єднанням

```cpp
class WiFiService : public modesp::BaseModule {
public:
    WiFiService();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

    // Diagnostic
    bool is_connected() const;
    const char* current_ssid() const;
    int8_t current_rssi() const;
    bool is_ap_mode() const;
};
```

### Логіка з'єднання

1. **Завантаження:** читає SSID/пароль з NVS (`wifi.ssid`,
   `wifi.password`).
2. **Спроба STA:** намагається приєднатись до налаштованого SSID. У разі
   успіху → підключено, отримує DHCP, реєструє ім'я хоста mDNS.
3. **Резервний AP:** якщо STA не вдається протягом ~30 с АБО немає
   облікових даних у NVS — відкриває AP `ModESP-<MAC>` з відомим паролем
   (за замовчуванням `12345678`). Стиль captive-portal — користувач
   приєднується, відкриває браузер, переходить на `http://192.168.4.1/`,
   вводить облікові дані.
4. **Відновлення STA:** події відключення STA запускають повторне
   з'єднання з експоненційною затримкою. Після ~5 хвилин невдач можна
   опційно перейти у режим AP (прапорець Kconfig).

### Ключі стану

| Ключ | Тип | Примітки |
|---|---|---|
| `wifi.connected` | bool | true, якщо STA приєдналось; false, якщо лише AP або відключено. |
| `wifi.ssid` | string | Поточний SSID (зберігається). |
| `wifi.password` | string | (зберігається, mqtt_subscribe вимкнено — ніколи не виставляється). |
| `wifi.rssi` | int | Сила сигналу в dBm. |
| `wifi.ip` | string | Поточна IP-адреса. |
| `wifi.ap_mode` | bool | true, якщо у режимі резервного AP. |
| `wifi.last_connect_s` | int | Секунд з моменту останнього успішного з'єднання. |

### HTTP-точки доступу (обробляються у HttpService)

| Метод + шлях | Призначення |
|---|---|
| `GET /api/wifi/scan` | Сканує ближні SSID; повертає масив `{ssid, rssi, secured}`. |
| `POST /api/wifi` | Зберігає нові облікові дані і перепідключається. |
| `GET /api/wifi/ap` | Поточна конфігурація резервного AP. |
| `POST /api/wifi/ap` | Оновлює резервний AP (SSID, пароль). |

### mDNS

Після успішного приєднання у STA ім'я хоста `modesp-<deviceid>.local`
стає доступним для розв'язання у локальній мережі. WebUI доступний через
`http://modesp-a1b2c3.local/` замість полювання за IP.

## `HttpService` — REST API і видача статичних файлів

```cpp
class HttpService : public modesp::BaseModule {
public:
    HttpService();

    void set_state(SharedState* state);
    void set_config(ConfigService*);
    void set_modules(ModuleManager*);
    // ... more setters для dependencies ...
    void set_scenario_engine(modesp::scenario::Engine*);

    httpd_handle_t server() const;
};
```

Впровадження залежностей відбувається у main.cpp (HTTP потребує доступу
майже до кожної іншої служби). Ініціалізується після того, як усі
залежності у стані INITIALISED — у Фазі 3 (пріоритет LOW).

### REST-точки доступу

Усього ~30 точок доступу. За категоріями:

**Стан і діагностика:**
- `GET /api/state` — повний знімок SharedState у форматі JSON.
- `GET /api/modules` — список модулів зі станами.
- `GET /api/board` — вміст board.json.
- `GET /api/bindings` — вміст bindings.json.
- `POST /api/bindings` — зберегти нові прив'язки (потребує перезавантаження).
- `GET /api/ui` — згенерована схема UI.
- `GET /api/log` — буфер останніх рядків журналу.
- `GET /api/log/summary` — коротке зведення.

**Налаштування (запис у стан):**
- `POST /api/settings` — одноразові записи (1-8 ключів за раз).

**Мережа:**
- `GET /api/wifi/scan`, `POST /api/wifi`, `GET/POST /api/wifi/ap`.
- `GET /api/mqtt`, `POST /api/mqtt` — конфігурація брокера.
- `GET /api/time`, `POST /api/time` — ручний час і часовий пояс.

**Обладнання:**
- `GET /api/onewire/scan` — виявлення сенсорів OneWire на прив'язаних
  шинах.
- `POST /api/drivers/<type>/scan` — виявлення, специфічне для драйвера.

**OTA:**
- `GET /api/ota` — поточний розділ і версія.
- `POST /api/ota` — почати завантаження прошивки (multipart).
- `POST /api/ota/confirm` — позначити очікувану прошивку як стабільну.
- `POST /api/ota/rollback` — завантажитись з попереднього розділу.

**Резервне копіювання та відновлення:**
- `GET /api/backup` — дамп збереженого стану і конфігурації у форматі
  JSON.
- `POST /api/restore` — застосувати резервну копію.

**Система:**
- `POST /api/restart` — перезавантажити пристрій.
- `POST /api/factory-reset` — стерти NVS і перезавантажитись.
- `GET /api/auth`, `POST /api/auth` — зміна облікових даних.

**Сценарії:**
- `GET /api/scenario/list`, `GET /api/scenario/info?handle=N`
- `POST /api/scenario/load`, `start`, `pause`, `resume`, `abort`,
  `unload`.

### Автентифікація

HTTP Basic Auth. Облікові дані за замовчуванням: `admin` / `modesp`
(зберігаються у просторі імен NVS `auth`). Зміна через `POST /api/auth`
або у WebUI Система → Автентифікація.

Кожна API-точка спочатку викликає `check_auth(req)`; відсутні/неправильні
облікові дані → 401 Unauthorized.

Автентифікацію можна вимкнути через прапорець NVS (`auth.enabled =
false`), що корисно у розробці. У продукції — лишайте увімкненою.

### Видача статичних файлів

Після всіх API-точок обробник за шаблоном видає файли з `/data/www/`:

- `/` → `/data/www/index.html` (точка входу WebUI).
- `/bundle.js.gz` → стиснутий gzip пакунок Svelte.
- `/bundle.css.gz` → стиснуті стилі.
- `/i18n/<lang>.json` → пакети перекладів.

Обробник за шаблоном слід реєструвати ОСТАННІМ (після API-точок). Інакше
шаблон `/*` затіняє конкретні маршрути.

## `WsService` — трансляція стану через WebSocket

```cpp
class WsService : public modesp::BaseModule {
public:
    WsService();

    void set_state(SharedState* state);
    void set_http_server(httpd_handle_t server);
};
```

Точка доступу WebSocket: `/api/ws`. Фронтенд WebUI підключається сюди
при завантаженні І підтримує з'єднання для оновлень стану в реальному
часі.

### Логіка трансляції

Кожні ~500 мс WsService:

1. Викликає `state.for_each_changed_and_clear()`.
2. Якщо змін ≤ 32 ключів — надсилає корисне навантаження-дельту:
   ```json
   {"type": "delta", "values": {"key1": v1, "key2": v2, ...}}
   ```
3. Якщо переповнення (`needs_full_broadcast()`) — надсилає повний знімок:
   ```json
   {"type": "snapshot", "values": {<all keys>}}
   ```
4. Нові клієнти, що підключаються посеред сесії, отримують знімок при
   приєднанні.

### Вхідні повідомлення

```json
{"type": "set", "key": "thermo.setpoint", "value": 23.5}
```

Еквівалентно `POST /api/settings {"thermo.setpoint": 23.5}`, але без
повного раунду HTTP. Використовується інтерактивними віджетами
(повзунки) задля малої затримки.

### Місткість

Максимум ~4 одночасних WebSocket-клієнти (Kconfig). Кожен тримає ~2 КБ
у буфері httpd на клієнта. Перевищує бюджет на завантажених пристроях.

## Підключення у main.cpp

```cpp
static modesp::WiFiService wifi_service;
static modesp::HttpService http_service;
static modesp::WsService   ws_service;

// Phase 2: register WiFi
app.modules().register_module(wifi_service);

// Phase 3: register HTTP + WS із dependency injection
http_service.set_state(&app.state());
http_service.set_config(&config_service);
http_service.set_modules(&app.modules());
http_service.set_wifi(&wifi_service);
http_service.set_persist(&persist_service);
http_service.set_hal(&hal);
http_service.set_datalogger(&datalogger);
http_service.set_scenario_engine(&scenario_engine);

ws_service.set_state(&app.state());

app.modules().register_module(http_service);
app.modules().register_module(ws_service);
app.modules().init_all(app.state());

// Step 9: connect WS handler to HTTP server, register wildcard last
if (http_service.server()) {
    ws_service.set_http_server(http_service.server());
    http_service.register_static_handler();   // MUST be last
}
```

Шаблон з сетерами багатослівний, але явний — кожна залежність видима у
main.cpp. Уникає прихованого глобального стану.

## Ключі стану (категорія мережі)

| Ключ | Примітки |
|---|---|
| `wifi.connected` | STA підключено. |
| `wifi.ssid` / `wifi.ip` / `wifi.rssi` | Деталі з'єднання. |
| `wifi.ap_mode` | Резервний AP активний. |
| `http.requests_count` | Всього обслужено HTTP-запитів (для налагодження). |
| `http.ws_clients` | Зараз підключено WS-клієнтів. |

## Продуктивність і пам'ять

| Служба | RAM | Завдання |
|---|---|---|
| WiFiService | ~3 КБ | виконується у main + завданнях Wi-Fi ESP-IDF. |
| HttpService | ~8 КБ (буфер httpd) | httpd має власне завдання. |
| WsService | ~4 КБ (буфери кадрів) | кадри WS у воркері httpd. |

Усього ~15 КБ виділено для мережевої поверхні. Кожен WS-клієнт додає
~2 КБ.

Максимум обробників URI: 64 (Kconfig `CONFIG_HTTPD_MAX_URI_HANDLERS`).
Підняли з типових 48 заради співіснування API сценаріїв і WS.

## Типові помилки

**WS не підключається:** спочатку перевірте `wifi.connected = true`.
Якщо Wi-Fi у порядку, але WS не вдається — шукайте у журналах відмову
`httpd_register_uri_handler` — імовірно, перевищено максимум обробників
URI.

**Статичний обробник затіняє API:** `register_static_handler()` ПОВИНЕН
викликатись ОСТАННІМ. Інакше шаблон `/*` збігається з усім, зокрема й
`/api/*`.

**401 Unauthorized у браузері:** очистьте кеш автентифікації браузера,
введіть облікові дані повторно. За замовчуванням `admin/modesp`;
скидання через factory-reset або пряме редагування NVS.

**`/api/state` повільний:** створення знімка займає ~30 мс за 96 записів.
Не опитуйте цю точку часто — використовуйте WS для реального часу, REST
для одноразових запитів.

**Шторм перепідключень Wi-Fi у журналах:** увімкнений резервний AP +
зниклий SSID STA призводять до постійного перемикання режимів. Вимкніть
резервний AP для стабільних розгортань АБО забезпечте детермінований
пароль AP.

## Що далі

- **[components/modesp_mqtt.md](modesp_mqtt.md)** — шар публікації/
  підписки MQTT поверх мережі.
- **[components/modesp_aws.md](modesp_aws.md)** — альтернативний хмарний
  бекенд AWS IoT.
- **[02-module-author-guide/ui-widgets.md](../../02-module-author-guide/ui-widgets.md)**
  — що `/api/ui` виставляє і як відображаються віджети.
- **[02-module-author-guide/debugging.md](../../02-module-author-guide/debugging.md)**
  — використання `/api/state`, `/api/modules`, `/api/log` для
  налагодження.

## Джерела

- [`components/modesp_net/include/modesp/net/`](../../../../components/modesp_net/include/modesp/net/)
  — публічні заголовки.
- [`components/modesp_net/src/`](../../../../components/modesp_net/src/)
  — реалізації. `http_service.cpp` — найбільший файл (~2000 рядків
  коду), що містить усі REST-обробники.
