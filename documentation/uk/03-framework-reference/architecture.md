# Архітектура

> 📖 **In English:** [documentation/en/03-framework-reference/architecture.md](../../en/03-framework-reference/architecture.md)

ModESP v4 — це шаруватий C++ фреймворк прошивки для пристроїв класу ESP32.
Ця сторінка документує архітектуру згори донизу: які компоненти існують,
як вони залежать один від одного, що виконується в якому завданні і як
конвеєр генерації, керований маніфестами, зв'язує усе разом під час
складання.

Якщо ви пишете модулі, ви зазвичай взаємодієте лише з тонким зрізом цієї
архітектури: BaseModule (modesp_core), ключі стану (SharedState) і,
можливо, драйверами (modesp_hal). Ця сторінка призначена для розуміння
підвалин, що лежать під цими API.

## Огляд шарів

```
┌──────────────────────────────────────────────────────────────────┐
│                        ВАШ ПРОДУКТ                               │
│   modules/<your_module>/   modules/<your_recipe>/                │
│   (manifest.json + C++)    (лише manifest.json)                  │
├──────────────────────────────────────────────────────────────────┤
│                     ФРЕЙМВОРК ModESP                             │
│                                                                  │
│   modules/equipment    modules/datalogger    modules/simple_thermo│
│        ↑                                                         │
│   ┌─────────────────────────────────────────────────────────┐    │
│   │ modesp_scenario  (engine, FSM, actions, continuous)     │    │
│   │ modesp_services  (Config, Persist, Error, Watchdog, Log)│    │
│   │ modesp_hal       (HAL, DriverManager, IDriver)          │    │
│   │ modesp_net       (WiFi, HTTP server, WebSocket)         │    │
│   │ modesp_mqtt      (MQTT client, TLS, HA discovery)       │    │
│   │ modesp_aws       (AWS IoT alternative cloud backend)    │    │
│   │ modesp_core      (App, ModuleManager, SharedState)      │    │
│   └─────────────────────────────────────────────────────────┘    │
├──────────────────────────────────────────────────────────────────┤
│              ESP-IDF v5.5 + FreeRTOS + LittleFS                  │
└──────────────────────────────────────────────────────────────────┘
```

Вищі шари залежать від нижчих; нижчі шари нічого не знають про вищі.
Прикладні модулі сидять угорі; ядро живе внизу.

## Карта залежностей компонентів

| Компонент | Залежить від | Надає |
|---|---|---|
| `modesp_core` | (ETL, FreeRTOS) | `App`, `ModuleManager`, `SharedState`, `BaseModule`, типи |
| `modesp_services` | core | Error, Watchdog, Config, Persist, Logger, SystemMonitor, nvs_helper |
| `modesp_hal` | core | HAL, DriverManager, інтерфейси IDriver |
| `modesp_net` | core, services, hal | WiFiService, HttpService, WsService |
| `modesp_mqtt` | core, services, net | MqttService, TLS, HA discovery |
| `modesp_aws` | core, services, net | AwsIotService (альтернатива до mqtt) |
| `modesp_scenario` | core, services | Engine, ActionRegistry, ContinuousRegistry, IStateBackend |
| `modules/equipment` | core, hal, services | Equipment Manager (сенсори → стан, стан → актуатори) |
| `modules/datalogger` | core, services | журналювання каналів і подій |
| `modules/simple_thermo` | core | еталонний бізнес-модуль |

Система складання забезпечує ці залежності через
`idf_component_register(REQUIRES ...)`.

## Об'єкт застосунку (`App`)

`modesp_core` надає одного синглтона рівня застосунку — `modesp::App`.
Створюється один раз у `main.cpp`:

```cpp
auto& app = modesp::App::instance();
app.init();             // construct SharedState, ModuleManager
// ... register modules ...
app.modules().init_all(app.state());     // calls on_init on registered modules
// ... later ...
app.modules().update_all(dt_ms);         // calls on_update on every tick
```

App володіє:
- `SharedState state_` — типізоване сховище ключ-значення
  ([shared-state.md](../02-module-author-guide/shared-state.md)).
- `ModuleManager modules_` — реєстр екземплярів BaseModule.

`app.state()` і `app.modules()` повертають посилання, що живуть протягом
усього часу роботи програми.

## ModuleManager — реєстрація та керування тактами

`ModuleManager` тримає масив фіксованої місткості з посилань `BaseModule*`
(без володіння — модулі статичні у main.cpp). Методи життєвого циклу
керують усіма зареєстрованими модулями:

```cpp
class ModuleManager {
public:
    bool register_module(BaseModule& m);     // adds to registry
    bool init_all(SharedState& state);       // calls on_init() on each CREATED module
    void update_all(uint32_t dt_ms);         // calls on_update() on each INITIALISED module
    void on_message(const etl::imessage& m); // dispatches to addressed module
    void stop_all();                         // calls on_stop()
    // ...
};
```

### Трифазна ініціалізація

`init_all` викликається ТРИ рази у main.cpp:

```cpp
// Phase 1 — register CRITICAL modules (error, watchdog, config, persist, monitor)
app.modules().register_module(error_service);
// ... more CRITICAL ...
app.modules().init_all(app.state());           // initialises CRITICAL only

// Phase 2 — register HIGH and NORMAL (wifi, hal, drivers, scenario, business modules)
app.modules().register_module(wifi_service);
// ... more HIGH/NORMAL ...
app.modules().init_all(app.state());           // initialises HIGH and NORMAL

// Phase 3 — register LOW (http, ws, datalogger)
app.modules().register_module(http_service);
app.modules().init_all(app.state());           // initialises LOW
```

`init_all` пропускає модулі, що вже у стані `INITIALISED`, тому кількаразові
виклики працюють як очікувалося. Модулі повертають `false` з `on_init`,
якщо не змогли ініціалізуватись — вони переходять у стан `FAILED` і не
отримують тактів.

Порядок у межах фази — це **порядок реєстрації**. `update_all` подає такти
модулям у тому самому порядку при кожному виклику.

### Цикл тактів 100 Гц

Після ініціалізації main.cpp виконує:

```cpp
while (true) {
    uint32_t now = millis();
    uint32_t dt_ms = now - last_tick_;
    last_tick_ = now;
    app.modules().update_all(dt_ms);
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(10));
}
```

Такт 10 мс = 100 Гц. `on_update(dt_ms)` кожного модуля виконується щотакту
у порядку реєстрації. Загальний час одного такту має лишатись < ~5 мс по
всіх модулях, щоб уникнути перезавантажень через сторожовий таймер і
тремтіння WS-розсилок.

## SharedState — хребет даних

Один процесний `SharedState` живе всередині App. Це типізоване сховище
ключ-значення під захистом м'ютекса (ETL `unordered_map<StateKey,
StateValue>` з обмеженою місткістю з `state_meta.h`). Модулі читають і
пишуть ключі; жодних прямих вказівників між модулями.

Три стилі доступу:

1. **Через помічники BaseModule** (найпоширеніше):
   ```cpp
   float t = read_float("equipment.air_temp", 0.0f);
   state_set("my_module.output", true);
   ```
2. **Через `app.state()` напряму** (для обробників HTTP, дій рецептів):
   ```cpp
   modesp::StateValue v;
   if (state.get("key", out)) { ... }
   ```
3. **Через `IStateBackend`** (рушій сценаріїв):
   ```cpp
   class SharedStateBackend : public IStateBackend { ... };
   ```

Повний довідник: [shared-state.md](../02-module-author-guide/shared-state.md).

## Згенеровані заголовки і конвеєр складання

Фреймворк **керується маніфестами**: `tools/generate_ui.py` виконується як
крок CMake перед складанням і генерує C++ заголовки з маніфестів:

| Згенерований файл | Вміст | Використовується |
|---|---|---|
| `state_meta.h` | constexpr-таблиця оголошених ключів стану + типи + макс. кількість | SharedState, PersistService, теми MQTT |
| `mqtt_topics.h` | константи рядків тем для кожного ключа стану | MqttService |
| `module_includes.h` | `#include "<module>.h"` на кожен запис у project.json | main.cpp |
| `module_instances.h` | оголошення `static <Module> name;` | main.cpp |
| `module_register.h` | виклики `manager.register_module(<name>)` | `modesp_register_modules(app)` у main.cpp |
| `modules.cmake` | список компонентів модулів для CMake REQUIRES | main/CMakeLists.txt |
| `display_screens.h` | конфігурації віджетів LCD | драйвери дисплея (якщо присутні) |
| `datalogger_channels.h` | відображення ідентифікаторів каналів | datalogger |
| `datalogger_events.h` | відображення ідентифікаторів подій | datalogger |
| `features_config.h` | прапорці можливостей часу складання | різне |

Артефакти, вбудовані в LittleFS (`data/`):

| Файл | Вміст |
|---|---|
| `data/ui.json` | об'єднана схема WebUI, що віддається за `/api/ui` |
| `data/board.json` | можливості обладнання вибраної плати |
| `data/bindings.json` | прив'язки драйверів вибраної плати |
| `data/scenarios/*.modr` | скомпільовані бінарники рецептів |
| `data/www/*` | попередньо складений Svelte SPA |
| `data/www/i18n/*.json` | пакети перекладів (UK/EN/DE/PL) |

`compile_scenario.py` виконується окремим кроком для модулів-рецептів,
створюючи бінарні файли `.modr`.

## Топологія завдань FreeRTOS

| Завдання | Пріоритет | Стек | Призначення |
|---|---|---|---|
| `main` | низький (еквівалент idle після завантаження) | 8 КБ | цикл оновлення 100 Гц. Більшість модулів тактує тут. |
| `app_main` | (початковий) | 4 КБ | налаштування при завантаженні, потім передача до `main`. |
| WiFi tasks | різні (ESP-IDF) | 4-8 КБ | стек Wi-Fi, lwIP, обробка мережевих пакетів. |
| httpd task | середній | 8 КБ | обробники HTTP-запитів виконуються тут, НЕ у головному завданні. |
| WebSocket worker | середній | 4 КБ | кадри WS |
| MQTT task | середній | 6 КБ | клієнт esp-mqtt |
| системні завдання ESP-IDF | різні | різні | IPC, таймери, ESP timer тощо. |

Модулі тактують у головному завданні — отже, з власної перспективи вони
однопотокові. Обробники HTTP і зворотні виклики MQTT виконуються в інших
завданнях, тому вони ПОВИННІ проходити через SharedState (захищений
м'ютексом) — а не торкатися стану модуля напряму.

## Збереження та відновлення стану

Два рівні:

1. **PersistService** (modesp_services) — підключає ключі стану з
   `persist: true` до NVS. Прозоро. Затримка 5 секунд. Відновлення
   відбувається до виклику `on_init` будь-якого модуля.
2. **NvsObserver рушія сценаріїв** (modesp_scenario) — відновлення на
   основі токенів для кожного екземпляра стану виконання сценарію.
   Магічне слово `SCTK`, обмежений темп записів, негайне збереження при
   змінах фази основної гілки.

Розділи NVS:
- `nvs` (24 КБ) — налаштування, облікові дані Wi-Fi, захищено ROM.
- `otadata` — селектор OTA.

Більші великі обʼєкти (LittleFS / `data/`) — лише для читання після
прошивки за замовчуванням; OTA оновлює увесь розділ атомарно.

## Потік OTA (верхній рівень)

Схема з двома образами та автоматичним відкатом:

1. Активна прошивка виконується у `ota_0`.
2. Нова прошивка завантажується через HTTP `/api/ota/upload` → потрапляє
   у `ota_1`.
3. Перезавантаження з `ota_1` як активним.
4. `app_main` перевіряє стан "pending-verify", дає новій прошивці
   60 секунд, щоб позначити себе стабільною.
5. Позначка стабільності = HTTP `/api/ota/confirm` або автоматично, якщо
   сторожовий таймер не спрацює.
6. В іншому випадку — відкат до `ota_0`.

Повний робочий процес розгортання у
[04-hardware/ota.md](../04-hardware/ota.md) *(планується)*.

## Мережа і зовнішній API

`modesp_net` надає:

- **WiFiService:** режим STA з резервним AP (не знайдено SSID → пристрій
  відкриває AP для введення облікових даних). Ім'я хоста mDNS
  `modesp-<deviceid>.local`.
- **HttpService:** esp_http_server з ~30 REST-точками доступу (стан,
  налаштування, конфігурація Wi-Fi, OTA, сценарії, інформація про модулі,
  плата, прив'язки тощо).
- **WsService:** WebSocket, що транслює зміни стану підключеним клієнтам
  кожні ~500 мс (або повний знімок при переповненні).

Опційні хмарні бекенди (взаємовиключні, вибір через Kconfig):
- `modesp_mqtt`: загальний MQTT, опційно TLS, з HA discovery.
- `modesp_aws`: AWS IoT Core з автентифікацією за сертифікатом.

Обидва реалізують той самий контракт публікації/підписки з погляду
автора модуля: оголошуєш у маніфесті, фреймворк підключає решту.

## Конвеєр рушія сценаріїв

```
modules/<recipe>/manifest.json
            │
            ▼ compile_scenario.py (build-time)
            │
data/scenarios/<recipe>.modr (binary)
            │
            ▼ engine.load_path (runtime)
            │
modesp::scenario::Engine ticks at 100 Hz
            │
            ├── ActionRegistry (lookups action handlers by hash)
            ├── ContinuousRegistry (factories для PID/hysteresis/ramp)
            ├── ResourceArbiter (ISA-88 §5.3 atomic claims)
            └── IEngineObserver (mirror writes, NVS persist)
            │
            ▼
SharedState mirror keys updated
WebUI / MQTT see changes
```

Рушій — це звичайний BaseModule, зареєстрований у Фазі 2. Див.
[scenario-engine/](scenario-engine/) для поглибленого розгляду.

## Модель периферії: роль = здатність (capability)

Модулі ніколи не називають драйвер. Роль оголошує **здатність
(capability)** — `temperature`, `relay_out`, `panel`… — а не джерело.
Термостат потребує «температуру» і не знає, хто її дає (ds18b20 / NTC /
BLE-канал / майбутній LoRa) — джерело замінне (R0.1, R3.1).

`capability` — **концепт часу складання**. Він живе в маніфестах
(`tools/capabilities.json` — SSOT словника) і в `generate_ui.py`; на
пристрої він **компілюється геть**. Прив'язка резолвиться за **роллю**:
генератор під час білду перевіряє, що роль і канал драйвера мають рівну
`capability` й узгоджений напрям (in/out), а C++ читає лише
`equipment.<role>` та резолвить джерело через `find_sensor(role)` /
`find_actuator(role)`. Тому HAL ніколи не вчить слово «capability» —
`Binding{hardware_id, role, driver_type, module_name, address}`
(`hal_types.h`) уже транспорт-агностичний.

Ланцюг прив'язки (єдиний маршрут периферії):

```
manifest модуля  →  роль {capability}          (що потрібно)
board.json       →  hardware / remote-пристрій  (що є на платі)
bindings.json    →  Binding{hardware,driver,role,module}  (build-time матч за capability)
       │
       ▼ generate_ui.py (cross_validate — R8.3)
on-device: find_sensor/find_actuator(role) → ISensorDriver/IActuatorDriver
```

### Транспорт-генеричні віддалені пристрої

Off-board сенсор/актуатор, доступний через транспорт, описується як
`RemoteDeviceConfig{id, transport, identity, name}` (`hal_types.h`,
реєстр `BoardConfig::remote_devices`, кеп `MAX_REMOTE_DEVICES = 16`).
`transport` — окреме поле (`ble` сьогодні; `lora`/`mqtt`/`espnow` далі),
авто-виводиться з `hardware_type`. `identity` — **непрозорий блоб**
(BLE MAC, майбутні LoRa devaddr / MQTT topic), що живе на рядку пристрою
(board.json factory-seed або runtime `/data/devices.json`), **ніколи не
на біндінгу ролі** (R0.3, R4.1). Резолв id→identity через
`find_remote_device(id)`. Тому роль лишається транспорт-агностичною:
той самий термостат байдужий, чи «температура» приходить дротом чи по
BLE. Новий транспорт = новий компонент + драйвер-міст (як `modesp_ble`);
HAL/генератор/webui не чіпаються, а HAL не залежить від жодного
транспорту (R4.2, інваріант `core←hal←…←ble`). Повний звід —
[rules.md](rules.md) (R0–R4).

## Що ви зазвичай не торкаєтесь напряму

- `app_main`, завдання ESP-IDF — чистий каркас завантаження у main.cpp.
- `modesp_core::SharedState` напряму — використовуйте помічники
  BaseModule.
- Обробники HTTP / WebSocket — генеруйте UI через маніфести.
- Клієнт MQTT — оголошуйте у маніфесті.
- Екземпляри драйверів — bindings.json підключає їх за роллю
  (build-time матч за capability); ви пишете модулі, що читають
  `equipment.<role>`, і ніколи не називаєте драйвер.

## Що ви часто налаштовуєте

- Маніфести модулів — оголошення ключів стану, UI, MQTT.
- C++ класи модулів — бізнес-логіка у `on_update`.
- Розділи сценаріїв у рецептах — процеси, керовані фазами.
- `bindings.json` для кожного розгортання — погоджуйте з обладнанням,
  поки не з'явиться повноцінний варіант плати.
- `project.json` — які модулі потрапляють у цю прошивку.

## Що далі

- **[components/modesp_core.md](components/modesp_core.md)** — детальний
  довідник API ядра: SharedState, BaseModule, ModuleManager.
- **[components/modesp_services.md](components/modesp_services.md)**
  *(планується)* — внутрішня будова служб.
- **[components/modesp_hal.md](components/modesp_hal.md)** *(планується)*
  — HAL і DriverManager.
- **[scenario-engine/](scenario-engine/)** — поглиблений розгляд рушія
  сценаріїв.
- **[02-module-author-guide/overview.md](../02-module-author-guide/overview.md)**
  — повернення до перспективи автора модулів.

## Джерела

- [`components/modesp_core/`](../../../components/modesp_core/) — типи
  ядра і App.
- [`main/main.cpp`](../../../main/main.cpp) — послідовність завантаження,
  фази ініціалізації, цикл тактів.
- [`project.json`](../../../project.json) — маніфест модулів.
- [`tools/generate_ui.py`](../../../tools/generate_ui.py) — генератор,
  що працює під час складання.
