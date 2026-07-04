# ModESP v4 Framework — Авторитетна архітектура та ієрархія

**Мета документа.** Це референс структури ModESP Framework — manifest-driven ESP32 firmware framework для промислових автоматів (ESP-IDF v5.5, C++17 з ETL). Він існує заради ОДНОГО: щоб інженер або AI-агент розумів, як влаштована система, і НЕ ЗЛАМАВ те, що працює. Кожне структурне твердження тут звірене з реальним кодом/маніфестами (шляхи в дужках). Правило номер один: UI/state/MQTT/реєстрація модулів/драйверів **генеруються** з маніфестів — щоб щось змінити, редагуй **маніфест**, а не згенерований вихід.

---

## 1. LAYERS — три шари

| Шар | Що це | Де живе |
|-----|-------|---------|
| **Product** | Бізнес-логіка + карта заліза конкретного виробу | `modules/`, `boards/` |
| **Framework** | Універсальні C++ компоненти + генератор | `components/`, `tools/` |
| **Platform** | ESP-IDF v5.5, FreeRTOS, NimBLE (bt), LittleFS | `managed_components/`, ESP-IDF |

Напрям залежностей строго `Product → Framework → Platform`. Framework НІКОЛИ не залежить від product-модуля.

```
D:/ModESP_v4_Framework/
├── project.json          ← список активних модулів + system-блок (single entry point)
├── CMakeLists.txt        ← board-resolution + запуск генератора + COMPONENTS
├── components/           ← 10 framework-компонентів (C++)
│   ├── modesp_core/         base_module, shared_state, module_manager, app  (КОРІНЬ графа)
│   ├── modesp_hal/          HAL, driver_manager, driver_registry, char_grid
│   ├── modesp_services/     config, nvs, persist(LittleFS), ota, watchdog, logger
│   ├── modesp_net/          wifi, http_service, ws_service        (опційний: CONFIG_MODESP_NET_ENABLE)
│   ├── modesp_ble/          ble_service — єдиний власник NimBLE-хоста (опційний: BLE_ENABLE)
│   ├── modesp_equipment/    EquipmentBase (generic driver binding)
│   ├── modesp_scenario/     track-based FSM / рецепти (опційний: SCENARIO_ENABLE)
│   ├── modesp_mqtt/         cloud backend MQTT  ┐ взаємовиключні
│   ├── modesp_aws/          cloud backend AWS   ┘ (Kconfig choice)
│   └── jsmn/                vendored JSON parser
├── modules/              ← PRODUCT: abs_test, datalogger, display, equipment,
│                            panel, player, presence, simple_thermo
├── drivers/              ← опційні драйвери заліза (sensor/actuator/display/audio/io)
│                            amt630a, at7456e, ds18b20, ntc, relay, digital_input,
│                            pcf8574_relay, pcf8574_input, max98357a, ld2410b,
│                            ble_xiaomi_th, ble_nrf_tilt, ble_led_panel
├── boards/               ← per-board залізо: dev, kc868a6, stand_s3
│                            (board.json + bindings.json [+ optional sdkconfig.board])
├── tools/                ← generate_ui.py (генератор), compile_scenario.py,
│                            gen_osd_font.py, cmake/modesp_driver.cmake, schemas/
├── main/                 ← app entry component (main.cpp, Kconfig.boards)
├── generated/            ← АВТОГЕНЕРОВАНІ .h + .cmake  (DO NOT EDIT)
├── data/                 ← образ LittleFS: ui.json, board.json, bindings.json,
│                            www/, i18n/, scenarios/, audio/  (board.json/bindings.json — КОПІЇ)
├── webui/                ← Svelte UI source (СТАТИКА, НЕ генерується; вантажить ui.json у runtime)
├── documentation/        ← uk/ (+ дзеркало en/)
└── managed_components/   ← ESP-IDF registry deps: etlcpp, littlefs, mqtt, mdns, libhelix-mp3
```

Компоненти виявлені: `ls components/` = jsmn, modesp_aws, modesp_ble, modesp_core, modesp_equipment, modesp_hal, modesp_mqtt, modesp_net, modesp_scenario, modesp_services. Модулі: abs_test, datalogger, display, equipment, panel, player, presence, simple_thermo.

---

## 2. THE CORE MODEL — Module ↔ Role ↔ Device ↔ Binding

Це центральне відношення фреймворку. Зрозумій його, і решта стане на місце.

### 2.0 Єдиний маршрут периферії (принцип №1)

**Датчик, актуатор, дисплей, панель — усе це ПЕРИФЕРІЯ.** Незалежно від типу
периферії й транспорту (GPIO / I2C / OneWire / BLE-observer / BLE-connect) маршрут
**ОДНАКОВИЙ**. Ніяких спецкейсів «панель окремо», «connect окремо»:

```
Периферія (драйвер) ─► стає доступною ─► [UI-БІНДІНГ] на роль модуля ─► модуль користується через роль
   будь-який тип        board.json /        сторінка «Прив'язки»          не знає про залізо
                        Devices page         (runtime, через веб)
```

- **Біндінг відбувається через UI**, у runtime — користувач на сторінці «Прив'язки»
  зіставляє роль із конкретною периферією. Це той самий механізм для дротового датчика,
  BLE-обсервера, connect-дисплея й реле.
- **Роль — іменований слот-потреба.** Модуль може оголосити **скільки завгодно** ролей,
  зокрема **кілька одного типу**. Приклад: модуль, якому потрібні два дисплеї, оголошує
  `display_main` + `display_aux` (обидва `type: display`); через UI кожну роль біндять на
  свій фізичний дисплей. Той самий маршрут, що й «температурна роль → датчик».
- Тому НЕ можна хардкодити конкретну периферію в модуль/ядро: модуль оголошує *потреби*
  (ролі), периферія існує *окремо*, а UI-біндінг їх зшиває — для всіх типів однаково.


```
   MODULE                         DEVICE
 (потребує роль)               (конкретне залізо)
  requires[]                    board.json / devices.json
      │                                │
      │  role: "display_main"          │  id: "disp_0"
      │  type: display                 │  chip: amt630a
      │  driver: [amt630a]             │  bus: i2c_0
      └──────────┐          ┌──────────┘
                 ▼          ▼
             BINDING  (bindings.json)
   {hardware:"disp_0", driver:"amt630a",
    role:"display_main", module:"display"}
              з'єднує роль модуля з пристроєм
```

**Визначення:**

- **MODULE** — одиниця бізнес-логіки (`modules/<name>/`, клас `<Name>Module : public BaseModule`). Декларує потрібні йому ролі у top-level `requires[]`.
- **ROLE** — іменований слот, який модуль **потребує**: `{role, type: sensor|actuator|display, driver: [дозволені драйвери], label, optional?}`. Це капабіліті-контракт, не залізо.
- **DEVICE** — конкретне залізо з `id`. Два джерела: (1) ДРОТОВЕ у `board.json` під типізованими секціями (`i2c_displays`, `i2s_buses`, `onewire_buses`, `gpio_outputs`…); (2) RUNTIME BLE у `/data/devices.json`, підписане через веб-сторінку «Пристрої».
- **BINDING** — рядок `bindings.json`, що ОБ'ЄДНУЄ роль модуля з пристроєм: `{hardware=<device id>, driver, role, module, address?, settings?}`. On-device структура — `Binding{hardware_id, role, driver_type, module_name, address, settings[]}` (`hal_types.h`).

**Правило власності (прямо):** роль оголошує САМЕ ТОЙ модуль, який її СПОЖИВАЄ, у своєму `requires`. Пристрої конфігуруються/підписуються ОКРЕМО (board.json / Devices page). Binding їх зшиває. Ідентичність провайдера ролі детектується без хардкоду: `role_providers()` збирає кожен модуль з top-level `requires` (`tools/generate_ui.py:135-142` — «жодних хардкодів імені equipment»). БУДЬ-ЯКИЙ модуль може бути провайдером ролей.

**`binding.module` — це маршрутизація, не документація.** DriverManager штампує `entry.module = b.module_name` на кожному драйвері (`driver_manager.cpp`), а споживчий модуль фільтрує біндінги за ним: `display_module.cpp:75` — `if (!(b.module_name == "display")) continue;`.

**Приклад (перевірено на stand_s3):**
- Модуль `display` володіє роллю `display_main` (`display/manifest.json:8-16`) → binding `{hardware:disp_0, driver:amt630a, role:display_main, module:display}` (`boards/stand_s3/bindings.json`).
- Модуль `player` володіє `audio_main` → binding `{hardware:i2s_0, driver:max98357a, role:audio_main, module:player}`.

**Приклад багатого периферійного драйвера на тому самому маршруті — `panel`.** Роль `panel`
оголошує модуль-**споживач** `panel` (`panel/manifest.json` → `requires`), не equipment. Драйвер
`ble_led_panel` — звичайний актуатор: його створює DriverManager із біндінга й індексує за роллю
(як реле). Модуль `panel` резолвить той САМИЙ об'єкт за іменем ролі — `find_actuator(role)->as_panel()`
(`panel_module.cpp::on_bind`, фільтр `binding.module == "panel"`) — і подає контент крізь його
`IPanelPort`. `as_panel()` — capability-cast без RTTI на `IActuatorDriver` (той самий ідіом, що
`IDisplayPort::as_power()`). Жодного глобального singleton: маршрут ідентичний «датчик → роль → модуль».

---

## 3. DEVICE LIFECYCLE — дротове (build-time) vs runtime-BLE

### Дротове залізо — board.json, build-time
I2C-шина/дисплей, OneWire, GPIO-реле/входи, ADC, UART, I2S, PCF8574-експандери оголошуються у `board.json` і фізично ініціалізуються `HAL::init` на завантаженні. У runtime — тільки GET (немає write-ендпоінта на board.json). Щоб змінити дротове залізо — редагуй board.json і переflashь.

### Runtime-BLE — /data/devices.json, підписка через веб
BLE-пристрої **НЕ хардкодяться** у board.json. На stand_s3 `board.json` має `ble_devices: []`, а bindings.json містить лише два дротові біндінги — з явною нотаткою `_note_ble_runtime`, що ВСІ BLE (Xiaomi/nRF-обсервери + iPixel-панель) додаються в runtime, потім прив'язуються ролі room_temp/orientation/panel.

**Flow: scan → subscribe → bind**

```
GET /api/ble/scan ─► seen-таблиця (лише devices з type != '', тобто ІДЕНТИФІКОВАНІ)
      │
      ▼  користувач обирає пристрій на сторінці «Пристрої»
POST /api/devices ─► handle_post_devices → validate → write /data/devices.json
      │             (MAX_RUNTIME_DEVICES=12; логує "restart needed")
      ▼  RESTART
ConfigService::on_init: parse_board_json ∪ parse_devices_json  (runtime-wins-by-id)
      │  merged → HAL.remote_devices_ (MAX_REMOTE_DEVICES=16; RemoteDeviceConfig{transport,identity,name})
      ▼
роль біндиться до device id (транспорт-агностично; find_remote_device резолвить id→identity/name)
```

**Два транспортні seam'и:**

| | OBSERVER (пасивний) | CONNECT (GATT) |
|---|---|---|
| Ідентичність | MAC | повне adv-name |
| Приклад | ble_xiaomi_th, ble_nrf_tilt | ble_led_panel (prefix `LED_BLE`) |
| Seam | `adv_decoder.h` | `central_link.h` |
| Реєстрація | `register_adv_decoder` / `register_adv_mfg_decoder` | `register_connect_profile` + `register_connect_matcher` |
| Модель | push у per-MAC кеш; `update()` no-op | write ЧЕРЕЗ `ICentralLink` |

**Декодери/матчери реєструються на BOOT**, у register-хуку драйвера (`extern "C" modesp_register_driver_<name>`), НЕ у фабриці. Перевірено: `ble_xiaomi_th_driver.cpp:188-191` (`register_adv_decoder` у хуку), `ble_led_panel_driver.cpp:266` (`register_connect_matcher("LED_BLE","ble_led_panel")`). DriverManager::init викликає `modesp_register_all_drivers()`. Причина: фабрика запускається лише коли біндінг вже існує — декодер, зареєстрований у фабриці, робить НЕприв'язаний пристрій НЕВИДИМИМ у скані, тож його неможливо підписати. `modesp_ble` — чистий транспорт: весь байтовий парсинг живе в драйвері.

---

## 4. MANIFEST-DRIVEN CODEGEN — єдине джерело правди

```
project.json ─┐
modules/*/manifest.json ─┤
drivers/*/manifest.json ─┼─► tools/generate_ui.py ─► data/ui.json + generated/*.h + generated/*.cmake
boards/<b>/board.json ────┤       (+ compile_scenario.py)     + components/modesp_hal/Kconfig
boards/<b>/bindings.json ─┘                                    + main/Kconfig.boards + data/www/i18n/*
```

Генератор — `tools/generate_ui.py`. Він читає board.json/bindings.json з **`data/`** (не з `boards/`) — CMake копіює пару активної плати в `data/` на configure перед запуском генератора.

### Згенеровані файли — НІКОЛИ не редагувати руками
Кожен несе заголовок `Auto-generated … DO NOT EDIT`. Повний набір з `generate_ui.py`:

- `data/ui.json` — merged runtime UI-схема (WebUI вантажить її; сам WebUI СТАТИЧНИЙ)
- `generated/state_meta.h` — StateMeta[] + `MODESP_MAX_STATE_ENTRIES`
- `generated/mqtt_topics.h` — MQTT_PUBLISH/SUBSCRIBE/ALARM, TOPIC_ROOT, HA_ENTITIES
- `generated/display_screens.h` — LCD-меню-дерево, MAIN_VALUES
- `generated/features_config.h` — FeatureConfig[] (active за bindings)
- `generated/module_includes.h`, `module_instances.h`, `module_register.h`, `modules.cmake` — авто-реєстрація модулів з project.json (recipe-модулі виключені)
- `generated/drivers.cmake` (MODESP_ALL_DRIVERS), `required_drivers.cmake` (MODESP_BOUND_DRIVERS), `driver_register_all.h` (guarded register-all)
- `components/modesp_hal/Kconfig` — меню «ModESP Drivers» (toggle на драйвер)
- `main/Kconfig.boards` — choice плат з boards/*/board.json
- `generated/datalogger_channels.h`, `datalogger_events.h` — з manifest `loggable`
- `data/www/i18n/*.json` — мовні пакети

**Виняток:** `generated/panel_font_data.h` НЕ пише `generate_ui.py` — його продукує окремий `tools/gen_osd_font.py`. Не приписуй його manifest-генератору.

### Авто-ретригер + build-time валідація
Редагування будь-якого manifest/project.json/schema/i18n перезапускає CMake configure (через `CMAKE_CONFIGURE_DEPENDS` + `CONFIGURE_DEPENDS` GLOB'и), який перезапускає `generate_ui.py`. Ручний `idf.py reconfigure` не потрібен.

Валідація FAIL'ить білд (ненульовий exit → CMake FATAL_ERROR) двома шарами: (1) **JSON Schema** (draft-07, `tools/schemas/*.schema.json`, `additionalProperties:false`; `jsonschema` — обов'язкова build-залежність); (2) **domain-валідатори**: ManifestValidator, DriverManifestValidator, cross_validate, validate_loggable, `validate_bindings`. `validate_bindings` перевіряє: поля присутні; module є в project.json; hardware id є на платі; driver-маніфест існує; `driver.hardware_type` збігається з типом секції board; requires_address задоволено; немає дублю ролі в модулі; спільне hardware лише при `multiple_per_bus` з різними address. Warnings (unused drivers тощо) білд НЕ ламають.

---

## 5. DEPENDENCY DIRECTIONS — дозволені та заборонені стрілки

```
                platform (ESP-IDF, ETL, littlefs, bt/NimBLE)
                        ▲
   ┌────────────────────┼─────────────────────┐
 modesp_core ◄─ hal ◄─ services ◄─ net ◄─ ble  scenario  mqtt/aws  equipment
     (root)         ▲              ▲    │                              ▲
   drivers ─────────┘        ble ──┘ (PRIV_REQUIRES net)         modules/equipment
      ▲                                                                │
   (modesp_hal auto)                                            (subclass EquipmentBase)
   modules ─► framework components (НІКОЛИ ─► driver by name)
```

**Дозволено:**
- `modules → framework components → platform`. Модуль REQUIRE'ить `modesp_core` (завжди) і за потреби `modesp_hal`/`modesp_net`/`modesp_equipment`/`modesp_scenario`.
- `drivers → modesp_hal` (додається автоматично `tools/cmake/modesp_driver.cmake`). BLE-драйвери додатково `PRIV_REQUIRES modesp_ble`.
- `modesp_ble → modesp_net` (BLE приватно вимагає net — перевірено `modesp_ble/CMakeLists.txt:14`).

**Заборонено:**
- ❌ `modesp_net → modesp_ble` — стрілка ЛИШЕ `ble → net`. Інакше NimBLE потрапить в offline/WiFi-only збірки й інвертує власність транспорту. (`modesp_net/CMakeLists.txt:15` REQUIRE'ить core/services/hal — БЕЗ ble.)
- ❌ будь-який `component → module` чи `component → driver by name`. Framework не залежить від product.
- ❌ будь-який `modesp_* у modesp_core.REQUIRES` — core є коренем (REQUIRE'ить лише ETL + platform).

**Опційність:** net/ble/scenario/mqtt/aws гейтять SRCS на `CONFIG_*` (порожній компонент, коли off), але REQUIRES гейтити НЕ можна (ESP-IDF розкриває requirements до завантаження sdkconfig). Cloud — взаємовиключний Kconfig-choice (MQTT | AWS | NONE); main лінкує рівно один через `target_link_libraries`. Тому root COMPONENTS явно перелічує mqtt/aws/ble (target_link_libraries не бере участі в discovery).

---

## 6. EXTENSION RECIPES

### Додати МОДУЛЬ
1. `modules/<name>/`: `manifest.json` (`"module":"<name>"`), `CMakeLists.txt`, клас `<Name>Module : public BaseModule`.
2. Додати `"<name>"` у `project.json → modules`.
3. `idf.py build`. Генератор сам робить includes/instances/register (сортує за priority)/modules.cmake.
- Ім'я: `^[a-z][a-z0-9_]*$`, папка == поле `module`. Клас за замовч. CamelCase+`Module` (override через `class_name`). `module_type:"recipe"` → виключений з C++.
- Хуки BaseModule: `on_init` / `on_update(dt)` / `on_message` / `on_stop` / `on_bind(DriverManager&, BindingTable&, HAL&)` — ЄДИНЕ місце, де hardware-модуль резолвить драйвери з bindings.

### Додати ДРАЙВЕР (sensor/actuator)
1. `drivers/<name>/manifest.json`: `driver`, `category` (sensor|actuator|io|display|audio), `hardware_type` (одне зі значень з `BOARD_SECTION_TO_HW_TYPE`), `provides`, `requires_address?`, `multiple_per_bus?`, `settings`.
2. У `.cpp`: фабрика `fn(const Binding&, HAL&)` + ОДИН макрос на file-scope: `MODESP_REGISTER_SENSOR(<name>,&factory)` / `MODESP_REGISTER_ACTUATOR(...)` / `_WITH_DISCOVERY` / `_DISPLAY` / `_AUDIO`.
3. `CMakeLists.txt` через `modesp_driver_component()` (робить драйвер опційним: SRCS гейтяться на `CONFIG_MODESP_DRIVER_<NAME>`).
4. Використати в `bindings.json`.
- **Ім'я драйвера == папка == перший арг макросу** — генератор механічно виводить `modesp_register_driver_<name>`. Розбіжність = link-error або тихо не зареєстрований.
- **display/audio** створює НЕ DriverManager, а модуль-власник у своєму `on_bind` (`create_display`/`create_audio`) — це module-bound backend-и (`is_module_backend`). `IDisplayPort` — семантичний seam (ADR-002: геометрія НЕ протікає через caps()). **panel — НЕ такий:** це звичайний актуатор, якого створює DriverManager; модуль резолвить його за роллю (`find_actuator(role)->as_panel()`) — багата периферія без окремого seam.

### Додати РОЛЬ
У `requires[]` модуля-власника: `{role, type, driver:[...], label, optional?}`; на платі — binding `{hardware, driver, role, module}`. Модуль читає значення з SharedState/резолвленого драйвера — GPIO не чіпає. Одне BLE-device живить кілька ролей через `address_channels` (binding.address обирає канал).

### Додати BLE device type
- **OBSERVER:** sensor-драйвер (`hardware_type:"ble"`, `requires_address:true`) + у boot-хуку `register_adv_decoder(fn)` (16-bit service data) або `register_adv_mfg_decoder(fn)` (manufacturer). Репорт через `report_sensor()`.
- **CONNECT:** `register_connect_profile({name_prefix, write_uuid, notify_uuid, on_notify})` у фабриці (повертає `ICentralLink`) + `register_connect_matcher(prefix, type)` у BOOT-хуку (щоб непри­в'язаний пристрій було видно у скані). Wire-format живе в драйвері; `modesp_ble` device-агностичний.
- Все під `#if CONFIG_MODESP_BLE_ENABLE && CONFIG_MODESP_BLE_CENTRAL`.

---

## 7. INVARIANTS — DO NOT BREAK

1. **Роль оголошує лише модуль-власник** (той, що її споживає). Не клади роль у чужий модуль. Це стосується ВСІХ типів периферії — навіть багатий connect-драйвер (panel) оголошує роль у своєму модулі й резолвиться за роллю, а не через глобал.
2. **НІКОЛИ не редагуй згенеровані файли** — `data/ui.json`, весь `generated/*.h` + `generated/*.cmake`, `components/modesp_hal/Kconfig`, `main/Kconfig.boards`, `data/www/i18n/*`. Змінюй МАНІФЕСТ; CMAKE_CONFIGURE_DEPENDS перегенерує.
3. **Ніякого транспорт-специфічного поля на Binding.** Binding посилається на device id; ідентичність (transport identity/adv-name) живе на рядку пристрою (board.json/devices.json), не в bindings.json. `find_remote_device` резолвить id→identity/name (транспорт-агностично).
4. **Декодери/матчери реєструються на BOOT** (register-хук `modesp_register_driver_<name>`), НЕ у фабриці — інакше неприв'язаний пристрій невидимий у скані й не підписується.
5. **Дротове → board.json; runtime-BLE → devices.json.** board.json — GET-only; його BLE-секція — лише factory seed. Не хардкодь BLE в board.json.
6. **Напрями залежностей:** modules→framework→platform; drivers→hal; ble→net (НЕ net→ble); ніщо не залежить від modesp_core у зворотний бік; framework не залежить від product.
7. **`binding.module` — маршрутизація.** Він мусить називати модуль з project.json І бути власником ролі, інакше білд падає або залізо ні до кого не під'єднане.
8. **Ім'я драйвера/модуля == папка == поле маніфесту** (`^[a-z][a-z0-9_]*$`); для драйвера ще == перший арг register-макросу.
9. **Опційні компоненти гейтять лише SRCS на CONFIG_*, ніколи REQUIRES.** Драйвер завжди через `modesp_driver_component()`, ніколи голий `idf_component_register`.
10. **Модулі не чіпають GPIO** — тільки `ISensorDriver`/`IActuatorDriver`/`IDisplayPort`/`IAudioSink`/`IPanelPort` через bindings + SharedState/publish. Один драйвер = один register-макрос. Аналогові актуатори мусять override'ити `set_value/get_value/supports_analog` (default = discrete on/off).
11. **Cloud — взаємовиключний** (mqtt XOR aws XOR none); board хардкод у модулях заборонений — залізо виражається лише через board.json/bindings.json. Редагуй їх у `boards/<board>/`, НЕ в `data/` (там копії, що перезаписуються).
12. **Ліміти — жорсткі кепи:** MAX_BINDINGS=24, MAX_REMOTE_DEVICES=16, MAX_RUNTIME_DEVICES=12, MAX_LOG_CHANNELS=6, меню ≤255 nodes / ≤15 root-submenus. Незнана секція board.json тихо ігнорується (лише warning) — hardware зникає.
