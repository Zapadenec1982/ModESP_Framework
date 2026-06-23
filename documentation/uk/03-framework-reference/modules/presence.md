# `presence` — модуль присутності (occupancy)

> 📖 **In English:** [documentation/en/03-framework-reference/modules/presence.md](../../../en/03-framework-reference/modules/presence.md)

Бізнес-модуль присутності/occupancy поверх mmWave-радара [`ld2410b`](../drivers/ld2410b.md). **Чистий споживач SharedState** (як [`simple_thermo`](simple_thermo.md)): драйвер сам не чіпає — читає `equipment.presence` (що публікує EquipmentBase з прив'язаного радара), застосовує логіку й віддає `presence.*` у веб, MQTT і DataLogger.

## Потік даних

```
ld2410b ──▶ EquipmentBase ──▶ equipment.presence / equipment.move_distance / equipment.still_distance
                                          │
                                  presence-модуль
              (enable → zone-gate(max_distance) → occupancy-hold(hold_sec))
                                          │
                          presence.detected / presence.state / presence.*
                                          │
                              веб «Присутність» · OSD · MQTT · DataLogger(event 40)
```

Очікувані ролі у bindings (драйвер `ld2410b`): `presence` (обовʼязково), опційно `move_distance` (address=moving) і `still_distance` (address=static) — для класифікації рух/статика та софт-фільтра дистанції.

## Налаштування (web, зберігаються)

Оголошені як module state-ключі (`access: readwrite`, `persist: true`) — генератор зашиває їх у веб/NVS/MQTT автоматично:

| Ключ | Тип | Деф. | Опис |
|---|---|---|---|
| `presence.enabled` | bool | true | Увімк/вимк детекцію. |
| `presence.hold_sec` | int 0-300 c | 5 | Occupancy-утримання після зникнення цілі (0 = миттєво). |
| `presence.max_distance` | int 0-800 см | 600 | **Ігнорувати цілі далі** (cm-точний, крок 10 см; 0 = вимкнено). Потребує прив'язаного distance-каналу. |

## Індикація (read-only стан)

| Ключ | Опис |
|---|---|
| `presence.detected` | Присутність (після гейтингу + утримання) — основний сигнал. |
| `presence.state` | `disabled`/`none`/`present`/`moving`/`static`/`both`. |
| `presence.moving_distance` / `presence.static_distance` | Дистанції, см. |
| `presence.idle_sec` | Секунд без присутності. |
| `presence.sensor_ok` | Справність датчика (з `equipment.presence_ok`). |

> **Споживачам:** беріть **`presence.detected`** (гейтнутий), а не сирий `equipment.presence` (повний діапазон, ігнорує `max_distance`).

## Логіка

`enabled && sensor_ok && raw && !gated` → з occupancy-утриманням `hold_sec`. `gated` = найближча ціль далі `max_distance` (коли прив'язаний distance-канал). Гейт на `sensor_ok` не дає присутності залипнути при відключенні датчика.

## MQTT / DataLogger

Публікує `presence.detected/state/moving_distance/idle_sec`; підписка на `enabled/hold_sec/max_distance`. DataLogger-подія `presence.detected` (id 40, both-edge).

## Опційність

Модуль у `project.json` → компілюється завжди (легкий, інертний без радара). Сам **драйвер** опційний через `CONFIG_MODESP_DRIVER_LD2410B` — див. [`drivers/ld2410b.md`](../drivers/ld2410b.md#опційність-kconfig).

## Джерела

- [`modules/presence/manifest.json`](../../../../modules/presence/manifest.json)
- [`modules/presence/src/presence_module.cpp`](../../../../modules/presence/src/presence_module.cpp)
- [`drivers/ld2410b.md`](../drivers/ld2410b.md) — драйвер радара.
