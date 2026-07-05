# Roadmap: capability-типізована, транспорт-генерична периферія

**Статус:** затверджено (2026-07). Мета — довести до продакшн модель, де **роль = здатність (capability), ніколи не драйвер**, для **сенсорів І актуаторів**, транспорт-генерично (wired + BLE зараз; LoRa/MQTT/ESP-NOW далі). Дизайн отримано дизайн-проходом (grounded); суддя обрав інкрементальну архітектуру + графти.

## Принцип
`capability` — **build-time концепт**. Роль = екземпляр capability; on-device резолвиться через `find_sensor/find_actuator(role)`, а `Binding{hardware,driver,role,address}` уже транспорт-агностичний. Тому **HAL ніколи не вчить слово «capability», фази P1–P4 = 0 змін у прошивці.** Генеричність живе в маніфестах + `generate_ui.py` + webui. Див. пам'ять `role-equals-capability`, `default-to-universal`, `ble-device-registry-model`.

## Модель
- **`tools/capabilities.json`** — SSOT словника: `{name, kind:sensor|actuator, direction:in|out, value_type, unit?}`.
- **Роль** оголошує `capability` (не драйвер). Кілька ролей можуть ділити capability (`air_temp` камера, `room_temp` кімната — обидві `temperature`, приймають будь-яке джерело).
- **Драйвер**: `provides.capability` (одноканальний) або `provides.channels[{channel,capability}]` (мультиканальний, рекомендовано); `address_channels[].capability` — back-compat шлях. Один пристрій → канали різної capability → різні ролі.
- **`transport`** — окреме поле драйвера (`wired|ble|lora|mqtt|espnow`), авто-виводиться з `hardware_type`. Реєстр пристроїв: `{id, transport, identity, name}` — `identity` непрозорий блоб (MAC/topic/devaddr); **ролі його не бачать** (identity ніколи на біндінгу ролі).
- **Матч:** роль приймає канал ⟺ `capability` рівні + напрям узгоджений. Жодного драйвера/hw_type/транспорту в предикаті.
- **Новий транспорт = новий компонент + драйвер-міст** (як `modesp_ble`). HAL/генератор/webui не чіпаються. **HAL не залежить від жодного транспорту** (інваріант `core←hal←…←ble`).

## Фази
| ID | Зміст | C++? | Ризик |
|----|-------|------|-------|
| **P0** | `capabilities.json` + завантаження/валідація/деривація в генераторі + `--report-capabilities` observe-only + golden byte-identical | ні | дуже низький |
| **P1** | опційні поля `capability`/`transport` у схемах драйвера/модуля + drift-guard + fallback (усе білдиться ідентично) | ні | дуже низький |
| **P2** | 13 драйверів дістають `capability`+`transport` (авто-міграція + підтвердження неоднозначних); генератор будує eligible-by-capability індекс | ні | низький |
| **P3** | матч за capability в генераторі (`cross_validate` + `_bindings_page`) + міграція маніфестів модулів (ролі кидають driver-списки) | ні | середній |
| **P4** | webui: плаский **Сенсори/Актуатори** список, `compatibleHw` за capability, `DevicesPage` за `transports[]` | ні | низько-серед. |
| **P5** | **єдина прошивкова фаза**: `BleDeviceConfig→RemoteDeviceConfig{id,transport,identity,name}`, `ble_devices→remote_devices`, `MAX_BLE_DEVICES→MAX_REMOTE_DEVICES` (=16), аліаси, byte-preserving, review-before-commit. `modesp_ble` НЕ чіпаємо | так (rename) | середній |
| P6 | (відкладено) довести 2-й транспорт end-to-end | — | — |

## Зафіксовані рішення
- Словник capabilities — **OPEN** (автор додає рядки, генератор валідує; typo ловиться eligible-set warning, не schema-reject).
- Аліаси (`type↔kind`, `mac↔identity`, `ble_devices↔remote_devices`) — **назавжди** (без flag-day).
- Роль назвала драйвер поряд із capability → **WARN** (нудж не переобмежувати).
- HAL-facet `capability_of()/channel_capability()` + `DiscoveredDevice.transport[12]` — **закладаємо** (малий слід; `_CAP` register-макроси деградують до звичайних → hand-written .cpp не міняється).
- `MAX_REMOTE_DEVICES=16` на ВСІ транспорти (піднімемо за потреби). `identity` ширина **40** (рефактор пізніше).

## Міграція (без flag-day)
Усі нові поля **опційні**; поки драйвер/роль не оголосили capability — генератор падає назад на теперішній `category==type`. `tools/migrate_capabilities.py` виводить capability з наявних сигналів (unit °C→temperature, category actuator+gpio→relay_out, address_channels temperature/humidity/battery, ld2410b presence/distance/energy, nRF angle/accel, digital_input→binary_in). `board.json ble_devices` читається як `remote_devices` (аліас; рядок без transport→"ble", без identity→mac). `bindings.json` — БЕЗ змін. Стара `/data/devices.json` на пристроях парситься після OTA.

## Статус (2026-07)
P0–P5 **зроблено й у `main`**. P4.5 (UX: роль без транспорту в назві, per-driver `channels_by_driver`, авто-прив'язка єдиного каналу, дружнє ім'я пристрою) — теж у `main`. P6 (2-й транспорт) відкладено. Деталі — пам'ять `capability-roadmap-status`.

## Відкладені архітектурні напрями (bottom-up — після P5)
Два питання того самого кореня: модель зараз **top-down** (периферія з'являється в системі лише коли якийсь модуль ЯВНО вимагає її роллю), а цільова універсальна візія — **bottom-up** (пристрій дав N каналів → усі N видимі; ролі опційні).

### 1. Видимість каналів (channel visibility)
**Симптом:** пристрій транслює кілька каналів (Xiaomi: temperature/humidity/**battery**; nRF: angle/**tilted**/**battery**/**осі**), але видно лише ті, під які є роль (`room_temp`=temperature, `orientation`=angle). Вологість/батарея/осі невидимі, бо їх ніхто не споживає. `EquipmentBase` публікує **один стан на РОЛЬ** (`equipment.<role>`), тож `equipment.room_humid` (який читає панель) ніхто не публікує — роль `room_humid` **ніколи не існувала** (не регресія, давня діра).

**Напрям (обрано B):** кожен підписаний канал пристрою **авто-реєструється як сенсор** у SharedState (напр. `sensor.<device>.<channel>`), незалежно від ролей модулів. Ролі лишаються для **логіки** (термостату потрібна температура — це роль); але сира видимість/логування/MQTT/панель НЕ вимагають ролі. Фреймворк-зміна (SharedState-публікація каналів реєстру пристроїв + генератор/webui їх показує). Тимчасовий костиль A (додати ролі `room_humid`/`room_batt` у модуль) — **відкинуто** як per-channel хардкод проти філософії.

### 2. Злиття `display` + `panel`
Обидва «виводять інформацію»; поділ на дві capability — надмірна спеціалізація. Ціль — одна capability `display` з фасетами `as_menu()`/`as_text()` (за наявним патерном `IDisplayPort::as_*()`). Повний бриф — spawn_task chip + пам'ять `display-panel-unify-todo`.

Обидва — реальні багатокомпонентні зміни: build + host-тести + adversarial review + review-before-commit.
