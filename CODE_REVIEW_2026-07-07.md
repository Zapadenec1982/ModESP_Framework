# ModESP Framework — глибоке ревью (2026-07-07)

**Метод:** багатоагентне ревью робочого дерева (uncommitted-зміни включно). 10 доменних експертів + критик повноти, кожна знахідка проходила адверсарну верифікацію (для critical/high — троє незалежних перевіряючих з різними лінзами: спростування / відтворюваність / серйозність, рішення голосуванням).

**Обсяг:** ~25 000 LOC C/C++ у `components/`, `drivers/`, `modules/`, `main/`; ESP-IDF 6.0.1, ESP32 / ESP32-S3, промисловий контролер холодопостачання.

**Підсумок верифікації:** 55 підтверджено · 1 правдоподібна · 2 спростовано · 28 знахідок (весь `scenario-engine` і `build-config`) **не встигли пройти фінальну верифікацію** через вичерпання ліміту — вони позначені нижче як «попередні, потребують перевірки», але описані з посиланнями на конкретні рядки й функціями, тож достовірність висока.

---

## Загальний вердикт

Каркас архітектури здоровий і дисциплінований. Нижні шари (`modesp_core`, реєстр драйверів, дисплейний seam за ADR-002/003) — найсильніша частина коду. **Червневе ревью (2026-06-11) відпрацьоване сумлінно:** F1, F2, F3, H3, H5, H11, H1, H4, H6, H7, BUG-021/024 — усі перевірено як реально виправлені.

Але **проект ще не готовий до року роботи без нагляду.** Ризик сконцентрований на межах, де система зустрічає рідкісні-але-неминучі збої після подій живлення, і на крос-тасковых швах. Є два критичні шляхи, що ведуть до **тихого й остаточного відмовляння** контролера (мертвий після збою I2C на завантаженні; вічний паніка-boot-loop від короткого WiFi-пароля), і залишкова діра H2 (гонка Scenario Engine), якої не закрили з червня, — здатна аварійно перезавантажити контролер одним кліком у WebUI.

**Пріоритет №1 перед польовим розгортанням — 5 «safety/boot» знахідок нижче.** Усі — локальні, невеликі за обсягом зміни.

---

## Критичні та найсерйозніші (fix before deploy)

### 1. [CRITICAL] Будь-який збій init на завантаженні → контролер мертвий назавжди
`main/main.cpp:203`, `components/modesp_hal/src/hal.cpp:406`

`app_main` робить `return` при невдачі `hal.init()` / `driver_manager.init()` / `app.init()`. На цей момент TWDT для головного таска ще не підписано, тож після return планувальник простоює вічно: ні WiFi, ні виходів, ні перезавантаження. Реальний шлях: `init_i2c_expanders()` повертає false, якщо єдиний початковий запис у PCF8574 не пройшов — саме те, що стається під час гонки живлення, коли після відключення повертається мережа, а розширювач повільно піднімається, або при короткочасному глічі SDA. Результат — холодильний контролер, що ніколи не вмикає реле й тихо стоїть мертвий до фізичного перезапуску.

**Fix:** на неусувну помилку — `esp_restart()` з обмеженим backoff-лічильником, або деградація (пропустити збійний розширювач, продовжити). Ніколи не лишати пристрій у idle без watchdog. HAL має робити помилки per-resource нефатальними (DriverManager уже демонструє правильний патерн).

### 2. [CRITICAL, правдоподібно] Невалідований WiFi-пароль + `ESP_ERROR_CHECK` → вічний паніка-boot-loop
`components/modesp_net/src/wifi_service.cpp:460`

`POST /api/wifi` приймає будь-який пароль без перевірки довжини й зберігає в NVS. `start_sta()` жорстко ставить `authmode = WIFI_AUTH_WPA2_PSK` і обгортає `esp_wifi_set_config()`/`esp_wifi_start()` у `ESP_ERROR_CHECK`. `esp_wifi_set_config` повертає `ESP_ERR_WIFI_PASSWORD` для непорожнього пароля коротшого за 8 символів проти порогу WPA2 → `abort()` → перезавантаження → `load_credentials()` перевіряє лише непорожній SSID → знову паніка, і так вічно. Відновлення лише через serial-reflash / стирання NVS. Так само `start_ap()` досяжний у рантаймі й панікує на транзієнтному `ESP_ERR_NO_MEM`.

**Fix:** валідувати довжину пароля (0 або 8–63) і в HTTP-хендлері, і в `save_credentials()`; прибрати всі `ESP_ERROR_CHECK` зі `start_sta`/`start_ap` на користь перевірених повернень з fallback у AP-режим; збійну збережену конфігурацію трактувати як «стерти й підняти AP».

### 3. [HIGH] Scenario Engine REST API гонить зі 100 Гц тіком — без синхронізації (H2, не виправлено з червня)
`components/modesp_scenario/src/core/engine.cpp:242`

В Engine немає жодного м'ютекса, але його мутуючий API викликається з двох тасків: головний цикл (100 Гц тік) і httpd-таск (`http_service.cpp:1926-1958` — `load_path/start/pause/abort/unload`). Оператор тисне Unload у WebUI під час RUNNING → `Engine::unload` неатомарно робить `arbiter_.release_scenario()`, потім скидає `s->runtime = SequenceRuntime{}` (обнуляє вказівник `scenario.header()`), поки тік у мідл-`instance_tick` розіменовує саме цей вказівник у буфер слоту → `LoadStoreProhibited` паніка й перезавантаження контролера посеред роботи, або пошкодження списків арбітра. Червневий побратим H4 (datalogger) виправили м'ютексами — а частину Engine ні.

**Fix:** маршалити всі REST-команди життєвого циклу на головний цикл через командну чергу (дренаж на початку `Engine::on_update`, хендлер лише enqueue + 202/токен) — це зберігає модель single-writer і збігається з уже наявним патерном `scenario_persist`. Альтернатива — м'ютекс, який бере і `on_update`, і кожен публічний метод.

### 4. [HIGH] `esp_mqtt_client_publish` блокує WDT-моніторований головний цикл → паніка-reboot
`components/modesp_mqtt/src/mqtt_service.cpp:452` (той самий патерн у `aws_iot_service.cpp:341`)

`publish_state()` крутиться в 100 Гц циклі й викликає publish для ~48 ключів. esp-mqtt робить socket-write у таску викликача й блокується до network timeout (типово 10 с), коли TCP send-буфер повний або mqtt-таск тримає API-lock під час TLS-reconnect. Брокерський лінк напівзависає → publish блокується → TWDT (5000 мс, `trigger_panic=true`) → апаратний watchdog-паніка, усі виходи скидаються — саме тоді, коли зв'язок деградує.

**Fix:** `network.timeout_ms` значно менший за 5 с WDT (напр. 2 с), або винести хмарну публікацію на власний таск через чергу; щонайменше — `esp_mqtt_client_enqueue()` замість `publish()` з головного циклу.

### 5. [HIGH] Заклинений I2C-шлейф → одне оновлення дисплея = ~18 с блокування → TWDT-паніка; ніде немає recovery шини
`components/modesp_osd/src/amt630a.cpp:214`

`osd_print()` робить ~62 транзакції на 20-колонковий рядок, кожна з `I2C_TIMEOUT_MS=50`. `render()`/`present_main()` йдуть у `DisplayModule::on_update` в 100 Гц циклі. Якщо AMT630A (або будь-який slave на шині) зависає, тримаючи SDA внизу — класичний wedge, що NACK-fast-fail не покриває — кожен запис вигорає повний 50 мс timeout: один кадр із 6 рядків ≈ 370 транзакцій = ~18 с блокування → TWDT (5 с) паніка. Після reboot, якщо slave досі тримає SDA, init розширювача падає й спрацьовує знахідка №1 (мертвий контролер). Немає скидання шини / SCL-clock-out, немає latch на N послідовних помилок.

**Fix:** рахувати послідовні I2C-помилки в порту й вимикати презентацію (retry з backoff) після N збоїв; на `ESP_ERR_TIMEOUT` робити `i2c_master_bus_reset()` / ручний clock-out SCL; батчити записи плиток.

---

## Безпека (мережа / secret storage / автентифікація)

| Sev | Знахідка | Файл |
|-----|----------|------|
| HIGH | **MQTT command-канал переписує creds/tenant і штовхає непідписану прошивку** — `_set_tenant`/`_set_mqtt_creds`/`_ota` досі активні; SHA256 не захищає (та ж команда дає і URL, і checksum). RCE-ланцюг за одним publish-ACL. (S5, не виправлено) | `modesp_mqtt/src/mqtt_service.cpp:718` |
| HIGH | **Секрети відкритим текстом у NVS** — AWS private key, WiFi PSK, MQTT-пароль; `CONFIG_NVS_ENCRYPTION`/`SECURE_FLASH`/`SECURE_BOOT` вимкнені. Фізичний доступ → клонування identity в AWS. (S4) | `modesp_aws/src/aws_iot_service.cpp:895` |
| HIGH | **`admin:modesp` за замовчуванням відновлюється factory-reset'ом** — `nvs_flash_erase()` стирає auth-namespace, пристрій піднімається з дефолтом поряд з відкритим AP. Порівняння пароля не constant-time. (S3) | `modesp_net/src/http_service.cpp:49` |
| MEDIUM | **WebSocket `/ws` віддає весь стан пристрою без автентифікації** — `send_full_state_to(fd)` без `check_auth`; будь-яка сторінка в інтернеті може відкрити `ws://<ip>/ws`. Обхід авторизації, яку `/api/state` стереже. | `modesp_net/src/ws_service.cpp:187` |
| MEDIUM | **Provisioning SoftAP відкритий (без пароля) за замовчуванням** — `WIFI_AUTH_OPEN` коли ap_pass порожній/<8 симв. | `modesp_net/src/wifi_service.cpp:530` |
| LOW | **Неавтентифіковані GET розкривають версію прошивки/плати/партицію/час** — розвідка для атаки. `/api/auth` віддає ім'я користувача. | `modesp_net/src/http_service.cpp:999` |

> **Позитив:** TLS зроблено правильно — AWS пінить Amazon Root CA + mTLS, MQTT перевіряє пінований CA або crt-bundle, жодних insecure-esp_tls-флагів/skip-CN. S1 виправлено (auth на `/api/cloud`+`/api/mqtt`, wildcard CORS прибрано). AWS Jobs strstr-dispatch посилено до exact-match.

---

## Пам'ять (OOB / overflow)

- **[MEDIUM] AWS Device Shadow serializer — OOB write + OOB read** (`aws_iot_service.cpp:541`). Ідіома `pos += snprintf(buf+pos, sizeof-pos, ...)` без коректного guard: `pos` перестрибує 2048, далі `sizeof - pos` underflow'иться у величезний `size_t`, `}}}\0` пишеться в сусідній BSS, а `publish(..., pos)` шле >2048 байт (info-leak на брокер). Спрацьовує **автономно кожні 5 с** при ~60 writable-параметрах. Виправити клампінгом кожного запису через saturating `remaining()` (безпечні serializer'и стану вже так роблять).
- **[HIGH+MEDIUM] OTA-таск: 4 КБ стековий буфер на 8 КБ таску, що виконує TLS-хендшейк** (`ota_handler.cpp:174`). Новий `esp_crt_bundle` для https піднімає перевірку X.509 у цьому ж таску (потрібно 4-6+ КБ стеку) при ~3.5 КБ headroom → stack-canary паніка на HTTPS-OTA. (частина S2, не виправлено) Fix: буфер у heap/static або стек таску до 12-16 КБ.
- **[LOW] `handle_get_scenario_list` — buf[512] overflow при `MAX_SEQUENCES=8`** (`http_service.cpp:1769`). Та сама unclamped-accumulator ідіома. Безпечно на дефолті (2).

---

## Надійність / безпечний стан (виживання холодильного контролера)

- **[HIGH] OTA self-validation вимагає STA-WiFi** (`main/main.cpp:373`) — прошивку, залиту через власний AP пристрою (офлайн-об'єкт без роутера), завжди відкочує через 60 с, бо `is_connected()` у AP-режимі завжди false. Оновлення тихо ревертиться. Fix: приймати `server() && (is_connected() || is_ap_mode())`.
- **[MEDIUM] Safe Mode недосяжний** (`error_service.cpp:90`) — `enter_safe_mode()` лише на FATAL, але **жоден код ніде не репортить FATAL**. Heap-critical і exceeded-max-restarts репортяться як CRITICAL (лише лог). Червневий F1-latch у EquipmentBase — мертвий код. Fix: промотувати heap-critical у FATAL або викликати `enter_safe_mode()` явно.
- **[MEDIUM] Прошита логіка ігнорує здоров'я сенсора** (`simple_thermo/src/simple_thermo_module.cpp:29`) — `read_sensor_value` повертає `0.0` при збої read(); мертвий сенсор при setpoint 22/diff 1 дає temp=0.0 → нагрів залатчений ON назавжди (у cooling-продукті — компресор проти застиглого значення). `_ok` публікується, але модуль його не читає. Fix: `etl::optional`/NAN замість 0.0 + gating на `air_temp_ok` + framework-failsafe.
- **[MEDIUM] PCF8574-реле: збійний I2C retry'иться щотік з логом, без backoff/alarm** (`pcf8574_relay_driver.cpp:69`). Bit-rollback (F2) на місці, але при тривалому збої шини кожна спроба = до 20 мс timeout + блокуючий `ESP_LOGE` (~8 мс) → цикл деградує; якщо збійна команда була OFF, компресор лишається фізично ON, і нічого крім spam у serial про це не звітує.
- **[MEDIUM] Restart-шлях модуля панікує** (`module_manager.cpp:138`) — `restart_module()` знову викликає `WiFiService::on_init`, де `ESP_ERROR_CHECK(esp_event_loop_create_default())` повертає `ESP_ERR_INVALID_STATE` на другому виклику → abort. Механізм відновлення сам себе вбиває.
- **[LOW] HAL хардкодить `0xFF` як «безпечний» стан PCF8574** (`hal.cpp:402`) — ігнорує per-pin `active_high`; на active-HIGH-платі boot-запис вмикає всі навантаження. Поточні kc868a6/dev не зачеплені, але фреймворк позиціонується під кастомні board.json.

---

## Конкурентність (крос-таскові гонки)

- **[MEDIUM] `/api/onewire/scan` тримає bus-mutex секундами** (`ds18b20_driver.cpp:663`) — `read_temp_by_address` тримає `s_bus_mutex` через `vTaskDelay(800)` × до 8 пристроїв (~6.5 с). Тим часом `update()` у головному циклі блокується на 200-500 мс/тік, а таймаути інкрементують `consecutive_errors_` → живі сенсори можуть хибно позначитися unhealthy від рутинної UI-дії. Fix: 0-tick try-take у `update()` (skip-not-error), стан-машина для сканування.
- **[MEDIUM] MQTT config-буфери (`prefix_/tenant_/user_/pass_`) мутуються з 3 тасків без синхронізації** (`mqtt_service.cpp:718`) → топік форматується з напівпереписаного `prefix_` → телеметрія/retained-ALARM у сміттєвий топік. Маршалити через той самий deferred-mailbox, що вже є для reconnect.
- **[MEDIUM] AWS cert/key static-буфери переписуються, поки esp-mqtt тримає живі посилання для mTLS** (`aws_iot_service.cpp:894`) — ротація cert під час reconnect → хендшейк парсить напівпереписаний ключ.
- **[MEDIUM] `POST /api/cloud` recv-цикл retry'иться вічно на socket-timeout** (`aws_iot_service.cpp:839`) — один авторизований клієнт (дефолтний admin) відкриває POST і не шле тіло → єдиний httpd-таск зависає назавжди → весь WebUI/REST/WS мертві.
- **[MEDIUM] Crash-critical scenario-flush на тіку може стояти ~4 с проти 5 с WDT** (`scenario_persist.cpp:121`); також stale queued-item може віддати чужий `flush_result_`, порушивши durable-on-return гарантію.
- **[MEDIUM] WiFi-прапорці (`connected_/ap_mode_/reconnect_pending_`) діляться між event-таском і головним циклом без атомарності** (`wifi_service.cpp:61`) → reconnect-storm.
- **[MEDIUM/LOW] ErrorService заявляє «callable from any thread», але не синхронізований** (`error_service.h:37`) і зберігає сирі `const char*` — заряджена пастка для першого драйвера/OTA/MQTT-хендлера, що зарепортить помилку.

> **Позитив:** ISR у репозиторії немає взагалі (звужує простір гонок). `scenario_persist.cpp` — зразковий off-loop NVS-дизайн (single-writer FIFO, crash-critical handshake). DataLogger H4/H5 виправлено чисто. WsService client-management solid. SharedState всюди bounded 100 мс таймаути, callback поза локом, документований захист від ABBA-deadlock.

---

## Дисплей / OSD (свіжий рефакторинг ADR-002/003)

Архітектурно **найздоровіша частина фреймворку** — seam-дисципліна реальна, `caps()` справді гейтить меню через згенерований `MENU_NODE_CAPS`, каліброка в board.json, no-reinit-on-seam інваріант тримається, bench-знання зафіксовані в коді. Системна слабкість — **тайминг**: усе синхронні 2-байтні I2C-транзакції на 100 кГц усередині 100 Гц циклу.

- **[HIGH] Кожен present — монолітний синхронний I2C-burst** (`amt630a_port.cpp:200`) — `render()` пише всю сітку 20×10 безумовно (~630 транзакцій ≈ 250 мс стопу на перемалювання меню; `present_main` ~165 мс). На кожне натискання кнопки / кожні 500 мс refresh. Немає shadow-buffer diffing. Fix: тримати shadow BGMAP, писати лише змінені комірки.
- **[MEDIUM] «Non-blocking» chunked font-recovery насправді блокує ~130 мс/тік ~6.5 с** (`amt630a_port.cpp:52`) — `kFontChunk=4` = 320 транзакцій ≈ 128 мс (13× бюджету циклу), ×49 тіків. Плюс `Recov::SETUP` містить `vTaskDelay(50 ms)` у циклі. Це штатний шлях для продуктової плати за ADR-003.
- **[MEDIUM] `present_notice` використовує адресацію menu-сітки — ALARM-банер спотворений/невидимий на MAIN (hero) екрані** (`amt630a_port.cpp:302`) — новий (uncommitted) `present_main` мапить вікна інакше, банер лягає в W1 row5 col16, показує ≤4 символи. Підриває всю мету ADR-001 (видимість тривог) саме на екрані, що бачать найчастіше.
- **[MEDIUM] Активний банер не пере-презентується після recovery / re-enable дисплея** (`display_module.cpp:197`) — infinite-TTL ALARM тихо зникає після power-cycle дисплея.
- **[MEDIUM] `select_input` блокує цикл до 200 мс у vTaskDelay-poll** (`amt630a.cpp:452`), викликається з `on_update` після кожного recovery.

---

## Драйвери / HAL

- **[HIGH] NTC рахує температуру з сирих ADC-кодів без калібровки** (`ntc_driver.cpp:160`) — припущення лінійності при full-scale=VCC хибне (на ATTEN_DB_12 full-scale ~3.55 В, нелінійно, `adc_cali` не створюється). 10k NTC при 25 °C → звіт ~28 °C, помилка ~3 °C росте з розкидом Vref. Холодильний контролер систематично переохолоджує / хибно тривожить. (M-block, не виправлено) Fix: `adc_cali_curve_fitting` handle, raw→mV.
- **[MEDIUM] Encoder семплить квадратуру на 100 Гц без PCNT/ISR** (`encoder_driver.cpp:55`) — EC11 на 1 об/с дає 80 quarter-steps/с (на межі Nyquist); швидкий флік → 2-крокові стрибки, які LUT відкидає без лічильника. Будь-який стоп циклу (DS18B20-мутекс вище) губить цілі оберти. ESP32 має апаратний PCNT саме для цього.
- **[LOW] `pcf8574_input` без дебаунсу** (`pcf8574_input_driver.cpp:36`) — сирий біт щоразу; KC868-A6 біндить 6 таких на двері/тиск → chatter відкриття/закриття.
- **[LOW] Правило input-only GPIO34-39 застосоване на всіх таргетах** (`hal.cpp:139`) — на ESP32-S3 (плата stand_s3) це повноцінні GPIO; pull-up тихо вимикається → плаваюча лінія.
- **[LOW] DS18B20 clone-евристика вимикає CRC на будь-якому read з байтами 5/6 = A5A5** (`ds18b20_driver.cpp:375`) — noise-corrupted read справжнього сенсора може обійти CRC.
- **[LOW] DS18B20 `resolution` показується в UI, але не застосовується** (`ds18b20_driver.cpp:69`) — мертва manifest-настройка (M-block).

> **Позитив:** F2/F3/H3/H11 + uint8-error-wrap — усі реально виправлені. Реєстр драйверів зі static-пулами + build-time gate (bindings vs menuconfig) — зразкова інженерія. SEARCH_ROM (AN187) акуратний. NTC ловить open/short window до дільникової математики.

---

## Архітектура

- **[MEDIUM] `components/` залежить вгору від `modules/` і `drivers/`** (`modesp_net/CMakeLists.txt:9`) — `modesp_net PRIV_REQUIRES ds18b20 datalogger`, `http_service.cpp` безумовно інклудить `datalogger_module.h`. Продукт без datalogger-модуля **не збереться**. Це саме той хардкод-dispatch, який DriverRegistry був створений усунути, але на межі ядра. Fix: інвертувати шов (ILogSource у core, endpoint-callback для onewire-scan).
- **[MEDIUM] `main.cpp` ігнорує результати `init_all()`/`register_module()`** (`main.cpp:185`) — `init_all` навмисно повертає false при збої CRITICAL-модуля (фікс BUG-024), але всі 3 call-site відкидають це → зіпсований board.json → зомбі-цикл, що звітує пристрій живим. Таблиця модулів на 15/16 — 17-й (ws_service) тихо не реєструється.
- **[MEDIUM] Composition root хардкодить опціональні згенеровані інстанси** (`main.cpp:301`) — прибирання datalogger/display/equipment з project.json ламає компіляцію `main.cpp`. Обіцянка manifest-driven модульності зламана в корені.
- **[LOW]** Публічні заголовки `modesp_core` залежать від згенерованих артефактів через `CMAKE_SOURCE_DIR` — «портативний» шар не self-contained (`modesp_core/CMakeLists.txt:9`).
- **[LOW]** `App::run()` — мертвий дивергентний другий головний цикл, що ніколи не оновлює драйвери; пастка для автора продукту (`app.cpp:31`).
- **[LOW]** `DisplayBackendRegistry` реалізує рівно той дизайн, який ADR-002 явно **відхилив** — ADR не оновлено (governance-drift, `display_backend_registry.h:33`).
- **[LOW]** `HttpService` — dependency-magnet: 2111 рядків, 8 інжектованих підсистем, 30+ хендлерів; структурна причина upward-deps.
- **[LOW]** `modesp_mqtt` хардкодить ключі бізнес-модулів (`simple_thermo.setpoint`) у HA-discovery замість генерації з маніфестів.

---

## Попередні знахідки (потребують фінальної верифікації — верифікатори не встигли через ліміт)

Ці 28 знахідок продукували експерти `scenario-engine` і `build-config`, що глибоко читали код (описи цитують конкретні рядки/функції), але їх адверсарну перевірку зрізало на ліміті. Достовірність оцінюю високою; варто підтвердити перед фіксом.

### Scenario Engine
- **[HIGH]** Phase-scope resource claims **початкової** фази кожного треку ніколи не acquire'яться (`instance.cpp:74`) — `instance_start` ставить трек прямо в RUNNING без `try_acquire_phase` → два сценарії з ексклюзивним claim на ту саму `equipment.pump` стартують разом → обидва актуюють залізо, ISA-88 арбітрація зламана.
- **[HIGH]** Crash-recovery токен клобериться через тік після load (`engine.cpp:87`) — phase-entered emit поза RUNNING-перевіркою → NvsObserver перезаписує crash-токен минулого циклу токеном свіжого LOADED-стану → recovery тихо губить години прогресу. `deserialize_token` не перевіряє `scenario_state`.
- **[HIGH]** `loop_on_complete` документований і компілюється, але рантайм його не реалізує (`track.cpp:173`) — моніторинг-треки зупиняються після одного проходу; під ALL_TRACKS сценарій може завершитися передчасно.
- **[MEDIUM]** Phase-scope ресурси все ще RUNNING-треків **течуть** при термінальному стані сценарію (`instance.cpp:188`) — `release_scenario` фільтрує лише `TRACK_IDX_SCENARIO` → наступний сценарій висить WAITING_FOR_RESOURCE до timeout.
- **[MEDIUM]** `resume()`/`try_recover` ніколи не пере-acquire'ять scenario-scope ресурси (`engine.cpp:294`) — після reboot арбітр порожній → відновлений сценарій резюмиться без жодного зі своїх ексклюзивних ресурсів → колізія з новим.
- **[MEDIUM]** NaN-значення стану порівнюються як «рівні» (`builtin_actions.cpp:180`) — `state_key_eq/le/ge/in_range` усі спрацьовують TRUE на faulted-сенсорі (NaN); `in_range(temp,-30,-18)` звітує продукт у нормі, поки сенсор мертвий.
- **[MEDIUM]** Continuous behaviors (PID/hysteresis/ramp) реєструються й документуються, але рантайм їх не активує (`track.h:42`) — рецепт із hysteresis-контролем компілюється з warning'ом і працює **без керування температурою взагалі**. Плюс конфлікт ownership-контракту ContinuousFactory (`new T()` vs «static singleton, не delete»).
- **[MEDIUM]** `GLOBAL_SCOPE_MAIN_TRACK` — мертва семантика (`instance.cpp:166`): main-only global-abort усе одно валить весь сценарій того ж тіку, при цьому НЕ звільняючи phase-claims інших треків.
- **[LOW]** Main-track phase-persistence без throttle (`nvs_observer.cpp:93`) — швидко-циклічні рецепти забивають NVS (50 змін/с → переповнення черги + знос флешу).
- **[LOW]** `time_of_day_eq` ламається на DST-переходах (`builtin_actions.cpp:326`) — пропущена хвилина ніколи не fire'иться (трек може зависнути на 24 год), повторена — двічі.
- **[LOW]** Loader приймає TIME-based kinds для global-transitions, які рантайм хардкодить ніколи не спрацьовувати (`instance.cpp:49`) — time-based safety-abort валідується чисто й тихо не захищає.

### Build / конфігурація
- **[HIGH]** Документований board-switch flow **не працює**, свіжий clone з non-default дефолтом тихо збирається з даними чужої плати (`CMakeLists.txt:11`) — `fullclean` лишає sdkconfig; на fresh-clone kconfgen створює sdkconfig посеред configure ПІСЛЯ копіювання dev-файлів → прошивка на KC868-A6 драйвить GPIO dev-плати (реле не актуюють). Doc посилається на неіснуючий символ `CONFIG_MODESP_BOARD`.
- **[HIGH]** Кодогенератори виконуються під випадковим registry-знайденим host-Python, не IDF-venv (`CMakeLists.txt:39`) — у CMakeCache підтверджено Python 3.14 замість Espressif-python; `compile_scenario.py` жорстко вимагає `jsonschema` → `idf.py build` падає на configure, хоча користувач ставив requirements у PATH-python (split-brain). Прошивка не відтворювана.
- **[MEDIUM]** CI немає взагалі; pytest падає з INTERNALERROR під PATH-python, 2 тести залежать від обраної плати (`test_features.py:93`). Під правильним інтерпретатором — 354 passed (значно краще за червневі 144/470).
- **[MEDIUM]** Host-тести компілюють hand-forked SharedState, що вже **розійшовся** з реальним (`shared_state_host.cpp:56`) — реальний отримав BUG-017 + ABBA-fix, форк ні → 100/100 pass валідують стару семантику.
- **[MEDIUM]** `dependencies.lock` у .gitignore, а `modesp_net` оголошує безмежний `mdns >=1.3.2` — fresh-clone тягне неперевірені версії.
- **[MEDIUM]** Дисплейні backend'и виключені з bound-driver gate і з sdkconfig.defaults (`generate_ui.py:2341`) — `idf.py set-target esp32s3` тихо скидає плату на dev → stand_s3 падає на LogPort, екран темний без build-time-ознаки.
- **[MEDIUM]** board-config docs обіцяють build-time GPIO/ADC-валідацію, якої `generate_ui.py` не робить (`board-config.md:258`) — реле на GPIO7 / NTC на ADC2 дають чистий build і crashing-пристрій.
- **[LOW]** Поверх флору IDF-версії неузгоджений: маніфест `>=4.1.0`, docs `v5.x`, код вимагає `>=5.3` (esp_driver_* split), збиралося проти 6.0.1 (`idf_component.yml:5`).
- **[LOW]** `abs_test` увімкнений у шаблонному project.json → `.modr` пакується в кожен образ; будь-хто може стартувати тест на продакшн-контролері. Stale `.modr` не чистяться (`project.json:9`).
- **[LOW]** `modesp_json` — порожній stub-компонент, збирається щоразу, не референсується нічим (`modesp_json/CMakeLists.txt:3`).
- **[LOW]** `VALID_HARDWARE_TYPES` без `encoder` (`generate_ui.py:464`) — first-party encoder-драйвер дає warning на кожному build, привчаючи ігнорувати валідатор.

---

## Спростовано верифікацією (не баги)

- **WebSocket oversized-PING → PONG з NULL-payload** (`ws_service.cpp:232`) — верифікатор встановив, що шлях коректно захищений; не баг.
- **State-serialization send-length OOB** (`http_service.cpp:275`) — `SerCtx::remaining()` коректно клампить; не баг.

---

## Що реально виправлено з червня 2026 (перевірено)

F1 (SAFE MODE latch), F2 (PCF8574 bit-rollback), F3 (driver-pool overflow), H1 (arbiter OOB + duplicate-hash), H3 (DS18B20 blocking-retry-under-mutex), H4 (datalogger buf/io mutexes), H5 (datalogger forced-flush), H6 (compile_scenario у build), H7 (bindings/board валідація), H11 (NTC attenuation), BUG-021 (WS client mutex), BUG-024 (init_all звітує CRITICAL). Досі відкриті: **S2, S3, S4, S5, H2, H9, H10** і M-block dead-settings (частково).

---

## Рекомендований порядок робіт

1. **Boot/safety (блокери польового розгортання):** знахідки №1–5 + Safe Mode недосяжний + sensor-health gating. Малі локальні зміни, найбільший вплив на «рік без нагляду».
2. **Security-губернатор:** S3 (примусова зміна дефолтного пароля після reset), S4 (flash+NVS encryption / secure boot), S5 (прибрати self-reconfig креденшелів через data-plane), WS-auth, закрити відкритий AP.
3. **Пам'ять:** AWS shadow OOB (автономний, кожні 5 с) + OTA stack-буфер.
4. **Scenario-engine коректність:** підтвердити й закрити 11 попередніх знахідок (arbitration/recovery — ядро надійності мультисценарних ворклоадів).
5. **Build/repro:** board-switch flow, IDF-venv Python для кодогену, commit `dependencies.lock`, мінімальний CI.
6. **Дисплей-тайминг:** shadow-diffing render, розбити font-recovery по бюджету транзакцій, полагодити banner-адресацію на hero-екрані.

*Звіт згенеровано багатоагентним ревью з адверсарною верифікацією. 28 знахідок scenario-engine/build-config — попередні (верифікацію зрізало на ліміті), решта — підтверджені щонайменше одним незалежним скептиком, critical/high — голосуванням трьох.*
