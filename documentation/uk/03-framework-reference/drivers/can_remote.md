# `can_remote` — віддалений ключ ModESP-вузла по CAN

> 📖 **In English:** [documentation/en/03-framework-reference/drivers/can_remote.md](../../../en/03-framework-reference/drivers/can_remote.md)

Драйвер-міст для CAN-транспорту: він робить ключ SharedState,
**експортований іншим ModESP-вузлом** (ModESP-CAN v1,
[ADR-004](../../../../docs/can/ADR-004-can-transport-p6.md)), схожим на
звичайний локальний сенсор. Сам драйвер не робить жодного I/O на шині —
компонент [`modesp_can`](../components/modesp_can.md) приймає STATE-кадри й
наповнює `CanRemoteCache`; драйвер лише підписується на одну пару
`(вузол, ключ)` на етапі factory і читає закешоване значення в `read()`.
Модуль-споживач прив'язує роль за capability і ніколи не дізнається, що
значення приїхало дротом.

Драйвер реєструється як `sensor` із `hardware_type: can`,
`transport: "can"`, `requires_address: true` і `multiple_per_bus: true`. Один
екземпляр драйвера на кожен прив'язаний віддалений ключ; один віддалений
**пристрій** (вузол) живить стільки ролей, скільки прив'яжете — по одному
binding'у на ключ. Це CAN-дзеркало [`ble_xiaomi_th`](ble_xiaomi_th.md):
identity живе на пристрої, а `address` прив'язки обирає, *що саме* з цього
пристрою живить роль.

REQUIRES: компонент `modesp_can` (`CONFIG_MODESP_CAN_ENABLE`). Згенерований
toggle драйвера несе `depends on MODESP_CAN_ENABLE`, тож драйвер автоматично
вимикається разом із компонентом.

## Ресурс плати

Віддалений вузол — не дротовий ресурс, а рядок у транспорт-генеричній секції
`remote_devices` (заводський seed у board.json або доданий у runtime через
`/data/devices.json`; ConfigService зливає обидва):

```json
"remote_devices": [
  {"id": "node_kitchen", "transport": "can", "identity": "12", "name": "Kitchen controller"}
]
```

| Поле | Значення |
|---|---|
| `id` | Логічне ім'я пристрою, на яке посилається `hardware` у binding'ах — ніколи не сама адреса (R0.3). |
| `transport` | Мусить бути `"can"` — усе інше factory відхиляє. |
| `identity` | **Node-id віддаленого вузла, 1..253**, рядком. Парситься і десяткове (`"12"`), і шістнадцяткове (`"0x0C"`). |
| `name` | Людська назва для UI; необов'язкова. |

Локальному вузлу додатково потрібні запис `can_buses` (піни + бітрейт) і
CAN-трансивер — див.
[components/modesp_can.md](../components/modesp_can.md). Кожен вузол зі
списку `remote_devices` із `transport: "can"` трекається на liveness від
старту й отримує стан-ключ `remote.<node-id>.online`.

## Прив'язки

`address` називає **віддалений ключ** — повне ім'я ключа SharedState так, як
його експортує вузол-*джерело* (`can_export` у його маніфесті). Key-id на
шині — це `fold16(djb2(address))`, порахований тим самим хешем, що й у
генератора вузла-джерела, тож збіг відбувається без спільного файлу-реєстру:

```json
{
  "manifest_version": 1,
  "bindings": [
    {"hardware": "node_kitchen", "driver": "can_remote", "role": "kitchen_temp", "address": "equipment.air_temp",  "module": "equipment"},
    {"hardware": "node_kitchen", "driver": "can_remote", "role": "kitchen_door", "address": "equipment.door_open", "module": "equipment"}
  ]
}
```

Якщо вузол-джерело оголосив ключ з явним override'ом `can_id`, хеш імені вже
не збігатиметься — передайте сам id у hex-формі:

```json
{"hardware": "node_kitchen", "driver": "can_remote", "role": "kitchen_temp", "address": "0x1234", "module": "equipment"}
```

Прив'язаний до модуля `equipment`, кожна роль публікує звичну пару:

- `equipment.kitchen_temp` (float) — віддалене значення;
- `equipment.kitchen_temp_ok` (bool) — прапор справності, керований
  `is_healthy()` нижче.

## Налаштування

| Ключ | Тип | Типово | Примітки |
|---|---|---|---|
| `stale_ms` | int | 5000 | Без свіжого STATE-кадру довше — значення несправне. Діапазон 500–600000, крок 500. Зберігається. |

## Provides

`{"type": "float"}` — значення віддаленого ключа, публікується у
`equipment.<role>`. Шина несе три типи; `read()` зводить їх до одного: f32
проходить як є, i32 приводиться до float, bool стає `0.0` / `1.0`.

## Шаблон: споживання бізнес-модулем

Споживач читає ключ ролі точно так само, як для локального сенсора — спершу
health-гейт, потім дія. Для жорстких interlock'ів можна додатково гейтитись
на ключі liveness вузла:

```cpp
// у вашому BaseModule::on_update:
float t;
bool  t_ok = state.get_or("equipment.kitchen_temp_ok", false);
if (t_ok && state.get("equipment.kitchen_temp", t)) {
    control_loop(t);
} else {
    fail_safe();   // вузол offline, кадр застарів або джерело позначило його невалідним
}

// необов'язковий interlock на весь вузол (node-id 12 у цьому прикладі):
bool kitchen_online = state.get_or("remote.12.online", false);
```

Ніщо у споживачі не згадує CAN, node-id чи ім'я віддаленого ключа —
перемкніть binding на локальний сенсор `ntc`, і модуль працює без змін.

## Як це працює

На етапі factory (старт, drivers/can_remote/src/can_remote_driver.cpp:65):

1. `hal.find_remote_device(hardware)` знаходить рядок пристрою; factory
   перевіряє `transport == "can"` і парсить `identity` як node-id 1..253.
2. `address` стає 16-бітним key-id: `fold16(djb2(address))` або літеральне
   значення для hex-форми `0x....`.
3. `CanRemoteCache::instance().subscribe(node, key_id)` повертає вказівник на
   слот кешу (ідемпотентно для тієї самої пари); драйвер його зберігає.

У runtime драйвер пасивний — `update()` нічого не робить. `CanService`
дренує прийняті STATE-кадри на main-loop task і освіжає слот кешу (значення,
біт VALID, часова мітка). `read()` повертає закешоване значення;
`is_healthy()` істинний, лише коли виконуються **всі три** умови:

- останній кадр ніс VALID=1 (`<key>_ok` джерела був true);
- кадр свіжіший за `stale_ms`;
- вузол online (heartbeat протягом 3,5 с).

Коли вузол зникає за таймаутом, кеш миттєво інвалідує всі його підписки,
тож справність падає одразу, не чекаючи `stale_ms`. Усе виконується на
єдиному main-loop task — без локів і без heap.

Очікування щодо свіжості: джерело шле on-change **плюс** повний refresh
кожні 5 с, тож справний, але «мовчазний» ключ перепідтверджується щонайменше
раз на 5 с.

## Виявлення

Поки немає. Node-id та імена віддалених ключів оголошуються вручну; ані
сканування CAN-шини, ані браузера вузлів у WebUI не існує (заплановано як
фаза C5 в ADR-004 — анонс експорт-таблиці + сторінка пристроїв). Драйвер
також поки не декларує per-канальну `capability`, тож матчер ролей падає у
fallback за типом (відкрите питання 10.3 в ADR-004).

## Типові помилки

**Роль ніколи не стає справною:** вузол-джерело мусить реально експортувати
ключ — `can_export: true` на стан-ключі (або ролі `requires[]`) у *його*
маніфесті, і його прошивка перезібрана після цього. Перевірте також
`remote.<n>.online`: якщо він ніколи не стає `true`, проблема в
шині/heartbeat'ах, а не в ключі.

**`address` мусить збігатися з експортованим ім'ям точно:** збіг — це хеш
повного рядка ключа (`equipment.air_temp`), тож одрук чи скорочене ім'я
мовчки підписує на key-id, який ніхто не передає.

**Ключ джерела має override `can_id`:** хеш імені більше не збігається.
Використайте hex-форму — `"address": "0x1234"` — з тим самим id, який
оголошує маніфест джерела.

**`identity` — не node-id:** MAC-рядок в `identity` — звичка з BLE; для CAN
він мусить парситись як ціле 1..253, інакше factory відмовить binding'у на
старті (шукайте помилки `can_remote` у лозі).

**`stale_ms` менше 5000 «миготить»:** ключ, чиє значення не змінюється,
перенадсилається лише повним refresh'ем раз на 5 с, тож жорсткіший
`stale_ms` змушує справність коливатися між refresh'ами. Зменшуйте його лише
для ключів, які точно змінюються (а отже й перенадсилаються) частіше.

**Пули скінченні:** 8 екземплярів драйвера (`MAX_CAN_REMOTE`) і 16 підписок
кешу (`CanRemoteCache::MAX_SUBSCRIPTIONS`). Коли вони вичерпані, factory
повертає `nullptr` з рядком "pool exhausted" у лозі.

## UI

Одна картка на binding на сторінці сервісного рівня **«Налаштування
датчиків»** (`page_id: driver_settings`): "CAN remote: {{hardware_id}}" з
`number_input` для `stale_ms`.

## Що далі

Цей драйвер — скелет фаз C2/C4 з ADR-004: він перевірений на одному стенді
через loopback self-test компонента, але двовузловий end-to-end прогін (фази
C3/C4 — реальна шина, трансивери, сторінка пристроїв WebUI для CAN) ще
попереду.

- **[components/modesp_can.md](../components/modesp_can.md)** —
  транспортний компонент: формат кадрів, heartbeat/liveness, `can_export`,
  Kconfig.
- **[04-hardware/board-config.md](../../04-hardware/board-config.md)** —
  секції `remote_devices` і `can_buses`.
- **[ble_xiaomi_th.md](ble_xiaomi_th.md)** — BLE-драйвер-міст, який цей
  драйвер дзеркалить (той самий патерн пристрій/identity/address, інший
  транспорт).
- **[writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md)**
  — посібник зі створення драйверів (factory + реєстрація).

## Джерела

- [`drivers/can_remote/manifest.json`](../../../../drivers/can_remote/manifest.json)
- [`drivers/can_remote/include/can_remote_driver.h`](../../../../drivers/can_remote/include/can_remote_driver.h)
- [`drivers/can_remote/src/can_remote_driver.cpp`](../../../../drivers/can_remote/src/can_remote_driver.cpp)
