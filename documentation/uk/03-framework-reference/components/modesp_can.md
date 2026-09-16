# `modesp_can` — CAN (TWAI) транспорт, ModESP-CAN v1

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_can.md](../../../en/03-framework-reference/components/modesp_can.md)

`modesp_can` — дротовий транспорт фреймворку для об'єднання кількох
ModESP-вузлів по класичному CAN 2.0B. Це Kconfig-gated компонент і **єдиний
власник TWAI-ноди** чипа (новий node-API `esp_driver_twai`, ESP-IDF v6).
Поверх сирого CAN він розмовляє протоколом **ModESP-CAN v1** — легким
кадровим протоколом, згенерованим з маніфестів і спроєктованим в
[ADR-004](../../../../docs/can/ADR-004-can-transport-p6.md). Компонент живе в
`components/modesp_can` — не в `modules/` — і реєструється в `main.cpp` як
сервіс `BaseModule("can", HIGH)` з ін'єкцією залежностей (`set_hal(&hal)` +
`set_state(&app.state())`) до `init_all`.

Компонент мостить **два внутрішні плани** ModESP:

| План | Назовні | Всередину |
|---|---|---|
| **SharedState** (retained-значення) | Ключі з позначкою `can_export` у маніфестах транслюються STATE-кадрами — on-change плюс повний refresh кожні 5 с. | STATE-кадри інших вузлів осідають у `CanRemoteCache`; драйвер-міст [`can_remote`](../drivers/can_remote.md) перетворює їх на звичайні ролі. |
| **Шина повідомлень** (transient-події) | Модуль публікує `MsgRemoteEvent{node=0, ...}`; сервіс кодує його в EVENT-кадр, якщо подія оголошена в `can_events.out`. | Прийнятий EVENT-кадр, оголошений у `can_events.in`, публікується локально як `MsgRemoteEvent{node=<src>, ...}` з main-loop task. |

Модулі-споживачі ніколи не бачать CAN: роль, яку живить віддалений вузол,
виглядає точнісінько як роль від локального сенсора. У цьому й сенс —
транспорт доводить модель P0–P5 «роль = capability, identity живе на
пристрої» end-to-end.

## Збирання та конфігурація (Kconfig)

Меню **"ModESP CAN"** (`components/modesp_can/Kconfig`):

| Символ | Типово | Значення |
|---|---|---|
| `CONFIG_MODESP_CAN_ENABLE` | n | Головний вимикач. Вимкнено → компонент дає **нуль сирців** (порожній `SRCS`, патерн `modesp_ble`), тож збірки без CAN не несуть жодного CAN-коду чи флешу. |
| `CONFIG_MODESP_CAN_SELF_TEST` | n | Стендовий loopback-режим без ACK — див. [Self-test](#self-test-config_modesp_can_self_test). Ніколи не вмикайте на робочій шині. |

`REQUIRES`: `modesp_core`, `modesp_hal`. `PRIV_REQUIRES`: `esp_driver_twai`,
`esp_timer`, `nvs_flash`. Згенерований `can_registry.h` підключається з
`generated/` (той самий механізм єдиного джерела істини, що й
`mqtt_topics.h` у `modesp_mqtt`).

Ще дві речі, потрібні кожному CAN-вузлу:

- запис `can_buses` у board.json (піни + бітрейт, нижче);
- **зовнішній CAN-трансивер** (SN65HVD230, TJA1051T/3, ...) між пінами MCU і
  шиною. TWAI-контролер ESP32 не має фізичного рівня — під'єднатися до
  реальної шини без трансивера неможливо.

Драйвери-мости з `hardware_type: "can"` (наприклад `can_remote`) отримують
автоматичний `depends on MODESP_CAN_ENABLE` у своєму згенерованому toggle
(`HW_TYPE_KCONFIG_DEPS` у tools/generate_ui.py:3244), тож без компонента вони
не компілюються.

## Ресурс плати (`can_buses`)

board.json оголошує піни й бітрейт; HAL зберігає це **лише як конфіг**
(`CanBusResource`, як `uart_buses`) — саму TWAI-периферію створює компонент і
володіє нею:

```json
"can_buses": [
  {"id": "can_0", "tx": 21, "rx": 22, "bitrate": 125000, "label": "CAN bus"}
]
```

| Поле | Значення |
|---|---|
| `id` | Hardware id шини. |
| `tx` / `rx` | GPIO до TXD/RXD трансивера. |
| `bitrate` | Біт за секунду; типово 125000. |

v1 бере **перший** запис шини (`hal.can_bus_at(0)`); структури даних готові
до мультишинних чипів (ESP32-P4), але сервіс їх поки не обходить. Плата без
`can_buses` — не помилка: сервіс ставить `can.status = "no_bus"` і лишається
неактивним.

## Формат кадрів (ModESP-CAN v1)

### 29-бітний arbitration ID

Кожен кадр має 29-бітний extended ID, який пакує
components/modesp_can/include/modesp/can/can_codec.h:63:

```
[28:26] prio (3 біти, 0 = найвищий)  події=2, стан=4, heartbeat=6
[25:24] kind (2 біти)                0=HEARTBEAT, 1=EVENT, 2=STATE, 3=CMD (зарезервовано)
[23:16] src  (8 біт)                 node-id відправника, 1..253 (0 і 254/255 зарезервовані)
[15:0]  id16 (16 біт)                key-id / event-id; для HEARTBEAT = magic 0x4D43 ("MC")
```

Правило CAN «два вузли не сміють слати один arbitration ID з різним payload»
виконується конструктивно: `src` — частина ID, тож кожен вузол володіє
власним простором ідентифікаторів.

### STATE (kind=2)

Retained-значення SharedState. Payload: байт `[0]` = тип значення (біти 0–3:
0=f32, 1=i32, 2=bool) плюс прапор VALID (біт 6); байти `[1..4]` — значення,
little-endian (bool — 1 байт, DLC=2; f32/i32 — DLC=5). VALID=0 дзеркалить
`false` у компаньйона `<key>_ok` джерела (наприклад, заморожене EMA в
`equipment`) — імпортер перетворює це на `is_healthy() = false`.
Надсилається on-change плюс повний refresh кожні 5 с (емуляція retained для
пізніх слухачів). **Рядкові ключі у v1 не експортуються** — вони не влазять у
8-байтний кадр.

### EVENT (kind=1)

Transient-подія: сирі 0..8 байт корисного навантаження за маніфестом. На RX
сервіс публікує `MsgRemoteEvent{node=src, event_id, len, data}` у **локальну**
шину повідомлень з main-loop task (шина не потокобезпечна — публікація з
чужих тасків заборонена). Перепубліковуються лише події, присутні у
згенерованому реєстрі; невідомі event-id рахуються й відкидаються.

### HEARTBEAT (kind=0, id16=0x4D43)

Надсилається кожні **1000 мс**. Payload: `[0]` версія протоколу (=1), `[1]`
прапори (біт 0 = вузол у safe mode; поки що завжди 0 — під'єднається до
interlock'ів продукту у фазі C5), `[2..3]` CRC-16 реєстру (діагностика
розбіжності прошивок у флоті; приймач записує, але поки не реагує), `[4..7]`
uptime у секундах (LE).

### CMD (kind=3)

Зарезервовано під адресний запис rw-ключів (v1.1). Прийняті CMD-кадри
ігноруються; у v1 команди передаються подіями.

## Стабільні ID (djb2-fold16)

`id16 = fold16(djb2(ім'я))`, де `fold16(h) = (h ^ (h >> 16)) & 0xFFFF`, а 0
відображається в 0x0001. Ключі стану хешуються від **повного імені ключа**
(`equipment.air_temp`), події — від `<module>.<event>`. Хеш-похідні ID
самоописні для гетерогенного флоту: дві різні прошивки виводять однаковий ID
для однакового імені без спільного файлу-реєстру.

Генератор перевіряє колізії в межах кожного kind (STATE і EVENT — окремі
простори ID завдяки kind-бітам) і **валить збирання** при зіткненні;
запасний вихід — явне поле `can_id` на конфліктному записі.

Існують дві реалізації хешу, і це критичні дзеркала wire-контракту:
`can_fold16` у tools/generate_ui.py:1097 (час збирання) та
`modesp::can::fold16` у
components/modesp_can/include/modesp/can/can_codec.h:93 (runtime — нею
драйвер-міст хешує `binding.address`). Спільні тест-вектори в
`tools/tests/test_can_registry.py` і `tests/host/test_can_codec.cpp` тримають
їх у синхроні — зміна будь-якої ламає сумісність із уже прошитими вузлами.

## План 1: експорт SharedState (`can_export`)

Позначити, що вузол транслює, — це правка маніфеста, а не коду. Згенерований
реєстр живлять два джерела (`collect_can_registry`,
tools/generate_ui.py:1118):

**(а) Ключ `state`** — додайте `can_export: true` (лише типи float/int/bool):

```json
"state": {
  "simple_thermo.setpoint": {"type": "float", "access": "readwrite", "can_export": true}
}
```

**(б) Роль у `requires[]`** — експортує runtime-ключ `equipment.<role>`; тип
кадру береться з `value_type` capability (у v1 — лише float/bool), тому явна
`capability` обов'язкова:

```json
"requires": [
  {"role": "air_temp", "type": "sensor", "capability": "temperature", "can_export": true}
]
```

Обидва приймають необов'язковий override `can_id`. Генератор пише
`generated/can_registry.h` — `CAN_EXPORT[]{key, id16, type}`, `CAN_EVENTS[]`,
`CAN_REGISTRY_CRC16` — і перезаписує його на кожному збиранні (навіть
порожнім, із sentinel-рядком), щоб застарілий header не пережив видалення
останнього запису. До 32 експорт-ключів на вузол (`MAX_EXPORT_KEYS`,
контролюється `static_assert`).

У runtime сервіс полить `SharedState::version()` щотакту (патерн
`MqttService`) і порівнює з кешем «останнє надіслане» на кожен ключ; зміна
дає STATE-кадр. Повний refresh-прохід стартує кожні 5 с і розкладається по
тактах курсором. TX обмежено бюджетом 4 кадри на такт (≤400 кадрів/с при
100 Гц головного циклу).

Імпорт значень чужого вузла — робота драйвера-моста: див.
[drivers/can_remote.md](../drivers/can_remote.md).

## План 2: події (`MsgRemoteEvent`)

Один симетричний конверт, `modesp/can/can_messages.h`
(`msg_id::CAN_REMOTE_EVENT` = 60):

```cpp
struct MsgRemoteEvent : etl::message<msg_id::CAN_REMOTE_EVENT> {
    uint8_t  node     = 0;    // 0 = "надіслати на шину"; інакше — вузол-джерело
    uint16_t event_id = 0;    // стабільний ID з generated/can_registry.h
    uint8_t  len      = 0;    // 0..8 байт корисного навантаження
    uint8_t  data[8]  = {};
};
```

- **TX:** модуль публікує конверт із `node == 0`. `CanService` ловить його в
  `on_message` і кодує EVENT-кадр — але лише якщо event_id оголошений
  `outbound` у реєстрі (whitelist); решта логується й відкидається.
- **RX:** прийнятий EVENT-кадр стає тим самим конвертом із `node == <src>`,
  опублікованим локально з main-loop task; споживачі фільтрують за
  `event_id`.

Події оголошуються у top-level блоці `can_events` маніфеста: імена `out`
пишуться **без крапок** (префікс `<module>.` додається автоматично), імена
`in` — повністю кваліфіковані `<module>.<event>` вузла-джерела:

```json
"can_events": {
  "out": {"defrost_started": {}},
  "in":  {"panel.button_pressed": {"payload": "u8"}}
}
```

`can_messages.h` навмисно **не** гейтиться `CONFIG_MODESP_CAN_ENABLE`: модуль
може публікувати конверт безумовно — без компонента в нього просто немає
підписника (нульова ціна).

## Node id

Кожному вузлу потрібна адреса 1..253. Порядок розв'язання
(can_service.cpp:124):

1. **NVS-override** — namespace `can`, ключ `node_id` (u8). Ставте його, коли
   дві плати зіткнулися адресами або коли потрібні стабільні, вручну обрані
   адреси.
2. **MAC-похідний дефолт** — `djb2(MAC) % 253 + 1`. Два пристрої однієї
   моделі плати отримують різні адреси без правки board.json (та сама
   філософія, що й у MQTT `device_id`). Колізія двох вузлів на ~250 значень
   можлива — саме для цього існує NVS-override.

board.json свідомо **не** несе node id: він описує *модель* плати, а два
пристрої однієї моделі мусять відрізнятися.

## Heartbeat і liveness

- Кожен вузол шле HEARTBEAT раз на **1 с**.
- Віддалений вузол вважається offline після **3,5 с** тиші (рахується
  будь-який кадр); перевірка liveness іде кожні 500 мс.
- Кожен трекнутий вузол отримує стан-ключ **`remote.<node-id>.online`**
  (bool) — доступний interlock'ам і сценаріям як звичайний ключ SharedState.
  Вузли з `remote_devices` трекаються від старту (починають offline);
  незнайомі вузли, почуті на шині, теж трекаються, доки не заповниться пул на
  16 слотів (заготовка discovery).
- Коли вузол зникає, **всі його закешовані підписки миттєво інвалідуються**,
  тож `is_healthy()` драйвера-моста падає одразу, не чекаючи `stale_ms`.

## Стан-ключі (`can.*`)

| Ключ | Тип | Значення |
|---|---|---|
| `can.status` | string | `ok`, `no_bus` (немає `can_buses` у board.json), `error` (init не вдався — вузол живий, CAN неактивний), `bus_off`, `recovering` (нода перестворена після bus-off; `ok` — лише після реального `tx_done`). |
| `can.node_id` | int | Розв'язана адреса цього вузла. |
| `can.tx_count` | int | Надіслані кадри. Тихий set (`track_change=false`) — без спаму WS-дельт. |
| `can.rx_count` | int | Прийняті кадри (лічильник ISR). Тихий. |
| `can.err_count` | int | Помилки шини. Тихий. |
| `can.rx_dropped` / `can.tx_dropped` | int | Переповнення RX-ring / TX-черги; публікуються лише коли ненульові. Тихі. |
| `can.bus_off` | bool | Активний стан bus-off. |
| `can.loopback_ok` | bool | Лише збірки з self-test: перший власний кадр пройшов повний цикл TX→RX. |
| `remote.<n>.online` | bool | Liveness віддаленого вузла `<n>` (див. вище). |

## Потокова модель

Усі чотири callback'и нового TWAI-драйвера виконуються в **ISR-контексті**, а
blocking-RX API не існує — тому компонент тримає жорсткий поділ:

- **RX:** `on_rx_done` копіює кадр (id + dlc + до 8 байт) у статичний
  SPSC-ring на 16 слотів — продюсер ISR, консюмер main loop. Повний ring
  дропає кадр і рахує це; ніщо не блокується.
- **TX:** черга драйвера **zero-copy** — вона зберігає вказівники — тож кадри
  живуть у статичному пулі на 12 слотів, доки `on_tx_done` не звільнить слот.
  `twai_node_transmit(..., 0)` неблокуючий; повна черга інкрементує лічильник
  замість очікування (main loop не сміє блокуватись: TWDT 5 с із
  `trigger_panic`).
- **Помилки:** `on_error` / `on_state_change` лише інкрементують атомарні
  лічильники та прапори. Уся справжня робота — декод, `state_set`, `publish`,
  відновлення після bus-off — відбувається в `on_update` на main-loop task.
  Відновлення **перестворює TWAI-ноду** після backoff'у 1 с (драйвер IDF не
  викликає `tx_done` для кадру, перерваного bus-off, тож teardown ноди —
  єдиний безпечний спосіб повернути TX-пул); статус іде `bus_off` →
  `recovering` → `ok`, де `ok` вимагає реального `tx_done` на новій ноді —
  stuck-dominant шина циклитиме `bus_off`/`recovering`, а не брехатиме `ok`.
- **Невдалий init не фатальний** (підтверджений патерн-вбивця боту з рев'ю
  2026-07-07): будь-який шлях помилки ставить `can.status` і повертає `true`,
  тож пристрій вантажиться і решта прошивки працює; CAN просто лишається
  неактивним.

Нуль heap у hot-path: ring, TX-пул, кеш підписок і таблиця вузлів — статики
фіксованого розміру.

Software-фільтрація: ESP32/S3 мають один mask-фільтр, яким не виразити
«heartbeat усіх + мої ключі + мої події». v1 приймає всі extended-кадри й
диспетчеризує програмно — на 250 kbps це щонайбільше ~2000 кадрів/с з
десятками байт роботи на кожен. Апаратний фільтр — оптимізація на потім, якщо
виміри колись цього вимагатимуть.

## Self-test (`CONFIG_MODESP_CAN_SELF_TEST`)

Вмикає прапори ноди `enable_self_test` + `enable_loopback`: власні кадри
повертаються **без ACK**, тож повний шлях TX→RX (кодування → передача → ISR →
ring → декодування) перевіряється на **одному** стендовому пристрої без
трансивера і без другого вузла. `can.loopback_ok` стає `true` після
повернення першого власного кадру.

> ⚠️ **Увага:** ніколи не вмикайте self-test на робочій шині — передача без
> ACK порушує семантику CAN для всіх інших вузлів.

## Обмеження v1

Чесний список того, чого *немає*:

- **Немає рядкових ключів** — кадри по 8 байт; фрагментація — окреме рішення.
- **Немає CMD-кадрів** — kind=3 зарезервовано; адресні rw-записи — v1.1.
- **Software-фільтрація** — CPU-податок на завантаженій шині (див. вище).
- **Лише перша шина** — мультишинні чипи готові на рівні даних, але не
  обслуговуються.
- **Немає інтеропу з чужими пристроями** — це свідомо протокол гомогенного
  флоту; реалізація CANopen/Cyphal може пізніше замінити компонент за тим
  самим швом драйвера-моста (фаза C6 в ADR-004).
- **CRC реєстру — лише діагностика** — їде в heartbeat'ах, але приймачі його
  поки не порівнюють і не реагують.

## Що далі

Фази C3+ з ADR-004 потребують двох фізичних вузлів із трансиверами й ще
попереду: дзеркалення стану та liveness на реальній шині (C3), події
end-to-end (C3b), роль через драйвер-міст end-to-end плюс сторінка пристроїв
WebUI для CAN (C4), liveness в interlock'ах продукту та discovery (C5).

- **[drivers/can_remote.md](../drivers/can_remote.md)** — драйвер-міст, що
  перетворює ключ віддаленого вузла на локальну роль.
- **[04-hardware/board-config.md](../../04-hardware/board-config.md)** —
  секції `can_buses` і `remote_devices`.
- **[ADR-004](../../../../docs/can/ADR-004-can-transport-p6.md)** — повне
  архітектурне рішення, відхилення від зовнішнього дизайн-документа, відкриті
  питання.

## Джерела

- [`components/modesp_can/include/modesp/can/can_codec.h`](../../../../components/modesp_can/include/modesp/can/can_codec.h) — чистий, host-тестований кодек кадрів.
- [`components/modesp_can/include/modesp/can/can_service.h`](../../../../components/modesp_can/include/modesp/can/can_service.h) / [`src/can_service.cpp`](../../../../components/modesp_can/src/can_service.cpp) — сервіс.
- [`components/modesp_can/include/modesp/can/can_remote_cache.h`](../../../../components/modesp_can/include/modesp/can/can_remote_cache.h) / [`src/can_remote_cache.cpp`](../../../../components/modesp_can/src/can_remote_cache.cpp) — кеш віддалених вузлів.
- [`components/modesp_can/include/modesp/can/can_messages.h`](../../../../components/modesp_can/include/modesp/can/can_messages.h) — конверт `MsgRemoteEvent`.
- [`components/modesp_can/Kconfig`](../../../../components/modesp_can/Kconfig) / [`CMakeLists.txt`](../../../../components/modesp_can/CMakeLists.txt)
- `tools/generate_ui.py` — `collect_can_registry` / `generate_can_registry_header` / `can_fold16`.
