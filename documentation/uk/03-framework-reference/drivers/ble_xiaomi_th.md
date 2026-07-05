# `ble_xiaomi_th` — BLE-датчик температури/вологості/заряду Xiaomi

> 📖 **In English:** [.../en/.../ble_xiaomi_th](../../../en/03-framework-reference/drivers/ble_xiaomi_th.md)

Сенсорний драйвер для гігро-термометра Xiaomi LYWSD03MMC, перепрошитого кастомним
firmware (pvvx / ATC / BTHome). Драйвер пасивно слухає BLE-рекламу через
**OBSERVER** спільного хоста `modesp_ble` — з'єднання не встановлюється. Один
фізичний датчик дає три канали (температура, вологість, заряд), кожен зі своєю
`capability`; канал обирається полем `address` у прив'язці (R3.5). Драйвер прив'язується до модуля `equipment` і
публікує `equipment.room_temp` / `equipment.room_humid` / `equipment.room_batt`
(+ прапорці справності `equipment.<role>_ok`).

**Формат даних належить драйверу.** На етапі factory драйвер реєструє свої
декодери реклами (pvvx/ATC `0x181A`, BTHome `0xFCD2`) у `modesp_ble` через
`adv_decoder.h`. Транспорт володіє лише радіо та пасивним скануванням — формату
пристрою він не знає — і віддає кожен кадр service-data зареєстрованим декодерам.
Декодер, що впізнав свій формат, публікує показник (`ble::report_sensor`) у кеш за
MAC, який читає драйвер.

`hardware_type` — `ble` (поле `transport: "ble"`). Роль прив'язується за
**capability** (R0.1/R3.1) — не за драйвером і не за MAC. Ідентичність (MAC) живе
на **пристрої** у board.json/devices.json, ніколи на ролі (R0.3); factory резолвить
device `id`→MAC через `find_ble_device` (аліас `find_remote_device`). Драйвер
залежить від компонента `modesp_ble`, який має бути ввімкнений
(`CONFIG_MODESP_BLE_ENABLE`).

## Залізо

| Параметр | Значення |
|----------|----------|
| Пристрій | Xiaomi LYWSD03MMC (гігро-термометр) |
| Firmware | pvvx / ATC / BTHome (кастомний) |
| Транспорт | BLE OBSERVER, пасивний broadcast (без з'єднання) |
| UUID реклами | BTHome `0xFCD2`, pvvx/ATC `0x181A` |
| Ідентичність | MAC на пристрої (board.json/devices.json), не на ролі (R0.3) |
| Stale timeout | `stale_ms`, дефолт 60000 мс (без broadcast → датчик не healthy) |

## Канали

Один фізичний датчик → три канали, кожен зі своєю `capability`. Канал обирається
полем `address` у прив'язці. Генератор виводить канали в `role.channels_by_driver`;
`<select>` каналу з'являється лише за 2+ каналів однієї capability, інакше єдиний
авто-прив'язується (R3.5).

| `address` | `capability` | Роль (приклад) | State key |
|-----------|--------------|----------------|-----------|
| `temperature` | `temperature` | `room_temp` | `equipment.room_temp` |
| `humidity` | `humidity` | `room_humid` | `equipment.room_humid` |
| `battery` | `battery` | `room_batt` | `equipment.room_batt` |

Для кожної ролі публікується прапорець справності `equipment.<role>_ok`. Якщо
протягом 60 с не надходить жодного broadcast, датчик вважається несправним.

## Прив'язки

### board.json

Запис у `ble_devices` потребує `id`, `mac` та `format`. `ble_devices` — legacy-аліас
транспорт-генеричного ключа `remote_devices` (R4.1); `mac` тут і є `identity` пристрою.
`format: "auto"` автоматично визначає тип firmware (pvvx / ATC / BTHome).

```json
"ble_devices": [
  { "id": "xiaomi_room", "mac": "a4:c1:38:b4:dc:11", "format": "auto" }
]
```

### bindings.json

Один запис на канал — три прив'язки одного й того ж `hardware` з різними
`address` / `role`:

```json
[
  { "hardware": "xiaomi_room", "driver": "ble_xiaomi_th", "role": "room_temp",  "module": "equipment", "address": "temperature" },
  { "hardware": "xiaomi_room", "driver": "ble_xiaomi_th", "role": "room_humid", "module": "equipment", "address": "humidity" },
  { "hardware": "xiaomi_room", "driver": "ble_xiaomi_th", "role": "room_batt",  "module": "equipment", "address": "battery" }
]
```

## Архітектура

BLE-хост `modesp_ble` — спільна інфраструктура (один NimBLE-host, співіснує з
Wi-Fi). Цей драйвер — незалежна фіча, що їде на ролі **OBSERVER**: хост запускає
пасивне сканування і роздає кожен кадр service-data декодерам, які драйвер
зареєстрував (`adv_decoder.h`). Байтовий розбір (BTHome `0xFCD2`, pvvx/ATC
`0x181A`) живе **у драйвері**; впізнаний показник кешується за MAC. Модулі ніколи
не звертаються до BLE напряму — I/O робить драйвер, модуль `equipment` лише читає
SharedState (`equipment.room_*`).
