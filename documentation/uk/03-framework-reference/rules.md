# Правила фреймворку (живий звід)

**Єдине авторитетне джерело правил ModESP.** Кожне правило — нумероване, з **чому** (навіщо) і **як застосувати**. Порушення = зламаний білд, тихий баг на залізі, або порушена архітектура. Це доповнює [project-hierarchy.md](project-hierarchy.md) (ієрархія + маршрут периферії) — сюди винесено самі ПРАВИЛА, щоб їх було де перевірити одним поглядом.

> Тримай живим: додаєш можливість — додай/онови правило тут. `CLAUDE.md` і `project-hierarchy.md` лінкують сюди.

---

## 0. Founding-принципи (не порушувати ніколи)

### R0.1 — Роль = здатність (capability), ніколи не драйвер
Роль оголошує `capability` (temperature/relay_out/panel…), а НЕ драйвер. Термостат потребує «температуру» і не знає, хто її дає (ds18b20 / NTC / BLE-канал / майбутній LoRa).
**Чому:** уся система абстракцій існує саме заради цього — щоб джерело було замінним. Обхід = втрата сенсу.
**Як:** у маніфесті модуля `{"role":..., "capability":"temperature"}`; on-device резолв через `find_sensor/find_actuator(role)`. Ніколи не хардкодь драйвер у ролі/модулі. Див. пам'ять `role-equals-capability`.

### R0.2 — За замовчуванням — універсально й транспорт-агностично
Ніколи не хардкодь драйвер / транспорт / канал / кількість. Закладай мультитранспортне майбутнє (BLE/LoRa/MQTT/ESP-NOW), сенсори І актуатори.
**Чому:** сценарій використання має бути гнучким; кожен хардкод — майбутній рефактор.
**Як:** нове поле → опційне з fallback; нова здатність → рядок у `capabilities.json`, не гілка в коді. Див. `default-to-universal`.

### R0.3 — Ідентичність — на пристрої, ніколи на ролі
`Binding` посилається на device `id`; ідентичність (MAC/adv-name/topic/devaddr) живе на рядку пристрою (board.json/devices.json), НЕ в bindings.json. `find_remote_device` резолвить id→identity/name.
**Чому:** роль транспорт-агностична; MAC на біндінгу ролі прив'язує роль до транспорту.

---

## 1. Іменування

### R1.1 — Ім'я драйвера/модуля == папка == поле маніфесту
Regex `^[a-z][a-z0-9_]*$`. Для драйвера — ще й перший аргумент register-макросу. `modules/heat_pump/` → `"heat_pump"` у project.json → клас `HeatPumpModule` → `heat_pump_module.h`.
**Чому:** генератор мапить папку↔маніфест↔реєстрацію; розбіжність ламає білд.

### R1.2 — Роль оголошує лише модуль-власник
Роль кладе ТОЙ модуль, що її споживає. Навіть багатий connect-драйвер (panel) оголошує роль у своєму модулі й резолвиться за роллю, не через глобал.

### R1.3 — Назви ролей/каналів — без транспорту
Мітка ролі не згадує транспорт («Room temperature», не «BLE room sensor»). Мітка каналу деривується з `capabilities.json`; драйвер перевизначає лише за потреби (інша одиниця чи розрізнення 2+ каналів однієї здатності).

---

## 2. Zero-heap hot path

### R2.1 — Жодних куп-алокацій у гарячому шляху
НІКОЛИ `std::string`/`std::vector`/`new`/`malloc` у `on_update()`/`on_message()`. ЗАВЖДИ `etl::string<N>`/`etl::vector<T,N>`/`etl::variant`/`etl::optional`.
**Чому:** фрагментація купи на ESP32 = падіння через дні аптайму.

---

## 3. Периферія: capability-матч + маршрут

### R3.1 — Матч ролі й каналу — лише за capability
Роль приймає канал ⟺ `capability` рівні + напрям (in/out) узгоджений. Жодного драйвера/hw_type/транспорту в предикаті. `capabilities.json` — SSOT словника.

### R3.2 — Один драйвер = один register-макрос
`MODESP_REGISTER_SENSOR/ACTUATOR(name, &factory)` — одна точка. Декодери/матчери реєструються на BOOT (register-хук), НЕ у фабриці — інакше неприв'язаний пристрій невидимий у скані.

### R3.3 — Модулі не чіпають GPIO
Тільки через `ISensorDriver`/`IActuatorDriver`/`IDisplayPort`/`IAudioSink`/`IPanelPort` + bindings + SharedState. Аналогові актуатори override'ять `set_value/get_value/supports_analog` (default = discrete on/off).

### R3.4 — `binding.module` — маршрутизація
Мусить називати модуль з project.json І бути власником ролі, інакше білд падає або залізо ні до кого не під'єднане.

### R3.5 — per-driver атрибути гейтяться за ОБРАНИМ залізом
`requires_address`/`channels`/`scan` — PER-DRIVER. WebUI ключить їх на BOUND драйвер (`addr_drivers`, `channels_by_driver`), не на роль-агрегат — інакше дротова прив'язка вимагає BLE-адресу.

---

## 4. Транспорт-генеричність

### R4.1 — Пристрій = `RemoteDeviceConfig{id, transport, identity, name}`
`identity` — непрозорий блоб (BLE MAC; майбутні LoRa devaddr/MQTT topic). `transport` — окреме поле, авто-виводиться з `hardware_type`. Реєстр у HAL (`remote_devices_`), резолв `find_remote_device(id)`.

### R4.2 — Новий транспорт = новий компонент + драйвер-міст
Як `modesp_ble`. HAL/генератор/webui **не** чіпаються. HAL **не** залежить від жодного транспорту.

### R4.3 — devices.json — runtime-only
Пишеться пристроєм у `/data/devices.json`, НІКОЛИ не build-вхід. Не лишай його в `data/` під час білду (потрапить у data.bin і перезапише реальні підписки). Gitignored.

---

## 5. Залежності й опційність

### R5.1 — Напрями залежностей
`modules→framework→platform`; `drivers→hal`; `ble→net` (НЕ net→ble); ніщо не залежить від `modesp_core` у зворотний бік; framework не залежить від product. Інваріант ядра: `core←hal←services←net←ble`.

### R5.2 — Опційні компоненти гейтять лише SRCS на `CONFIG_*`, ніколи REQUIRES
Драйвер завжди через `modesp_driver_component()`, ніколи голий `idf_component_register`. Вимкнений у menuconfig драйвер не компілюється.

### R5.3 — Cloud взаємовиключний
mqtt XOR aws XOR none. Board-хардкод у модулях заборонений — залізо лише через board.json/bindings.json.

---

## 6. Генеровані файли — НЕ РЕДАГУВАТИ

### R6.1 — Ніколи не редагуй згенероване
`data/ui.json`, весь `generated/*.h` + `generated/*.cmake`, `components/modesp_hal/Kconfig`, `main/Kconfig.boards`, `data/www/i18n/*`. Змінюй **МАНІФЕСТ** — `CMAKE_CONFIGURE_DEPENDS` перегенерує.
**Чому:** перезаписуються при білді; правка втратиться.

### R6.2 — Дротове → board.json; runtime-remote → devices.json
board.json — GET-only на пристрої; його remote-секція — лише factory seed. Не хардкодь BLE в board.json. Редагуй board/bindings у `boards/<board>/`, НЕ в `data/` (там копії).

---

## 7. Ліміти (жорсткі кепи)

### R7.1
`MAX_BINDINGS=24`, `MAX_REMOTE_DEVICES=16`, `MAX_RUNTIME_DEVICES=12`, `MAX_LOG_CHANNELS=6`, меню ≤255 nodes / ≤15 root-submenus. Незнана секція board.json тихо ігнорується (лише warning) — hardware зникає, тож звіряй імена.

---

## 8. Build

### R8.1 — data.bin завжди свіжий → білдь через `run_build.ps1`
Ninja-ціль LittleFS-образу має phony-вихід → `DEPENDS`/`ninja -t clean` НЕ форсять перезбірку; змінений `data/` летить старим. `run_build.ps1` після білду перезапускає littlefs-команду → образ = поточний `data/`. Ручний `Remove-Item build\data.bin` не потрібен.

### R8.2 — Правка маніфесту сама ретригерить генерацію
`CMAKE_CONFIGURE_DEPENDS` покриває manifest/project.json/schemas — ручний `idf.py reconfigure` не потрібен. Після `fullclean` за потреби `Remove-Item build\esp-idf\marcel-cd__etlcpp` (ETL clang-fix).

### R8.3 — Валідація на build-time
`generate_ui.py` перевіряє bindings↔board↔driver + capability↔словник. Невалідна прив'язка/невідома capability ламає білд. Плата, що прив'язує вимкнений драйвер → FATAL; узгодити `tools/drivers_sync.py --fix/--prune`.

---

## 9. Документація (це теж правило)

### R9.1 — Кожен драйвер і модуль МУСИТЬ мати док
`documentation/{uk,en}/03-framework-reference/{drivers,modules}/<name>.md`. Host-lint валить збірку, якщо `drivers/*/` чи `modules/*/` без доку. Шапка доку виводиться з маніфесту (capability/канали/стан) — авто-звірка проти розсинхрону.

### R9.2 — uk — авторитетна, en — дзеркало
Пишеш українською; англійська дзеркалить. Стиль — [STYLE.md](../../STYLE.md) / [docs-style](../06-contributing/docs-style.md).

---

## Джерела (звідки консолідовано)
- [project-hierarchy.md](project-hierarchy.md) — INVARIANTS + маршрут периферії.
- [capability-roadmap.md](capability-roadmap.md) — зафіксовані рішення capability-моделі.
- `CLAUDE.md` — критичні правила збірки (лінкує сюди).
- Пам'ять: `role-equals-capability`, `default-to-universal`, `ble-device-registry-model`, `capability-roadmap-status`.
