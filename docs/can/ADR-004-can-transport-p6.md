# ADR-004 — CAN-транспорт (P6): компонент `modesp_can` + власний кадровий протокол ModESP-CAN v1

- **Статус:** ЗАПРОПОНОВАНО (draft, на затвердження архітектором; реалізація C0–C2 на гілці `feat/can-transport`)
- **Дата:** 2026-07-15
- **Контекст (проєкту):** ModESP Framework (ESP-IDF v6.0.1, C++17 + ETL, zero-heap у hot-path, single main-loop task 100 Hz)
- **Спирається на:** [capability-roadmap](../../documentation/uk/03-framework-reference/capability-roadmap.md) (P6 = «другий транспорт end-to-end»), CODE_REVIEW_2026-07-07 (патерни відмов, яких треба уникати), зовнішній дизайн-документ «ModESP × CAN» (довідково, не джерело істини)
- **Пов'язані:** ADR-002/003 (шов display-backend'ів — прецедент «реєстр + factory»), модель `modesp_ble` (перший транспорт — зразок для дзеркалення)

---

## 1. Контекст і проблема

Потрібно об'єднати кілька вузлів на базі ModESP (автономні контролери, сенсорні
вузли, органи керування) у детерміновану дротову мережу. CAN 2.0B — природний
вибір: диференційна пара, арбітраж без колізій, 125–250 kbps на сотні метрів,
автомобільна/промислова спадщина.

Що вже є в `main` (P0–P5):

- **роль = capability** (`tools/capabilities.json`, матчер у генераторі);
- **транспорт-генеричний реєстр пристроїв**: `RemoteDeviceConfig{id, transport,
  identity, name}` у `board.json remote_devices` + runtime `/data/devices.json`;
  identity — непрозорий blob, ролі його не бачать;
- **зафіксована модель**: *новий транспорт = новий компонент + драйвер-міст (як
  `modesp_ble` + `ble_xiaomi_th`); HAL/генератор/webui майже не чіпаються;
  HAL не залежить від жодного транспорту*.

P6 у roadmap явно відкладений: «взяти 2-й транспорт end-to-end». Це він.

## 2. Рішення (один абзац)

Додаємо **`components/modesp_can`** — Kconfig-gated компонент, єдиний власник
TWAI-ноди (новий `esp_driver_twai` node-API), який мостить **два внутрішні
плани** ModESP: SharedState (retained-стан, on-change + періодичний refresh) і
шину повідомлень (transient-події через конверт `MsgRemoteEvent`). Поверх
сирого CAN — **власний легкий протокол ModESP-CAN v1**, згенерований з
маніфестів (`generated/can_registry.h`): стабільні 16-бітні ID ключів/подій
(djb2-хеш імені з можливістю override), heartbeat + liveness, CRC реєстру для
виявлення розбіжності прошивок. Імпорт чужих значень — через **драйвер-міст
`drivers/can_remote`** (звичайний `ISensorDriver`, який читає кеш
`CanRemoteCache`), прив'язаний до `remote_devices{transport:"can",
identity:"<node-id>"}` + `binding.address = <ім'я віддаленого ключа>`. Піни та
бітрейт — нова секція **`can_buses`** у `board.json` (ім'я зарезервоване в
документації ще зі Stage 1.5), HAL зберігає лише конфіг (як `uart_buses`).

## 3. Свідомі відхилення від зовнішнього дизайн-документа

| Пропозиція документа | Рішення тут | Чому |
|---|---|---|
| C++-інтерфейс `IRemoteTransport` | **Не вводимо** | Репозиторій уже свідомо відкинув цю абстракцію: модель P0–P5 — «новий транспорт = новий компонент + драйвер-міст», транспорт — string-тег + шви-реєстри. Зміна протоколу (CANopen/Cyphal) = інша реалізація компонента за тим самим driver-bridge швом — модулі так само не змінюються. |
| `can_import` у маніфесті модуля | **Відкладено (v1.1)** | Імпорт у v1 — лише через binding драйвера-моста (роль ← capability) — це і є Режим A. Загальне дзеркало `remote.<node>.<key>` без ролі впирається в бюджет SharedState (135 записів) і в 32-символьний ліміт ключа; окреме рішення після досвіду C3. |
| `node_id` у `board.json can_bus` | **NVS (`can.node_id`) + MAC-derived default** | `board.json` описує МОДЕЛЬ плати; два пристрої однієї моделі мають різні адреси. Аналогія: MQTT `device_id` виводиться з MAC. Дефолт: `djb2(MAC)%253+1`, override через NVS/API. |
| CANopen / Cyphal одразу | **Опція C (власний протокол), шлях назад відкритий** | Гомогенний флот, codegen-філософія, zero-heap. CANopenNode тягне OD-церемонію і чужий стиль пам'яті; Cyphal — O(1)-алокатор і крутішу криву. Обидва — можлива заміна реалізації `modesp_can` пізніше (перевірити інтероп-вимоги на C6). |
| Події = whitelisted типізовані `etl::imessage` | **Єдиний конверт `MsgRemoteEvent`** | Серіалізація довільних message-типів вимагає per-type кодеків. Конверт {node, event_id, len, data[8]} симетричний в обидва боки: `node==0` → «надіслати в шину CAN», `node!=0` → «прийнято від вузла». Шина повідомлень у кодовій базі й так майже не використовується — консерватизм доречний. |

## 4. Протокол ModESP-CAN v1

### 4.1 Ідентифікатор кадру (29-біт extended)

```
[28:26] prio (3 біт, 0 = найвищий)   події=2, стан=4, heartbeat=6
[25:24] kind (2 біт)                 0=HEARTBEAT, 1=EVENT, 2=STATE, 3=CMD (зарезервовано)
[23:16] src  (8 біт)                 node-id відправника, 1..253 (0 і 254/255 зарезервовані)
[15:0]  id16 (16 біт)                key-id / event-id; для HEARTBEAT = magic 0x4D43 («MC»)
```

Правило CAN «два вузли не шлють один arb-ID з різним payload» виконується
конструктивно: `src` — частина ID, кожен вузол володіє своїм простором.

### 4.2 Кадри

**STATE (kind=2).** Retained-значення ключа SharedState. Payload:
`[0]=тип+прапори` (біти 0–3: 0=f32, 1=i32, 2=bool; біт 6 = VALID), `[1..4]` —
значення little-endian (bool — 1 байт, DLC=2; f32/i32 — DLC=5). VALID=0 ⇔
локальний компаньйон `<key>_ok == false` (заморожене EMA у equipment) —
імпортер транслює це у `is_healthy()=false`. Надсилається on-change
(polling `version()`, як MQTT) + повний refresh кожні 5 с (емуляція retained
для пізніх слухачів). **Рядкові ключі у v1 не експортуються** (не влазять у
8 байт; фрагментація — окреме рішення).

**EVENT (kind=1).** Transient-подія. Payload — сирі 0..8 байт за манiфестом
(`payload: none|u8|i32|f32`). На RX компонент публікує в ЛОКАЛЬНУ шину
`MsgRemoteEvent{node=src, event_id, len, data}` з main-loop task (шина не
thread-safe — публікація з чужих тасків заборонена, це підтверджений факт
рев'ю). На TX модуль публікує `MsgRemoteEvent{node=0, ...}` — `CanService`
ловить його в `on_message` і кодує в кадр, якщо event_id оголошений як `out`.

**HEARTBEAT (kind=0, id16=0x4D43).** Період 1000 мс. Payload: `[0]` proto_ver
(=1), `[1]` прапори (біт 0 = safe_mode вузла), `[2..3]` CRC16 реєстру
(виявлення розбіжності маніфестів у флоті), `[4..7]` uptime у секундах (LE).
Вузол вважається offline після 3500 мс тиші → усі його імпортовані значення
стають unhealthy, `remote.<id>.online=false` — доступно interlock'ам через
звичайний state-ключ.

**CMD (kind=3).** Зарезервовано під адресний запис rw-ключів (v1.1): payload
`[0]=target node`, далі як STATE. У v1 команди передаються подіями.

### 4.3 Стабільні ID

`id16 = fold16(djb2(ім'я))`, де fold16(h)=(h^(h>>16))&0xFFFF, 0→0x0001.
Ключі стану хешуються від повного імені ключа (`equipment.air_temp`), події —
від `<module>.<event>`. Генератор перевіряє колізії в межах кожного kind
(окремі простори) і падає з помилкою; явний override — поле `can_id`.
Хеш-derived ID самоописні для гетерогенного флоту: різні прошивки виводять
однаковий ID для однакового імені без спільного реєстру. Python-реалізація
(генератор) і C++ (runtime-підписка моста) мають спільні тест-вектори.

### 4.4 Фільтрація

ESP32/S3 мають 1 mask-фільтр — гетерогенний набір (heartbeat усіх + вибрані
ключі + події) ним не виражається. v1: приймаємо всі extended-кадри, software
dispatch у `on_update` (на 250 kbps це ≤ ~2000 кадрів/с, десятки байт роботи
на кадр). HW-фільтр — оптимізація пізніше, якщо виміри покажуть потребу.

## 5. Схема + генератор (фази C0–C1)

- **`board.schema.json` + парсер + HAL:** секція `can_buses:
  [{id, tx, rx, bitrate=125000, label}]` → `CanBusConfig` (config-only, як
  `uart_buses`; TWAI-периферією володіє компонент). `BOARD_SECTION_TO_HW_TYPE
  += {"can_buses": "can_bus"}`.
- **`remote_devices` per-transport hw_type (заявлений «P6 refinement»):**
  entry з `transport:"can"` → hw_type `"can"`; identity для can — ціле
  1..253 (валідація в генераторі поруч із MAC-перевіркою для ble).
- **`driver.schema.json`:** transport enum += `"can"`.
- **`module.schema.json`:** (а) state-ключ: `can_export: bool` (+опц.
  `can_id`); лише float/int/bool; (б) `requires[]`-роль: `can_export: bool`
  (+`can_id`) — експортує runtime-ключ `equipment.<role>` з типом з
  capability; (в) top-level `can_events: {out: {назва: {payload?, can_id?}},
  in: {...}}`.
- **`generated/can_registry.h`:** `EXPORT_KEYS[]{key, id16, type}`,
  `EVENTS[]{name, id16, outbound}`, `REGISTRY_CRC16`, `PROTO_VER` — за
  дисципліною always-rewrite + sentinel-запис для порожніх масивів.
- **`HW_TYPE_KCONFIG_DEPS += {"can": "MODESP_CAN_ENABLE"}`** — драйвери-мости
  автоматично вимикаються без компонента.
- Білд без жодного `can_*` у маніфестах і без `CONFIG_MODESP_CAN_ENABLE`
  зобов'язаний лишитися еквівалентним (нові файли — інертні).

## 6. Компонент `modesp_can` (фаза C2)

```
components/modesp_can/
├── CMakeLists.txt        # порожні SRCS коли вимкнено (патерн modesp_ble)
├── Kconfig               # MODESP_CAN_ENABLE (default n), MODESP_CAN_SELF_TEST
├── include/modesp/can/
│   ├── can_codec.h        # чистий header: pack/unpack ID, payload, djb2_16, crc16 — host-testable
│   ├── can_remote_cache.h # кеш вузлів/підписок (дзеркало BleCentral), час ін'єктується
│   ├── can_messages.h     # MsgRemoteEvent (msg_id::CAN_REMOTE_EVENT = 60, діапазон сервісів)
│   └── can_service.h
└── src/{can_service.cpp, can_remote_cache.cpp}
```

**`CanService`** — `BaseModule("can", HIGH)`; DI: `set_hal(&hal)` з main.cpp.

Правила потоків (усі — наслідки підтверджених фактів рев'ю/IDF):

- Всі 4 callback'и нового TWAI-драйвера — **ISR-контекст**; blocking-RX API не
  існує. RX: `on_rx_done` → `twai_node_receive_from_isr` у статичний ring
  (SPSC: продюсер-ISR / консюмер-main-loop). Вся логіка (декод, state_set,
  publish) — у `on_update`.
- TX **zero-copy** (черга драйвера зберігає вказівники!) → статичний пул
  кадрів; слот звільняє `on_tx_done`. `twai_node_transmit(timeout=0)` —
  неблокуючий; переповнення черги = лічильник, не очікування (TWDT 5 c,
  trigger_panic — блокуватись у main loop не можна).
- Помилки шини: `on_error/on_state_change` лише інкрементують атомарні
  лічильники/прапори; `on_update` веде backoff і після bus-off
  **перестворює ноду** (drop черги драйвера + повний reset TX-пулу — драйвер
  IDF не викликає `tx_done` для кадру, перерваного bus-off, тож слот пулу
  інакше тік би назавжди; знахідка адверсарного рев'ю 2026-07-15). Статус:
  `bus_off` → `recovering` → `ok`, де `ok` — лише після реального `tx_done`
  (stuck-dominant шина не має брехати «ok»). Жодного ErrorService з чужого
  таска (він не потокобезпечний — знахідка рев'ю).
- Невдача ініціалізації CAN — **не фатальна** для боту (знахідка-блокер
  рев'ю: fail у init → мертвий пристрій): `can.status="error"`, модуль живий.

Стан-ключі: `can.status` (string), `can.node_id` (int), `can.tx_count` /
`can.rx_count` / `can.err_count` (int, track_change=false), `can.bus_off`
(bool), `remote.<device-id>.online` (bool) на кожен зареєстрований вузол.

**Self-test (C2):** `CONFIG_MODESP_CAN_SELF_TEST` → прапори
`enable_self_test + enable_loopback` ноди: власні кадри повертаються без ACK
і трансивера — повний TX→RX цикл на одному стенді; `can.loopback_ok=true`
після першого власного heartbeat.

## 7. Драйвер-міст `drivers/can_remote` (фаза C4 — скелет у C2)

Категорія sensor, `hardware_type:"can"`, `transport:"can"`,
`requires_address:true` (**address = ім'я віддаленого ключа**, напр.
`equipment.air_temp`), `multiple_per_bus:true`, setting `stale_ms=5000`.
Factory: `hal.find_remote_device(hardware)` → identity → node_id →
`CanRemoteCache::register_subscription(node_id, fold16(djb2(address)))` →
читає кеш у `read()`; `is_healthy()` = VALID && свіжий && вузол online.
Capability каналу у v1 не декларується (generic міст, matcher падає у
fallback за типом) — уточнення після досвіду discovery (відкрите питання 10.3).

## 8. План фаз (дисципліна roadmap: кожен крок — зелений білд)

| ID | Зміст | Ця сесія |
|----|-------|----------|
| C0 | Схеми (module/driver/board) + парсер board.json + HAL config + валідація identity | ✅ |
| C1 | Генератор: `can_registry.h` + колізії/типи + pytest | ✅ |
| C2 | `modesp_can` (service+codec+cache+messages) + main.cpp wiring + self-test + host-тести кодека | ✅ |
| C3 | 2 вузли на фізичній шині: стан-mirror + heartbeat + liveness (потрібні трансивери) | обладнання |
| C3b | Події end-to-end: орган керування → актуатор | обладнання |
| C4 | `can_remote` міст: роль ← capability до CAN-вузла (Режим A e2e) + webui DevicesPage для CAN | частково (скелет) |
| C5 | Liveness в interlocks продукту; discovery (анонс експорт-таблиці) | — |
| C6 | (опційно) CANopen/Cyphal реалізація за тим самим швом | — |

## 9. Наслідки

**Плюси:** транспорт №2 доводить генеричність P0–P5 end-to-end; нуль змін у
модулях-споживачах; нуль залежності від MQTT/хмари; zero-heap, детерміновані
ID; один стенд тестується без трансивера (self-test); host-тести кодека.

**Мінуси/борг:** немає інтеропу з чужими CAN-пристроями (свідомо, шлях C6);
рядки/фрагментація не підтримані; software-фільтрація на завантаженій шині —
CPU-податок; discovery/webui UX для CAN відкладені; command-кадри (kind=3)
зарезервовані, але не реалізовані.

## 10. Відкриті питання

1. **kind=3 (адресний запис rw-ключів)** — потрібен для «панель пише setpoint
   сусіда» без пари подій. Рішення після C3b.
2. **Авто-дзеркало `remote.<node>.<key>`** для scenario-engine без ролей —
   впирається в бюджет SharedState; можливо, окремий обмежений whitelist.
3. **Discovery**: анонс експорт-таблиці на старті (kind=EVENT, зарезервований
   event-id) → webui список вузлів/каналів → capability з боку імпортера.
4. **Мульти-bus** (ESP32-P4 має 3 контролери) — структура даних готова
   (vector), сервіс v1 бере перший бус.
5. **Бітрейт-профілі** та рекомендації по трансиверах (SN65HVD230 3.3V) —
   у документацію 04-hardware після C3.
