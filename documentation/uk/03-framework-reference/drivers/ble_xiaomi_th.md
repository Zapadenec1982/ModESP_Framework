# `ble_xiaomi_th` — BLE-датчик температури/вологості/заряду Xiaomi

> 📖 **In English:** [.../en/.../ble_xiaomi_th](../../../en/03-framework-reference/drivers/ble_xiaomi_th.md)

Сенсорний драйвер для гігро-термометра Xiaomi LYWSD03MMC, перепрошитого кастомним
firmware (pvvx / ATC / BTHome). Драйвер пасивно слухає BLE-рекламу через
**OBSERVER** спільного хоста `modesp_ble` — з'єднання не встановлюється. Один
фізичний датчик дає три канали (температура, вологість, заряд), які обираються
полем `address` у прив'язці. Драйвер прив'язується до модуля `equipment` і
публікує `equipment.room_temp` / `equipment.room_humid` / `equipment.room_batt`
(+ прапорці справності `equipment.<role>_ok`).

`hardware_type` — `ble_device`. Прив'язка за **MAC**. Драйвер залежить від
компонента `modesp_ble`, який має бути ввімкнений (`CONFIG_MODESP_BLE_ENABLE`).

## Залізо

| Параметр | Значення |
|----------|----------|
| Пристрій | Xiaomi LYWSD03MMC (гігро-термометр) |
| Firmware | pvvx / ATC / BTHome (кастомний) |
| Транспорт | BLE OBSERVER, пасивний broadcast (без з'єднання) |
| UUID реклами | BTHome `0xFCD2`, pvvx/ATC `0x181A` |
| Прив'язка | за MAC-адресою |
| Stale timeout | 60 с (без broadcast → датчик не healthy) |

## Канали

Один фізичний датчик → три канали. Канал обирається полем `address` у прив'язці.

| `address` | Роль (приклад) | State key |
|-----------|----------------|-----------|
| `temperature` | `room_temp` | `equipment.room_temp` |
| `humidity` | `room_humid` | `equipment.room_humid` |
| `battery` | `room_batt` | `equipment.room_batt` |

Для кожної ролі публікується прапорець справності `equipment.<role>_ok`. Якщо
протягом 60 с не надходить жодного broadcast, датчик вважається несправним.

## Прив'язки

### board.json

Запис у `ble_devices` потребує `id`, `mac` та `format`. `format: "auto"`
автоматично визначає тип firmware (pvvx / ATC / BTHome).

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
Wi-Fi). Цей драйвер — незалежна фіча, що їде на ролі **OBSERVER**: хост парсить
рекламні пакети сенсорів (BTHome `0xFCD2`, pvvx/ATC `0x181A`) і за MAC передає їх
драйверу. Модулі ніколи не звертаються до BLE напряму — I/O робить драйвер,
модуль `equipment` лише читає SharedState (`equipment.room_*`).
