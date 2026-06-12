# ModESP Framework — Глибоке ревью (2026-06-11)

Обсяг: ядро (modesp_core/hal/services/scenario), модулі, драйвери, Python-генератор,
мережевий стек (net/mqtt/aws/OTA), тести/збірка/документація. Кожен finding верифіковано
перечитуванням коду; тестові сюїти реально запускались; генератор перезапускався з
побайтовим порівнянням виходу.

---

## КРИТИЧНІ (безпека / фізична безпека обладнання)

### S1. `POST /api/cloud` повністю без автентифікації — перехоплення cloud-ідентичності
`components/modesp_aws/src/aws_iot_service.cpp:795` (та GET :752)
Будь-хто в локальній мережі може перезаписати AWS endpoint, Thing name, **сертифікат і
приватний ключ пристрою**. На відміну від `POST /api/mqtt`, який гейтиться через
`HttpService::check_auth`, тут перевірки немає взагалі. Плюс ці хендлери досі шлють
`Access-Control-Allow-Origin: *` (видалено в http_service за AUDIT-039, але не тут).
**Фікс:** додати `check_auth()` у обидва хендлери, прибрати wildcard CORS.

### S2. OTA: дозволений `http://`, checksum опціональний, підпису образу немає — віддалений RCE
`components/modesp_services/src/ota_handler.cpp:82-87, 268-301`; досяжно з MQTT
(`mqtt_service.cpp:602-621`) і AWS Jobs (`aws_iot_service.cpp:438-444`).
URL з cloud-повідомлення передається в `esp_http_client` без перевірки схеми. Якщо
checksum порожній — верифікація просто пропускається з warning (:300). Підпису образу
немає; MITM, що переписує firmware, переписує і checksum (вони в одному повідомленні).
Додатково: перевірка project/board виконується тільки якщо перший read ≥ 0x70 байт
(:190) — chunked transfer її обходить; `char buf[4096]` на 8K стеку — для https
майже гарантований stack overflow (:157).
**Фікс:** тільки https + crt_bundle/pinned CA; checksum обов'язковий (fail, не warn);
Secure Boot v2 + підписані образи; буферизувати перші 0x100 байт до header-перевірок.

### S3. Дефолтні креденшіали `admin:modesp`, відновлюються після factory reset
`components/modesp_net/src/http_service.cpp:45-47, 1580-1581, 1598`
Зашиті в код; factory reset стирає NVS і повертає відомий пароль. Мінімум 4 символи,
порівняння не constant-time (:150).
**Фікс:** примусова зміна пароля при першому вході, constant-time compare.

### S4. Секрети у відкритому NVS: WiFi PSK, MQTT-пароль, **приватний ключ AWS**
`mqtt_service.cpp:36-48, 691-697`; `wifi_service.cpp:281-306`; `aws_iot_service.cpp:714-732`
Без flash/NVS encryption дамп флешу віддає все, включно з TLS-ключем пристрою.
**Фікс:** flash encryption + NVS encryption; задокументувати вимогу для production.

### S5. MQTT-команди `_set_mqtt_creds` / `_set_tenant` дозволяють брокеру переписати креденшіали й namespace пристрою
`mqtt_service.cpp:651-715` (wildcard підписка `…/cmd/+` :333-348)
Telemetry-plane переписує auth-plane без жодної автентифікації команди.
**Фікс:** прибрати віддалену мутацію креденшіалів або підписаний контрольний канал.

### F1. SAFE MODE не латчиться — виходи можуть знову увімкнутись через 1 тік після FATAL
`modules/equipment/src/equipment_base.cpp:229-238`
`on_message(SYSTEM_SAFE_MODE)` вимикає виходи один раз, але `on_update()` далі викликає
`apply_arbitration()` → продуктова логіка знову подасть `set_actuator(true)` на
наступному циклі. ErrorService припускає, що off-стан персистентний — це не так.
Плюс `publish()` синхронний у задачі-відправника → гонка по `roles_[].output_req`.
**Фікс:** латч-прапорець `safe_mode_` в EquipmentBase, який блокує виходи в `on_update`
до явного скидання.

### F2. PCF8574 relay: спільний `output_state` не відкочується при невдалому I2C-записі — фантомне вмикання реле
`drivers/pcf8574_relay\src\pcf8574_relay_driver.cpp:46-69` (і `emergency_stop()` :71-76)
Біт мутується ДО запису; при фейлі біт лишається. Наступний успішний запис будь-якого
іншого реле на тому ж експандері (KC868-A6: compressor+fan на одному PCF) фізично
вмикає компресор, про який софт думає, що він OFF. `is_healthy()` завжди true.
**Фікс:** snapshot/restore біта при фейлі; лічильник помилок у `is_healthy()`.

### F3. DriverManager: переповнення `etl::vector` при >8 сенсорів у bindings.json
`components/modesp_hal/src/driver_manager.cpp:79, 93` (ліміти `hal_types.h:32-33`)
`push_back` без `full()`-перевірки; MAX_SENSORS=8, але MAX_BINDINGS=24 і пулів на 32.
Звичайна конфігурація з 9 сенсорами → запис за межі → пошкодження сусідніх членів.
**Фікс:** перевірка `full()` + reject із логом; узгодити ліміти.

---

## ВИСОКІ

### H1. ResourceArbiter: OOB-запис у `inserted[MAX_RESOURCES]` при >32 декларацій з дублікатами хешів
`components/modesp_scenario/src/arbiter/resource_arbiter.cpp:60-83, 120-143`
Loader не обмежує `resource_count` ≤ MAX_RESOURCES і не реджектить дублікати → stack smash.
**Фікс:** валідація в modr_loader (count ≤ 32, унікальні хеші).

### H2. Відсутній threading contract: message bus, ErrorService, LoggerService, Engine API
- `error_service.h:37` каже «з будь-якого потоку», але `report()` несинхронізований і
  синхронно диспатчить у `on_message()` всіх модулів у задачі-викликача (`error_service.cpp:43-93`).
- `ModuleManager::publish()` = голий `bus_.receive()` (`module_manager.cpp:150-152`).
- `Engine` пропонує «Direct registry access for HTTP handler diagnostics» без локів
  (`engine.h:126-140`).
**Фікс:** черга для cross-task report/publish + явний контракт «main-task only» з configASSERT.

### H3. DS18B20: блокуючий retry (`vTaskDelay(50)`) всередині 100 Hz main loop під bus mutex
`drivers/ds18b20/src/ds18b20_driver.cpp:151-154, 697-709`
Worst case ~135 мс стоп усіх модулів і драйверів; навіть успішне читання ~12 мс > 10 мс тіку.
**Фікс:** stateful retry на наступному тіку або окрема low-prio задача для OneWire.

### H4. DataLogger: гонка HTTP-стрімінг ↔ main loop (буфери + файли)
`modules/datalogger/src/datalogger_module.cpp:356-506` vs `:174-220, 270-334`
(виклик з `http_service.cpp:1408,1421`)
HTTP-задача ітерує `temp_buf_`/`event_buf_` і читає файли, поки main loop робить
`push_back`/`clear`/`remove+rename`. Порвані записи, дублікати, фейли ротації.
**Фікс:** м'ютекс/снепшот навколо буферів; блокувати ротацію під час серіалізації.

### H5. DataLogger мовчки втрачає семпли при sample_interval < 37.5 с
`datalogger_module.cpp:199-204` — буфер 16 записів, flush раз на 10 хв, маніфест дозволяє 30 с
→ 20 семплів за вікно, 4 губляться без логу.
**Фікс:** flush при заповненні буфера (один рядок) або min 40 с у маніфесті.

### H6. `compile_scenario.py` не підключений до збірки — найбільша діра в "single source of truth"
`CMakeLists.txt:28-43` запускає лише `generate_ui.py`. Зміна `scenario` в маніфесті →
`idf.py build` флешить **старий** `.modr`. Плюс компілює всі `modules/*` ігноруючи
`project.json` — `abs_test.modr` зараз у flash-образі.
**Фікс:** `execute_process` для `compile_scenario.py --strict` + фільтр за project.json.

### H7. bindings.json/board.json взагалі не валідуються генератором
`tools/generate_ui.py:1770-1780, 624-630`
Живий приклад: `data/bindings.json` біндить ds18b20 без `address`, хоча маніфест драйвера
вимагає `requires_address: true` (драйвер при boot жорстко фейлить init — H8).
**Фікс:** BindingsValidator (role ∈ requires, driver дозволений, hardware існує, address є).

### H8. Шаблон dev-плати порушує вимогу самого драйвера: ds18b20 без address → `air_temp` мертвий з коробки
`boards/dev/bindings.json:4` vs `ds18b20_driver.cpp:98-103`.

### H9. Тести: 144 із 470 Python-тестів зламані (фікстури на видалені thermostat/defrost/protection); host-тести компілюють **застарілий форк** SharedState
- `tools/tests/test_modules.py`, `test_features.py`, `test_kc868a6.py`, `test_sequence_host.py:27`.
- `tests/host/shared_state_host.cpp:56` — без `track_change`-гейта (BUG-017) і з логом під
  м'ютексом (ABBA, який реальний код навмисно уникає). Регресія в реальному shared_state.cpp
  не буде спіймана.
- CI відсутній (`.github/workflows` немає) — тому все це й зогнило.
**Фікс:** компілювати реальні `components/modesp_core/src/*.cpp` у host-білді; переписати
фікстури на актуальні модулі; додати GitHub Actions (host tests + pytest + регенерація).

### H10. CLAUDE.md: таблиця документації посилається на неіснуючий `docs/` (реальні доки в `documentation/en|uk/`); сам CLAUDE.md у .gitignore

### H11. NTC: конвенція атенюації зламана — сирі dB кастяться в `adc_atten_t`
`drivers/ntc/src/ntc_driver.cpp:75`; `boards/dev/board.json` передає 11 (невалідний enum →
init fail), kc868a6 передає 3 (enum). Дві плати — дві протилежні конвенції.

---

## СЕРЕДНІ (вибрані, повний список у звітах агентів)

**Маніфест "бреше" — налаштування-фікції, які UI показує, а код не читає:**
- equipment: `ntc_beta/r_series/r_nominal/ds18b20_offset` — `apply_sensor_config()`
  оголошений, але ніде не визначений і не викликається (`equipment_base.h:147`).
- ds18b20: `read_interval_ms` (hardcoded 1000), `offset`, `resolution` — не підключені;
  `parasitic` у scan — не реалізований.
- ntc: `beta/r_series/r_nominal/offset/read_interval_ms` — configure() їх не приймає.
- digital_input: `invert` (NC-контакти!) — недосяжний.
- datalogger: `log_evap/log_cond/log_setpoint/log_humidity` — мертві тумблери.
- equipment: маніфест публікує `sensor1_ok`, код публікує `air_temp_ok` — індикатор
  здоров'я сенсора в UI порожній назавжди.
**Це системно підриває довіру до manifest-driven моделі — або підключити, або вирізати.**

**Scenario engine:**
- Phase timeout недосяжний поки action повертає PENDING (`track.cpp:264-342`) — для
  промислового контролера safety-timeout має домінувати.
- `scenario_timeout_max_ms` оголошений і ніде не enforced (`modr_format.h:188`).
- ALL_TRACKS: фейл не-main треку → вічний RUNNING, ресурси арбітра не звільняються
  (`instance.cpp:173-202`).
- .modr: офсети перевіряються на range, але не на alignment → LoadStoreAlignment panic
  на пошкодженому файлі (CRC рахується по тих самих байтах) (`modr_loader.cpp`).
- Експоненційний blow-up у валідації композитних умов — CPU DoS 4-кілобайтним файлом
  (`modr_loader.cpp:227-240`).
- NVS-записи синхронно в 100 Hz тіку (`nvs_observer.cpp:30-46`) — стоп loop на 10-100+ мс.

**HAL/драйвери:**
- I2C freq з board.json парситься і ігнорується, девайси hardcoded 100 kHz (`hal.cpp:256-309`).
- `consecutive_errors_` uint8_t wrap → відключений сенсор кожні ~4.3 хв «одужує» на 5 циклів.
- PCF8574 input: помилки I2C ковтаються, stale-стан назавжди, is_healthy завжди true.
- PCF8574 таймаут 100 мс × 6 щотікових читань — до ~600 мс стопу при підвислій шині.
- NTC: raw/4095 без adc_cali — систематична похибка кілька °C на краях діапазону.

**Генератор:**
- Документоване правило `mqtt_subscribe`/`access==readwrite` НЕ enforced.
- Event ID: нема range-check 0-255, нема резервування системних 7/10 і `id+1` для BOTH.
- `datetime.now()` у ui.json → кожен білд бруднить git diff (єдине джерело недетермінізму).
- Stale-файли: видалення останнього loggable лишає старі headers у збірці.
- `visible_when` на неіснуючі ключі (datalogger → `equipment.has_evap_temp` тощо) — шипиться сьогодні.
- Валідація datalogger після запису 6 файлів — фейл лишає mixed-стан generated/.

**Збірка/гігієна:**
- `recipe_smoke` у production project.json; `abs_test.modr` у flash-образі.
- sdkconfig розійшовся з defaults (WIFI_IRAM_OPT, mbedTLS buffers) — задокументовані
  RAM-оптимізації неактивні в локальному білді; -Og, OTA-слот заповнений на 81.5%.
- 4 мертві ps1-скрипти з хардкодом `D:\ModESP_v4` (старий шлях).
- writing-a-module.md пропускає обов'язковий крок `main/CMakeLists.txt PRIV_REQUIRES`.
- testing.md описує бінарі тестів, яких не існує.

**Мережа:**
- MQTT за замовчуванням cleartext :1883 без TLS; mqtts без пінінгу брокера.
- AWS Jobs: dispatch по `strstr` без перевірки thing-namespace; плоский скан JSON ловить
  `url` будь-де в документі.
- MQTT/Shadow серіалізація StringValue без JSON-екранування (HTTP/WS екранують).
- `POST /api/cloud`: одне `httpd_req_recv` у 4K буфер — PEM cert+key мовчки обрізається.
- Неавтентифіковані GET: /api/state, /api/bindings, /api/log*, /api/onewire/scan.
- malloc у WS hot path (AsyncSendCtx + 6K буфер на кожен broadcast кожні 1.5 с).

---

## Що ЧИСТЕ (верифіковано)

- **Zero-heap у hot path** — витримано практично всюди (ETL, статичні пули, stack snprintf);
  винятки: stdio у datalogger flush (раз на 10 хв), malloc у WS send, factories continuous
  primitives (поки не підключені).
- **GPIO тільки в HAL** — жоден модуль не чіпає gpio_* напряму.
- **Naming convention** — всі модулі відповідають правилам.
- **generated/ ↔ маніфести** — байт-у-байт без дрифту (крім timestamp); ручних правок немає.
- **partitions.csv** — влазить у 4MB з запасом 64K, OTA rollback увімкнений.
- **git-гігієна** — артефакти не закомічені, .gitignore ретельний.
- OneWire: таймінги по datasheet, CRC8 коректний, SEARCH_ROM по AN187; relay safe-state
  ordering правильний; знакове розширення від'ємних температур коректне.
- Тести, що відповідають коду, — якісні й поведінкові (46 doctest кейсів зелені,
  104 тести компілятора сценаріїв зелені).

---

## Рекомендований порядок виправлень

**Тиждень 1 — безпека людей і пристрою:**
1. F1 латч SAFE MODE; F2 відкат біта PCF8574; F3 full()-перевірка DriverManager; H1 валідація арбітра.
2. S1 auth на /api/cloud; S2 https-only OTA + обов'язковий checksum; S3 примусова зміна пароля.

**Тиждень 2 — real-time і дані:**
3. H3 прибрати блокуючий retry DS18B20; H4 м'ютекс DataLogger; H5 flush при full().
4. H2 threading contract (черга для ErrorService/publish).

**Тиждень 3 — pipeline і довіра до маніфестів:**
5. H6 compile_scenario у CMake; H7 валідація bindings; прибрати timestamp з ui.json.
6. Підключити або вирізати всі мертві manifest-налаштування (M-блок).
7. H9 полагодити тести + CI; H10 виправити CLAUDE.md.

---

*Звіт згенеровано шістьма паралельними ревью-агентами 2026-06-11; кожен finding
підтверджений перечитуванням коду, тести виконувались, генератор перезапускався.*
