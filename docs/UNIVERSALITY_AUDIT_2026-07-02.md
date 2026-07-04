# Аудит універсальності ModESP Framework

## 1. Загальна оцінка

Мета — "додав папку + рядок у project.json → все працює" — досягнута приблизно на **65%**. Механізми (реєстри, генератор, Kconfig-пайплайн) переважно існують і працюють; ламається все на композиційному корені (main.cpp, main/CMakeLists.txt) та на межах шарів, де framework досі знає імена конкретних модулів і драйверів еталонного продукту.

| Шар | Оцінка | Обґрунтування |
|---|---|---|
| **Ядро** (modesp_core/services) | **7/10** | ModuleManager/BaseModule/SharedState name-agnostic, capacity з генератора; але немає bind-хука, а в core-хедерах живе холодильна лексика (panel.slot*, DEFROST_*, ALARM_*) |
| **Драйвери** (sensor/actuator/io) | **8/10** | DriverRegistry + генерований Kconfig/register-all = справжній drop-in для 9 драйверів; ламається на display/audio, ds18b20-у-HTTP і закритому словнику hardware_type |
| **Модулі** | **6/10** | Реєстрація/UI/state/datalogger генеруються; але add/remove вимагає до 3 ручних правок framework-файлів, а framework-інфраструктура (EquipmentBase, DataLogger) живе в modules/ |
| **Генератор** | **7/10** | state_meta/mqtt_topics/datalogger/Kconfig — реально manifest-driven з крос-валідацією; але спецкейси на ім'я 'equipment', мертвий modules.cmake, українська як зашита мова, нуль JSON-схем |
| **Build** | **5/10** | Правка маніфесту НЕ перезапускає генерацію (критично); ручне дублювання списку модулів; board-профіль у глобальних sdkconfig |
| **Сервіси** | **4/10** | Опційний тільки BLE; net/scenario/cloud завжди компілюються і реєструються; modesp_net тягне datalogger і ds18b20 |
| **WebUI** | **6/10** | Generic-рендеринг ui.json чистий; але bindings-редактор, чарти й події приварені до еталонного продукту (equipment, onewire, холодильні канали) |

---

## 2. Що вже добре

Ці механізми — фундамент мети, їх треба зберегти і поширити, а не переписувати:

- **DriverRegistry + MODESP_REGISTER_SENSOR/ACTUATOR** (components/modesp_hal/driver_registry.h) — нуль хардкодів імен драйверів у driver_manager; фабрика реєструється в .cpp самого драйвера.
- **Генерований driver-пайплайн**: `components/modesp_hal/Kconfig`, `generated/drivers.cmake`, `driver_register_all.h`, `required_drivers.cmake` + `modesp_driver_component()` + `drivers_sync.py --fix/--prune` — sensor/actuator/io драйвер сьогодні реально drop-in-папка. Це еталонний патерн для решти рефакторингу.
- **Build-time валідація bindings↔board↔driver** (generate_ui.py `validate_bindings`) — невалідна прив'язка ламає білд; FATAL при вимкненому в menuconfig драйвері.
- **Генератор core-виходів**: state_meta.h (з автосайзингом MODESP_MAX_STATE_ENTRIES), mqtt_topics.h, datalogger_channels/events.h (stable explicit IDs), display_screens.h, module_includes/instances/register.h, .modr-рецепти з крос-валідацією mirror-ключів.
- **modesp_ble як зразок опційності**: справжній Kconfig master switch + empty-component pattern при вимкненні + генеровані key-списки. Це готовий шаблон для net/scenario/cloud.
- **Generic-рендеринг WebUI**: Dashboard, DynamicPage, Layout, visibility rules, WidgetRenderer споживають ui.json без знання імен модулів; board.json — чисте залізо; вибір плати крос-валідується.
- **Рецепти (abs_test)** — повністю manifest-only, жодного C++.

---

## 3. Знахідки

### 3.1. Ручні кроки та build-пайплайн

🔴 **Правка маніфесту не перезапускає генерацію** — `CMakeLists.txt:91`. Єдині CMAKE_CONFIGURE_DEPENDS — board.json/bindings.json активної плати. modules/\*/manifest.json, drivers/\*/manifest.json, project.json і самі generate_ui.py/compile_scenario.py — НЕ configure-inputs, тож після першого configure правка маніфесту + `idf.py build` мовчки шипить stale ui.json, datalogger_channels.h, Kconfig, drivers.cmake. Документація (generate_ui.md:181-195) описує неіснуючий add_custom_command з DEPENDS. Це ламає центральний контракт single-source-of-truth. **Фікс:** додати project.json, обидва tools/-скрипти і glob маніфестів у CMAKE_CONFIGURE_DEPENDS (або перевести генерацію на add_custom_command з явними DEPENDS).

🟠 **main/CMakeLists.txt:7 — ручний список модулів, а generated/modules.cmake мертвий**. Коментар у файлі сам наказує «update this list too», хоча generate_ui.py:2309 вже пише `set(PRODUCT_MODULES ...)` саме для цього — grep показує нуль споживачів PRODUCT_MODULES, а docs брешуть, що main його використовує. **Фікс:** `include("${CMAKE_SOURCE_DIR}/generated/modules.cmake")` + `PRIV_REQUIRES ... ${PRODUCT_MODULES}` (modesp_hal вже доводить патерн із drivers.cmake); видалити ручний список і крок 3 з CLAUDE.md.

🟠 **Список плат зашитий у main/Kconfig.projbuild:2**. Вибір MODESP_BOARD (DEV/KC868A6/STAND_S3) + мапінг MODESP_BOARD_DIR — вручну; нова boards/my_board/ недосяжна без правки framework-Kconfig (confgen мовчки губить невідомий символ → FATAL на mismatch), і жоден board-док цього кроку не згадує. **Фікс:** генерувати Kconfig.boards зі скану boards/\*/board.json (той самий патерн, що драйверний Kconfig).

🟠 **Кастомні scenario-дії вимагають правки центрального tools/known_actions.json:2** (з ручними djb2-хешами), а білд запускає compile_scenario `--strict` (CMakeLists.txt:121), який підіймає W0220→E0226. Продуктова дія, зареєстрована в рантаймі, не може потрапити в рецепт без правки framework-файлу. **Фікс:** `provides_actions` у manifest.json модуля, merge у KnownActionRegistry, авто-хеші; known_actions.json — лише для built-ins.

🟠 **Display/audio виключені з driver-пайплайна** — generate_ui.py:2340 (`DRIVER_COMPONENT_CATEGORIES = {"sensor","actuator","io"}`). drivers/amt630a і drivers/max98357a — manifest-only заглушки; реальний код у модулях: ручні #ifdef-реєстри (display_backend_registry.cpp:55-70, audio_backend_registry.cpp:56), ручні Kconfig (CONFIG_MODESP_DISPLAY_*/AUDIO_*), хардкод SRCS у CMake; max98357a_sink.cpp прямо смикає gpio_config()/i2s_* всередині modules/player. Новий бекенд = правка трьох ручних списків. Власний ADR-002 (docs/display/ADR-002:290) вже планує саме генерований шлях. **Фікс:** MODESP_REGISTER_DISPLAY/AUDIO + modesp_driver_component() + генеровані Kconfig/register-all для цих категорій; у модулях лишити тільки бізнес-логіку над IDisplayPort/IAudioSink.

🟠 **Усі modules/ і drivers/ компілюються незалежно від project.json** — `CMakeLists.txt:4` (EXTRA_COMPONENT_DIRS без set(COMPONENTS ...)). Модуль, викинутий із project.json, дереєструється, але його компонент компілюється щобілду; повне видалення = стерти папку + правити main/CMakeLists.txt. Драйвери мають Kconfig-гейт SRCS, модулі — ні. **Фікс:** живити `set(COMPONENTS main ${PRODUCT_MODULES} ...)` з того ж modules.cmake, або генеровані CONFIG_MODESP_MODULE_<NAME>.

### 3.2. Хардкоди у framework-коді (ядро, сервіси, webui)

🟠 **main.cpp вручну в'яже конкретні інстанси** — main/main.cpp:237/241/245/324: `equipment.bind_drivers(...)`, `display.bind_display(...)`, `player.bind_audio(...)`, `http_service.set_datalogger(&datalogger)`. Викинь 'equipment' із project.json — main.cpp не компілюється; новий модуль із залізом = правка main.cpp, бо BaseModule не має bind-хука (base_module.h:41-44 — лише on_init/update/message/stop). Реєстрація generic, bind — ні. **Фікс:** `virtual void BaseModule::on_bind(DriverManager&, const BindingTable&, HAL&)`, який ModuleManager кличе між реєстрацією та init_all; set_datalogger → capability-lookup, толерантний до відсутності.

🟠 **HA-discovery таблиця в MQTT-сервісі називає продуктові ключі** — components/modesp_mqtt/src/mqtt_service.cpp:1066: `ENTITIES[] = {{"equipment.air_temp",...},{"simple_thermo.setpoint",...}}`, публікується безумовно з retain=1 на кожен connect; коментар сам визнає «table is hardcoded». Продукт без simple_thermo лишає мертву retained-сутність на брокері. **Фікс:** HA-метадані (device_class, unit, entity_type) у mqtt-секції маніфесту → генерований `gen::HA_ENTITIES[]` у mqtt_topics.h (точно як state_meta.h).

🟠 **Мертвий префікс 'protection.' керує QoS/retain алармів** — mqtt_service.cpp:400: `strncmp(key, "protection.", 11)` — ім'я модуля видаленого холодильного продукту; нуль збігів у modules/, тож reliable-delivery шлях мертвий, а справжні аларм-ключі нового продукту мовчки йдуть QoS 0 без retain. **Фікс:** прапорець `"alarm": true` (або qos/retain) у mqtt-ключах маніфесту → генерований бітмап.

🟠 **EquipmentBase детектить драйвери за літералами "ntc"/"ds18b20"** — modules/equipment/src/equipment_base.cpp:181: strcmp по type() → equipment.has_ntc_driver/has_ds18b20_driver (продубльовано в manifest.json:14-15, BindingsEditor.svelte:130, OneWireDiscovery.svelte:7, i18n en/de/pl). Новий сенсорний драйвер не отримує capability-прапорця без правки базового класу. **Фікс:** generic `equipment.has_<type>_driver` для кожного bound-типу в існуючому циклі; webui ітерує generic-набір.

🟠 **EquipmentBase вирішує bool-vs-float за ім'ям "digital_input"** — equipment_base.cpp:270, хоча маніфести вже декларують provides.type: pcf8574_input має `{"type":"bool"}`, але публікується як float 0.0/1.0 і ще й EMA-фільтрується (живий баг на kc868a6: 6 прив'язок). **Фікс:** `virtual ValueKind kind()` у ISensorDriver або генерована таблиця driver_type→value_kind з provides.type.

🟠 **Вибір cloud-бекенда — ручний 2-way #if без інтерфейсу** — main/main.cpp:54-58/107-111 + if/else у main/CMakeLists.txt:11-17 + 2-варіантний choice у Kconfig.projbuild. MqttService/AwsIotService duck-typed (aws_iot_service.h:32 сам це документує); невибраний бекенд не компілюється → дрейф інтерфейсу непомітний. Третій бекенд = правка трьох framework-файлів. **Фікс:** ICloudService у modesp_core + CloudRegistry за патерном драйверів; генератор емить choice/link-list.

🟠 **HAL I2C-експандер = протокол PCF8574** — components/modesp_hal/src/hal.cpp:362-418: single-byte quasi-bidirectional, 0xFF power-on, 8 пінів; поле board.json `"chip"` парситься (hal_types.h:91), але ніде не диспатчиться — `"chip":"pca9555"` мовчки поїде не тим протоколом. Регістрові експандери (MCP23017/PCA9555) неможливі як drop-in. **Фікс:** протокол чипа — в драйвер-компоненти; HAL тримає лише i2c dev handle + generic transfer, або backend-реєстр за 'chip'.

🟡 **EquipmentBase ігнорує власне ім'я — префікс "equipment." зашитий** (equipment_base.cpp:171 та ін.). Конструктор приймає ім'я, key-форматування його ігнорує; `EquipmentBase("cold_room")` мовчки публікує під equipment.*. Оскільки equipment.* — свідомий крос-модульний контракт, фікс мінімальний: assert/коментар, а довгостроково — вивести namespace з name() синхронно з маніфестами.

🟡 **Message-ID модулів вручну алоковані в core types.h:117** з холодильними залишками: ALARM_TRIGGERED=150, SETPOINT_CHANGED=160, DEFROST_START/END=170/171 (+ MsgSetpointChanged у driver_messages.h) — усе мертвий код без publishers/subscribers. **Фікс:** видалити; за потреби — manifest-driven алокація ID за патерном datalogger_events.h.

🟠 **ChartWidget: live-оновлення мертві для всіх реальних каналів** — webui/src/components/widgets/ChartWidget.svelte:64: liveVals мапить air/evap/cond/setpoint/humidity → equipment.evap_temp, thermostat.setpoint тощо; реальні генеровані канали — 'air_temp'/'temperature', модуля 'thermostat' не існує; баг маскується dev-server-моками. **Фікс:** генератор вже знає id→state_key (LOG_CHANNELS) — емитити мапу в конфіг віджета в ui.json.

🟠 **Події datalogger у webui — магічні ID замість експорту з маніфестів** — ChartWidget.svelte:38: ALARM_CLEAR=7, POWER_ON=10, евристика `e[1]>=5` = alarm, лейбли через ручні chrome-ключі 'event.7'/'event.10' → маніфестні події 30/31, 40/41 рендеряться "Event #30" з хибним alarm-стилем; zone-pairing інвертований (l.181 збирає непарні як ON, а генеровані ON — парні 30/40) → зони малюються неправильно вже на demo-модулі. **Фікс:** генератор емить таблицю подій (id, pair_id, kind, i18n-лейбли) в ui.json з тих самих даних, що datalogger_events.h; викинути парність/пороги/chrome-ключі.

🟠 **Ручні driver-settings картки в webui** — BindingsEditor.svelte:237 (NTC: equipment.ntc_beta/... з JS min/max) + OneWireDiscovery.svelte:7-20 (ds18b20). Гірше: за bindings.md:94-100 глобальні equipment.ntc_*-ключі «never actually wired» — 4 із 5 інпутів пишуть в осиротілі ключі (тихе no-op калібрування). **Фікс:** емитити settings із драйверних маніфестів (per-binding!) у bindings-сторінку ui.json і рендерити generic WidgetRenderer'ом.

🟠 **Address/sharing у bindings-редакторі — onewire-only** — BindingCard.svelte:16 (`needsAddress = hw_type === 'onewire_bus'`) + BindingsEditor.svelte:53 (`SHAREABLE_HW = Set(['onewire_bus'])`), хоча маніфести вже мають requires_address/multiple_per_bus і генератор емить requires_address у ui.json. Наслідок: конфіг stand_s3 (xiaomi_room × 3 ролі з адресами) неможливо ані створити, ані виразити в редакторі. **Фікс:** гейтити address-input на roleDef.requires_address з ui.json; 'shareable' — генерований прапорець із маніфестів.

🟡 **BindingsEditor штампує module:'equipment'** (BindingsEditor.svelte:111) + EquipmentStatus форматує всі числа як °C (:15) — mislabel для room_humid/room_batt. Корінь — генератор дає roles[] лише з equipment (див. 3.6). **Фікс:** unit/type per role у ui.json; °C — з декларованого unit.

### 3.3. Порушення шарів

🟠 **EquipmentBase (framework-клас) живе в modules/equipment** — equipment_base.h:36, 347 рядків; власний CMakeLists коментує «EquipmentBase = framework». Продукт, що пише `class MyEquipment : public EquipmentBase` (як велять docs), мусить залежати від product-модуля або копіювати файли; магічне ім'я 'equipment' зав'язує main.cpp і три спецкейси генератора. **Фікс:** перенести в components/modesp_equipment; modules/equipment — тонкий demo-сабклас; генератор ключиться на capability-прапорець маніфесту, не на ім'я.

🟠 **modesp_net → datalogger** — http_service.cpp:18 безумовний include datalogger_module.h, CMakeLists.txt:9 PRIV_REQUIRES datalogger, зберігає DataLoggerModule* (http_service.h:28/57/82). Framework-компонент залежить від "продуктового" модуля; продукт без datalogger не збере main. **Фікс:** ILogSource/IHistoryProvider у modesp_core (або generic route-registration у HttpService), datalogger реєструється сам; /api/log вже null-guarded.

🟠 **modesp_net → ds18b20** — http_service.cpp:1321-1334: DS18B20Driver::scan_bus/read_temp_by_address, `b.driver_type == "ds18b20"`, CMakeLists.txt:9 безумовний PRIV_REQUIRES ds18b20; видалення drivers/ds18b20 ламає framework-білд; manifest-поле discovery.scan_endpoint існує, але не використовується (реальний маршрут /api/onewire/scan ≠ задекларований /api/drivers/ds18b20/scan). **Фікс:** `DriverRegistry::register_discovery(type, scan_fn)` поруч із MODESP_REGISTER_SENSOR + generic `/api/drivers/<type>/scan`, диспатч через реєстр.

🟠 **Протоколи Xiaomi/BTHome/iPixel живуть у modesp_ble** — ble_service.cpp:958-987 (декодери 0x181A/0xFCD2), :640-644 (GATT fa00/fa02/fa03), :65 (панельний шрифт). drivers/ble_xiaomi_th і ble_led_panel — тонкі шими; новий BLE-формат = правка framework-компонента. Рішення документоване (phase3b_plan), але суперечить drop-in-меті цілої категорії. **Фікс:** modesp_ble = транспорт (scan/connect/GATT) + API реєстрації adv-декодерів (UUID→parse_fn) і connect-профілів із .cpp драйверів.

🟠 **PanelModule → BlePanel-singleton в обхід driver/SharedState** — panel_module.cpp:65: `modesp::BlePanel::instance()`; data-plane (текст/power/brightness) минає bindings/DriverManager, raw iPixel-байти продубльовані в модулі (l.75/83) і драйвері. **Фікс:** iPixel-кодування повністю в drivers/ble_led_panel, панель для модулів — actuator-style інтерфейс через bindings.json.

🟡 **panel_text.h у modesp_core** — panel_text.h:34 зашиває "panel.slot0..4"/SLOTS=5, дублюючи modules/panel/manifest.json (+ приклад 'DEFROST' у коментарі). Свідомий trade-off, але два джерела правди. **Фікс:** генерувати slot-константи з panel-маніфесту.

### 3.4. Дірки в маніфестах

🟠 **Driver-manifest 'settings'/persist/discovery — мертва схема** — drivers/ds18b20/manifest.json:33 і далі: settings[].persist:true, ui.cards, discovery.scan_endpoint задекларовані, generate_ui.py і webui не мають жодного споживача (grep нуль); рантайм-значення — тільки з bindings.json; generate_ui.md:36 хибно стверджує, що driver ui читається. Схема обіцяє більше, ніж тулчейн виконує, і ніхто не попереджає. **Фікс:** або реалізувати контракт (per-binding settings UI + persist-шлях + discovery-маршрути), або вирізати мертві поля зі схеми/доків + WARNING на неспожиту 'ui'-секцію.

🟠 **'features': спека та імплементація — дві різні фічі** — generate_ui.py:893 vs manifest.md:289-312. Документовані typed-прапорці `{"type":"bool","default":false}`; код знає лише always_active/requires_roles; фіча без requires_roles → `set() ⊆ bound_roles` = вакуумно True → default:false мовчки інвертується в ACTIVE; валідатор не флагає невідомі поля. **Фікс:** обрати binding-derived модель, переписати manifest.md, валідатор reject'ить 'type'/'default'.

🟠 **Нуль JSON-схем для module/driver/board/bindings/project** — generate_ui.py:60+. Схема є лише в scenario. `"persits": true` мовчки губить persistence (l.1587/1595); невідомий widget-тип → compat=None → перевірку пропущено (l.132); string у 'priority' → сирий TypeError у сортуванні (l.2288); однина 'gpio_output' у board.json мовчки ігнорується. **Фікс:** module/driver/board/bindings/project_schema.json (draft-07, additionalProperties:false) + jsonschema (вже залежність) + warn на нерозпізнані top-level ключі.

🟠 **Ліміт 32 символи на state-ключ не перевіряється** — обіцяно в manifest.md:77/generate_ui.md:46, у ManifestValidator (generate_ui.py:90+) перевірки немає; ETL мовчки обрізає до 32 (ризик колізій ключів), E0402 покриває лише mirror-ключі рецептів. **Фікс:** len(key)>32 → ERROR; заодно module name ≤16.

🟠 **bind_drivers() ігнорує manifest 'requires'** — equipment_base.cpp:33-35 (коментар сам зізнається): копіює все з DriverManager; whitelists/optional не enforced ніде — навіть build-time validate_bindings не звіряє role із requires (stand_s3 в'яже недекларовані room_temp/room_humid — і вони публікуються); документований «missing optional=false → abort startup» не реалізований. **Фікс:** генерована constexpr-таблиця ролей із requires (патерн datalogger_channels.h); bind по ролях, fail на відсутніх non-optional, reject не-whitelisted.

🟠 **DisplayModule читає/пише 7 недекларованих ключів** — display_module.cpp:96-97/135-147/198-202: display.banner, banner_level, backlight, contrast, brightness, saturation, input відсутні в маніфесті → нема в state_meta.h → WebUI/MQTT відмовляють у записі (прямо всупереч коментарю модуля), persist неможливий. **Фікс:** задекларувати всі 7 у manifest.state + build-check «ключ у коді, але не в маніфесті».

🟠 **at7456e: нема драйверного маніфесту, SPI-піни в Kconfig** — modules/display/Kconfig:16 (власний TODO «перенести у board.json»); drivers/at7456e/ не існує → bindings.json із driver "at7456e" валить білд (validate_bindings), тобто бекенд мертвий через власний документований шлях активації; дві плати з різною розводкою AT7456E неможливі. **Фікс:** drivers/at7456e/manifest.json (як amt630a) + секція spi_buses у board.json/HAL; піни з HAL, Kconfig-піни видалити.

🟠 **persist:true на string-ключах мовчки ігнорується** — persist_service.cpp:90: гілки лише float/int/bool; generated/state_meta.h:47-48 має {"panel.message","string",persist=true} із modules/panel/manifest.json — ключі ніколи не відновлюються/не зберігаються, без жодного warning на генерації. **Фікс:** batch_read_str/write_str + "string"-гілка, або generate_ui.py FAIL на persist+string.

🟡 **Panel/Presence читають equipment.<role> літералами без machine-readable декларації** — panel_module.cpp:157-187 (equipment.room_temp/room_humid — ролі існують лише в stand_s3/bindings.json), presence_module.cpp:31-37 (equipment.presence/move_distance — у маніфесті лише прозове "_inputs_note"). Механізм 'inputs' із валідацією в генераторі ВЖЕ існує (generate_ui.py:347-402) — треба лише вживати його і розширити на динамічні equipment.<role>. Перейменування ролі мовчки вимикає споживача.

🟡 **Board-профіль у глобальних sdkconfig.defaults** — sdkconfig.defaults.esp32s3:8 зашиває STAND_S3+BLE+MAX98357A+16MB+partitions_16mb.csv; boards/*/ не мають ані sdkconfig-фрагмента, ані flash/partition-полів. **Фікс:** boards/<name>/sdkconfig.board + flash_size/partition_table у board.json.

### 3.5. Неопційні сервіси

🟠 **Cloud без опції 'none'** — main/Kconfig.projbuild:26-39: choice лише MQTT/AWS; main.cpp:107-110/210 завжди інстанціює і реєструє cloud_service; вимикається лише runtime-NVS. **Фікс:** CONFIG_MODESP_CLOUD_NONE + empty-component для обох бекендів (патерн modesp_ble) + #if-гейт у main.cpp.

🟠 **modesp_net неможливо вимкнути** — main.cpp:96-98/208/329-330: WiFi/Http/Ws без жодного Kconfig (grep MODESP_NET — нуль); mqtt/aws/ble всі PRIV_REQUIRES modesp_net → мережа = жорсткий hub. Offline/BLE-only продукт неможливий без правки startup-коду. **Фікс:** CONFIG_MODESP_NET_ENABLE (default y) + empty-component + Kconfig-depends у залежних; врахувати OTA-rollback-петлю (main.cpp:396).

🟠 **Scenario-engine неможливо вимкнути** — main.cpp:247-288 безумовно в'яже Engine/NvsObserver/persist-задачу (BLE поруч гейтиться #if); main і modesp_net безумовно PRIV_REQUIRES modesp_scenario; 8 /api/scenario/* завжди реєструються (http_service.cpp:1995-2002). **Фікс:** CONFIG_MODESP_SCENARIO_ENABLE, гейт wiring + endpoints, скіп compile_scenario.py при off.

🟡 **modesp_osd завжди компілюється** — components/modesp_osd/CMakeLists.txt:4: at7456e.cpp+amt630a.cpp без CONFIG-гейта (флеш рятує лише gc-sections; кост compile-time + асиметрія з рештою драйверів). **Фікс:** гейт SRCS на CONFIG_MODESP_DISPLAY_* (як ble_service.cpp), або дочекатися переїзду в drivers/ (див. 3.1).

### 3.6. Генератор

🟠 **Спецкейси на літеральне ім'я 'equipment'** — generate_ui.py:2110-2115 (FeatureResolver будується лише якщо є маніфест 'equipment', інакше features_config.h мовчки all-false з хибним коментарем), :287 (V15 збирає ролі тільки з нього), :992-995 (bindings-сторінка бере ролі тільки з нього — ролі display/player зі stand_s3 невидимі в редакторі), :305-309 (whitelist 'equipment.has_'). **Фікс:** вибирати role-provider'ів за capability (наявність top-level requires / прапорець provides_roles), агрегувати requires[] з УСІХ таких модулів з `role.module` в ui.json; ERROR коли є bindings, але нема провайдера ролей.

🟠 **HAL-словник hardware_type закритий і продубльований у 5+ місцях** — generate_ui.py:466-472/619-632, hal_types.h, hal.cpp init_*/find_*, ручний jsmn-парсер config_service.cpp:214+ (невідомі секції board.json мовчки скіпаються; board-специфічні дефолти зашиті: `active_high=false // KC868-A6`, `pin_count=8 // PCF8574`). Списки вже роз'їхались: **"pwm_channel" — фантом** (валідується, але нема ні структури, ні init, ні парсингу), SPI відсутній (що й загнало at7456e-піни в Kconfig). Нова шина (SPI/CAN/RS-485) = координовані правки в 5 місцях. **Фікс:** коротко — прибрати pwm_channel або доробити end-to-end, warn на невідомі секції, дефолти в board.json; стратегічно — одна table-driven дескрипторна таблиця hardware-типів для генератора+парсера+HAL.

🟠 **i18n: українська зашита як source-мова, збіг перекладів за точним рядком** — generate_ui.py:2710 (`{"languages":["uk"]+..., "default":"uk"}`), :2637-2694 — reverse-map по точній UA-рівності: два модулі з однаковим українським текстом і різними перекладами мовчки колізують (last-write-wins); webui теж зашиває uk (stores/ui.js:75, stores/i18n.js LANGS). Продукт англійською неможливий без правок генератора+webui. **Фікс:** system.source_lang у project.json; усі translatable-поля через структуровані i18n-ключі (widget.i18n_key вже існує — поширити на titles/labels/roles); dedupe languages.

🟠 **MAX_CHANNELS=6 зашито, переповнення — лише WARNING** — generate_ui.py:2452-2454; LOG_CHANNELS_COUNT емититься більшим за runtime-ємність (datalogger_module.h:26), канали 7+ мовчки ніколи не логуються — тихе втрачання даних у промисловому логері, всупереч fail-fast позиції решти валідації. **Фікс:** overflow → build ERROR; ємність піднімається через project.json (system.datalogger.max_channels); сумісність бінарного формату — через channel count у заголовку файлу.

🟠 **Cloud-provider у генераторі — строгий if/else 'aws' vs 'mqtt'** — generate_ui.py:1198-1280: віджети/тексти/endpoints обох провайдерів у Python, 'none' немає (else завжди емить MQTT-картку навіть для offline-продукту); UI-селектор (project.json) і firmware-селектор (Kconfig) — несинхронізовані джерела правди. **Фікс:** кожен cloud-компонент постачає manifest-фрагмент своїх карток; генератор вибирає за project.json включно з 'none'.

🟡 **Network/System сторінки зашиті в Python** — generate_ui.py:1142-1395: ~250 рядків структури з фіксованими endpoints. Локалізація вже працює через language packs, але зміна структури понад передбачені knobs (system.pages, cloud_provider) = правка framework-Python. **Фікс:** framework-owned маніфести (modesp_net/modesp_services) через той самий _module_page-пайплайн.

### 3.7. Дрібні знахідки (не перевірено вручну)

- App::run() — мертвий другий lifecycle без driver update (app.cpp:63); modesp_json — порожня заглушка; холодильні коментарі в types.h/persist_service/driver_messages; MODESP_MAX_MODULES=24 — фіксований define (docs обіцяють неіснуючий Kconfig).
- `${CMAKE_SOURCE_DIR}/generated` у публічних INCLUDE_DIRS core-компонентів — core не збирається standalone; modesp_scenario включає generated/ дарма; Kyiv TZ + pool.ntp.org зашиті в main.cpp:300.
- digital_input/pcf8574_input декларують 'invert'-setting, який код не читає; мертві ds18b20_messages.h/relay_messages.h; gen_osd_font.py — закритий список таргетів; BindingSetting — float-only, тихе обрізання на 6 записів; 16-char DriverType не перевіряється генератором (тиха трункейшн на девайсі); I2C-порт — за порядком декларації, не за board.json; LD2410-дефолти в generic HAL-типах; validate_bindings ламає документований порожньо-адресний default-канал ld2410b; всі HAL-шини компілюються завжди.
- Datalogger логує EVENT_ALARM_CLEAR на falling будь-якого rising-event; шаблон EquipmentModule обіцяє pass-through, але тіло порожнє (demo-вихід не доходить до actuator_1); demo-модулі невідрізнимі від інфраструктури в project.json.
- compile_scenario: mirror-ключі треба вручну дублювати в manifest.state хоч вони derivable; set_state спецкейснуто за ім'ям; зарезервовані event ID 7/10 зашиті; widget-словник продубльований generator↔webui з тихими fallback'ами; dashboard завжди бере перший card модуля; i18n-coverage-валідація — no-op; нема перевірки folder==manifest name (плутані link-помилки).
- Docs-drift: неіснуючий --strict, хибний determinism-claim, 5 мертвих посилань docs/* у CLAUDE.md; SEQUENCE_RUNTIME_MARGIN=96 з ручним переписом ключів; BLE-адресація inline-спецкейс у валідаторі; MAX_MENU_ITEMS=16 продубльовано з C++; генерація в source tree (два build-dirs клоберять одне одного); PROJECT_VER=1.0.1 вже розійшовся з project.json 1.0.0.
- Cloud-бекенди завжди компілюються обидва; datalogger REQUIRES esp_http_server (httpd_req_t у публічному API); tests/host/CMakeLists — ручні списки модулів; run_build.ps1 — машино-специфічний шлях; redirect-header ota_handler.h; бренд 'modesp' у topic-префіксах.
- WebUI: StatusText із defrost-словником; мертвий doReset→'protection' + ~250 рядків холодильного CSS; мертвий MiniChart; chrome-i18n вручну (LANGS зашитий, de/pl неповні, stale data/i18n/manifest.json); dev-server мокає холодильні ключі; data/audio/2.mp3 поза модулем; сніжинка '❄' у Layout.

---

## 4. Дорожня карта

### Фаза 1 — Quick wins (~2 тижні, ~15 файлів, без зміни архітектури)

Мета: build-пайплайн стає надійним, add/remove модуля зводиться до project.json, увесь мертвий/оманливий контракт зникає.

1. **Configure-deps** (🔴 3.1): додати project.json, tools/generate_ui.py, tools/compile_scenario.py, glob modules/\*/manifest.json + drivers/\*/manifest.json у CMAKE_CONFIGURE_DEPENDS. *1 файл.*
2. **Ожити modules.cmake**: include у main/CMakeLists.txt → `${PRODUCT_MODULES}`; видалити ручний список; прибрати «крок 3» з CLAUDE.md/writing-a-module.md. *3 файли.*
3. **COMPONENTS-фільтр** з того ж PRODUCT_MODULES у root CMakeLists. *1 файл.*
4. **Генерований Kconfig.boards** зі скану boards/\*/board.json (патерн драйверного Kconfig) + source з main/Kconfig.projbuild. *2 файли.*
5. **Kconfig-гейти за патерном modesp_ble**: CONFIG_MODESP_NET_ENABLE, CONFIG_MODESP_SCENARIO_ENABLE, CONFIG_MODESP_CLOUD_NONE + empty-component SRCS у modesp_net/scenario/mqtt/aws + #if у main.cpp/http_service.cpp. *~8 файлів.*
6. **Валідаційні quick-fixes у generate_ui.py**: persist+string → ERROR; len(key)>32 → ERROR; MAX_LOG_CHANNELS overflow → ERROR; folder==manifest-name → ERROR; прибрати фантом pwm_channel; warn на невідомі board.json-секції; PROJECT_VER із project.json. *2 файли.*
7. **Санітарія**: видалити DEFROST_*/ALARM_*/SETPOINT_CHANGED/MsgSetpointChanged, 'protection.'-префікс (тимчасово: QoS з майбутнього alarm-прапорця, поки — все QoS0 чесно), мертві MiniChart/doReset/CSS, modesp_json, redirect-headers; полагодити 5 мертвих посилань у CLAUDE.md. *~10 файлів.*

**Після Фази 1:** правка маніфесту гарантовано регенерує все; додати/видалити стандартний модуль без bind = тільки project.json; нова плата = папка boards/; мінімальний no-cloud/no-net/no-scenario продукт збирається з menuconfig.

### Фаза 2 — Структурні зміни (~4–6 тижнів)

Мета: нуль продуктових імен у framework-коді; equipment — не магія; display/audio — повноцінні драйвери.

1. **BaseModule::on_bind(DriverManager&, BindingTable&, HAL&)** — ModuleManager кличе між реєстрацією та init_all; equipment/display/player переходять на хук; усі per-module виклики зникають із main.cpp. *~6 файлів.*
2. **Datalogger як framework-сервіс**: ILogSource/route-registration інтерфейс у modesp_core/net; datalogger реєструється сам; drop include+PRIV_REQUIRES із modesp_net; API серіалізації — transport-neutral callback замість httpd_req_t. *~5 файлів.*
3. **EquipmentBase → components/modesp_equipment**; modules/equipment — demo-сабклас; генератор ключиться на provides_roles/requires-capability замість `== "equipment"` (усі 4 місця: FeatureResolver, V15, bindings-page — з агрегацією ролей з усіх модулів, RUNTIME_KEY_PREFIXES). *~8 файлів.*
4. **Discovery-реєстр**: register_discovery(type, scan_fn) у DriverRegistry, реєстрація в ds18b20_driver.cpp, generic /api/drivers/<type>/scan; drop ds18b20 із modesp_net. *~4 файли.*
5. **Display/audio у драйверний пайплайн**: DRIVER_COMPONENT_CATEGORIES += display/audio; MODESP_REGISTER_DISPLAY/AUDIO; код at7456e/amt630a/max98357a переїжджає в drivers/ (modesp_osd розчиняється); ручні реєстри/Kconfig/SRCS-списки видаляються; drivers/at7456e/manifest.json + spi_buses у board.json/HAL. *~15 файлів.*
6. **Manifest-driven MQTT**: HA-метадані + alarm/qos прапорці в mqtt-секції → генеровані gen::HA_ENTITIES[]/бітмап; TOPIC_ROOT із project.json. *~5 файлів.*
7. **EquipmentBase за маніфестом**: генерована requires-таблиця → bind по ролях, enforce optional/whitelist; ValueKind з provides.type; generic has_<type>_driver. *~6 файлів.*
8. **JSON-схеми**: module/driver/board/bindings/project_schema.json + jsonschema-валідація; узгодити 'features' спеку з кодом; вирізати або реалізувати мертві driver-settings/discovery поля. *~8 файлів.*
9. **ui.json-контракти для webui**: role.module + unit, requires_address/shareable у редакторі, channels-мапа для ChartWidget, таблиця подій (id/pair/kind/лейбли), driver-settings per-binding через generic-рендер; видалити NTC/OneWire-хардкоди. *~10 файлів.*
10. **ICloudService + CloudRegistry** за патерном драйверів; manifest-фрагменти network-карток замість if/else 'aws'. *~6 файлів.*

**Після Фази 2:** framework-компоненти не містять жодного імені модуля чи драйвера; новий модуль із залізом — без правки main.cpp; новий display/audio/discovery-драйвер — drop-in папка; webui-редактор працює для будь-якого модуля/шини.

### Фаза 3 — Nice-to-have (у міру потреби)

> **Статус 2026-07-04:** зроблено пункти 1, 2, 4, 6 (+ network/system-частина п.7).
> Лишилися 3, 5, і решта п.7 — див. «Що лишилось» наприкінці §5.

1. ✅ **BLE decoder/profile-реєстр**: Xiaomi/BTHome → drivers/ble_xiaomi_th (`adv_decoder.h`), iPixel+шрифт → drivers/ble_led_panel; транспорт `modesp_ble` знеособлено (`central_link.h`+`ICentralLink`); PanelModule через `IPanelPort` (resolve у on_bind), не singleton. *(коміти 540445a, dbdae8a)*
2. ✅ **Table-driven hardware-типи**: `BOARD_SECTION_TO_HW_TYPE` — єдине джерело правди, `VALID_HARDWARE_TYPES` derived; додано `spi_bus`/`pwm_output` (секції+схема). *Generic HAL-сховище лишилось per-type (кожен тип має свою resource-структуру); runtime SPI/PWM — HAL find_<type>() під конкретний драйвер.* *(коміт c2a3798)*
3. ⬜ **provides_actions у маніфестах модулів** → merge у KnownActionRegistry, авто-хеші; автоін'єкція mirror-ключів рецептів. **(лишилось)**
4. ✅ **Board-профілі**: `boards/<name>/sdkconfig.board`, застосовується CMake для активної плати; board-специфіка геть із sdkconfig.defaults.esp32s3. *(коміт a9e332a)*
5. ⬜ **Генерація в ${CMAKE_BINARY_DIR}/generated** + staging для LittleFS (паралельні build-dirs, чистий git). **(лишилось — генеровані файли досі в `generated/`)**
6. ✅ **i18n**: `source_lang` у project.json (default uk), runtime-читання /i18n/manifest.json уже було. *Структуровані ключі + генерація chrome-словників — частково.* *(коміт 20d9270)*
7. 🟡 **modesp_module_component()** хелпер (⬜) + network/system сторінки з даних (✅ `tools/system_pages.json`, коміт 7934d56) + host-тести через convention-discovery (⬜) + manifest 'assets' для медіа (⬜) + demo-модулі в examples/ (⬜).

---

## 5. Критерій готовності

Framework вважається universal, коли ВСІ пункти проходять без правки жодного файлу поза modules/<x>/, drivers/<x>/, boards/<x>/ і project.json:

> **Статус 2026-07-04** (Фази 1–2, Фаза 3 п.1/2/4/6, + Фаза 4 прибирання зчеплення): `[x]`
> перевірено (acid-тест / білд / grep / 379 host-тестів), `[~]` механізм є але лишився залишок.
> Деталі — «Що лишилось» нижче.

**Модулі**
- [x] `git rm -r modules/simple_thermo` + видалити зі `project.json` → `idf.py build` проходить, webui не має слідів модуля. *(auto-registration з project.json; підтверджено acid-тестом demo_meter)*
- [x] `git rm -r modules/panel modules/player modules/presence` + project.json → білд проходить. *(Ф2.1 on_bind прибрав per-module виклики з main.cpp)*
- [x] Створити modules/my_pump/ (manifest+CMake+C++ з on_bind) + один рядок у project.json → модуль реєструється, б'є драйвери, з'являється в UI/MQTT/datalogger. Нуль правок у main/. *(acid-тест demo_meter: `git status` = лише project.json + нова папка)*
- [x] `grep "equipment|simple_thermo|datalogger|panel|player|presence" components/ main/ webui/src/` → лише generic-слова. *(Фаза 4: OneWireDiscovery.svelte видалено, `equipment`-дефолт + `SHAREABLE_HW`/onewire-хардкод у BindingsEditor → manifest-driven `hw.shareable`/`requires_address` (коміти 369e070); `panel_text.h` перенесено в panel-модуль (9a49312, b573fc8). Лишок — generic категорії `IPanelPort`/`IAudioSink`/`EquipmentBase` (як `display`) + коментарі.)*

**Драйвери**
- [x] Створити drivers/my_sensor/ (manifest+фабрика+MODESP_REGISTER_SENSOR) → toggle у menuconfig, працює з bindings.json. Нуль інших правок — включно з display/audio/panel категорією та драйвером із discovery. *(acid-тест demo_out; ble_xiaomi_th/ble_led_panel — drop-in у Ф3.1)*
- [x] `git rm -r drivers/ds18b20` (+ прибрати з bindings) → framework збирається. *(Ф2.3 discovery-реєстр прибрав ds18b20 із modesp_net)*
- [x] pcf8574_input публікує bool; будь-який provides.type респектиться без правки C++. *(Ф2)*

**Плати / збірка**
- [x] `mkdir boards/my_board` + board.json + bindings.json (+ опц. sdkconfig.board) → плата в menuconfig, білд проходить. Нуль правок Kconfig. *(Kconfig.boards Ф1 + board-профіль Ф3.4; acid-тест demo_board)*
- [x] Правка будь-якого manifest.json + `idf.py build` (без reconfigure) → свіжі generated/* і ui.json. *(CMAKE_CONFIGURE_DEPENDS Ф1.1)*
- [x] Помилки в маніфесті (persist на string, ключ 33 символи, 7-й log-канал, опечатка поля, невідомий widget) → червоний білд. *(валідації Ф1.6 + JSON-схеми Ф2.8)*

**Опційність**
- [~] menuconfig: NET off + CLOUD none + SCENARIO off + BLE off → мінімальний offline-продукт. *(гейти підтверджено на місці: Kconfig-символи MODESP_NET/SCENARIO/BLE_ENABLE + empty-component SRCS у modesp_net/scenario/ble + 17 `#if`-guards у main.cpp; повний offline-білд — на мінімальній платі, бо stand_s3 обов'язково потребує BLE)*
- [~] Продукт англійською: source_lang в project.json, жоден український літерал не потрібен. *(механізм підтверджено: `source_lang: "en"` → i18n-маніфест `["en","de","pl"]` default `en`, коміт 20d9270; повністю англ. продукт ще потребує англомовних маніфестів)*
- [x] HA discovery / alarm QoS / chart-канали / event-лейбли — все з маніфестів, підтверджено на модулі (abs_test), якого не існувало на момент рефакторингу. *(Ф2.7 + Ф2.10)*

**Останній тест ("acid test")**
- [x] Порожній project.json + нова плата + один новий модуль + один новий драйвер → збираються, і `git status` показує зміни ЛИШЕ в нових папках та project.json. *(підтверджено: demo_board + demo_meter + demo_out → firmware modesp_demo_board_v1 зелений, нуль правок framework)*

---

## 6. Що лишилось (станом на 2026-07-04)

Universal-мету досягнуто й зверифіковано: framework-код product-clean (grep → лише
generic-слова), додати/прибрати модуль, драйвер чи плату можна без правок поза
`modules/<x>/`, `drivers/<x>/`, `boards/<x>/`, `project.json` (acid-тести), 379 host-тестів
зелені. Залишкове зчеплення (Фаза 4) прибрано. Лишилися лише **необов'язкові enhancement-и**,
що РОЗШИРЮЮТЬ framework (не universality-прогалини):

**Enhancement-и дорожньої карти (Фаза 3, за потреби):**
- **Ф3.3 — provides_actions**: модулі декларують дії в маніфесті → merge у KnownActionRegistry
  + авто-хеші + mirror-ключі рецептів. Сьогодні `ActionRegistry` (modesp_scenario) наповнює
  main.cpp — працює; це зручність для scenario-авторів. *Нова фіча, не universality-прогалина.*
- **manifest `assets` для медіа**: модуль декларує медіа (напр. `data/audio/2.mp3`), генератор
  стейджить у `data/`. Дасть player-модулю володіти своїм аудіо. *Нова фіча.*
- **Ф3.5 — генерація в `${CMAKE_BINARY_DIR}/generated`**: зараз генеровані лежать у `generated/`
  в дереві. **Свідомо відкладено:** ризик > користь — тягне committed `panel_font_data.h`
  (dev-time, TTF) + правку include-шляхів у багатьох компонентах заради косметики (чистіший git),
  без universality-виграшу. Робочий стан стабільний.
- **`modesp_module_component()` CMake-хелпер**: модулі мають тривіальний CMakeLists (без
  Kconfig-опційності драйверів), тож хелпер — churn заради ~1 рядка. *Пропущено як marginal.*
- **demo-модулі в `examples/`**: guide-и (`writing-a-module.md`/`writing-a-driver.md`) уже несуть
  повний покроковий шаблон + `simple_thermo` як живий demo. *Пропущено як дублікат.*
- ✅ **host-тести** — полагоджено (Фаза 4): `tools/run_host_tests.py` (PYTEST_DISABLE_PLUGIN_AUTOLOAD),
  379 passed / 65 skipped; 6 `test_cpp_host` (native C++ host-білд) — окрема тема, скіпаються за замовч.

**Перевірки, які варто прогнати на залізі / окремій конфізі:**
- Мінімальна offline-збірка на мінімальній платі (гейти підтверджено на місці — див. §5).
- ✅ **Рантайм BLE Ф3.1 — ПІДТВЕРДЖЕНО НА ЗАЛІЗІ (2026-07-04, stand_s3 N16R8).** Boot-лог:
  Half A — BTHome-декодер (у драйвері ble_xiaomi_th) декодує Xiaomi `T=24.64C RH=40.36% batt=95%`;
  Half B — `central-link target='LED_BLE_...'` → `Panel: panel backend resolved` (IPanelPort у on_bind)
  → `chr uuid=0xfa02/0xfa03` (generic chr_role по профільних UUID) → `central-link READY (write+notify)`
  → `ble_led_panel: panel show_text '08:41' (149-byte frame)` — панель крутить час/темп/вологість.
  Board-профіль Ф3.4 теж підтверджено (16MB flash + partitions_16mb + BLE з sdkconfig.board).