# `modesp_ble` — спільний BLE-хост (observer + central + peripheral)

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_ble.md](../../../en/03-framework-reference/components/modesp_ble.md)

`modesp_ble` — спільна BLE-інфраструктура для всієї прошивки. Один NimBLE-хост, що співіснує з Wi-Fi (coexist), і три **одночасні** ролі: пасивне сканування реклами сенсорів (observer), підключення до пристрою з записом команд (central) і власний GATT-сервер з рекламою (peripheral). Компонент живе в `components/modesp_ble` і реєструється в `main.cpp` як сервіс `BaseModule` з пріоритетом 1 — **не** в `modules/`. Окремі фічі (Xiaomi-сенсор і LED-панель iPixel) їдуть поверх цього хоста незалежно одна від одної.

ЗАЛЕЖНОСТІ (PRIV_REQUIRES): `bt`, `esp_coex`, `nvs_flash`. `main/CMakeLists` лінкує `idf::modesp_ble`, коли увімкнено `CONFIG_MODESP_BLE_ENABLE`.

## Три ролі

| Роль | Призначення |
|---|---|
| **OBSERVER** | Пасивне сканування. Парсить рекламу сенсорів (BTHome uuid `0xFCD2`, pvvx/ATC uuid `0x181A`) і живить BLE-сенсорні драйвери (напр. `ble_xiaomi_th`) за MAC. |
| **CENTRAL** | Підключається до пристрою (LED-панель iPixel) і пише команди. Доступна як singleton `BlePanel`. Підключення **ставить на паузу** сканування observer, потім **відновлює** його. |
| **PERIPHERAL** | Власний GATT-сервер (телеметрія/керування + Wi-Fi provisioning), рекламує ім'я `"ModESP"`. |

## Kconfig

Меню **«ModESP BLE»**:

| Опція | Опис |
|---|---|
| `CONFIG_MODESP_BLE_ENABLE` | Майстер-перемикач (вмикає компонент і лінк у `main`). |
| `CONFIG_MODESP_BLE_CENTRAL` | Central — підключення до панелі. |
| `CONFIG_MODESP_BLE_PROVISIONING` | Wi-Fi provisioning через peripheral GATT. |

## `BlePanel` — central API

Singleton, через який драйвери/модулі керують підключеним пристроєм. Драйвер `ble_led_panel` володіє лише BLE-лінком (ціллю підключення); модуль `panel` — єдиний писар живлення, яскравості, ефекту й контенту через `BlePanel` — розв'язка йде саме через цей singleton.

```cpp
void    set_target(const char* adv_name_prefix);   // префікс adv-name для підключення
bool    is_connected();
void    write_cmd(const uint8_t* data, size_t len, bool with_response);
void    show_text(const char* s,
                  uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
                  uint8_t anim = 0, uint8_t speed = 0x32, uint8_t rainbow = 0);
```

`set_target()` задає префікс рекламного імені; central сканує його, підключається й відкриває control char. `show_text()` рендерить нативний TEXT-кадр iPixel (гліфи + колір + анімація — деталі див. у модулі `panel`). Байтовий протокол керування панеллю описано в [`docs/ble/panel_protocol.md`](../../../../docs/ble/panel_protocol.md).

## Фічі поверх хоста

`modesp_ble` — це лише інфраструктура; конкретні пристрої під'єднуються через драйвери з `hardware_type: "ble_device"`. Модулі **ніколи** не чіпають BLE/GPIO напряму — I/O роблять драйвери, модулі читають/пишуть SharedState.

| Фіча | Роль хоста | Драйвер / модуль |
|---|---|---|
| Xiaomi гігро-термо-сенсор | OBSERVER (без підключення, пасивний broadcast) | [`ble_xiaomi_th`](../drivers/ble_xiaomi_th.md) (sensor) |
| LED-панель iPixel | CENTRAL (connect) + контент | [`ble_led_panel`](../drivers/ble_led_panel.md) (actuator) + модуль [`panel`](../modules/panel.md) |

### Observer → сенсор

`ble_xiaomi_th` читає Xiaomi LYWSD03MMC з кастомною прошивкою (pvvx/ATC/BTHome) **пасивно** — observer ловить broadcast за MAC і роздає драйверу. Один фізичний сенсор → 3 канали (temperature / humidity / battery), які прив'язка обирає полем `address`. Публікується в `equipment.room_temp` / `equipment.room_humid` / `equipment.room_batt` (+ health-прапорці `equipment.<role>_ok`). Stale-timeout 60 c: немає broadcast → не healthy.

### Central → панель

`ble_led_panel` (actuator) — це **connect**-пристрій, що матчиться за **adv-name**, не за MAC. Драйвер володіє лише BLE-лінком (ціллю підключення); живлення (ON/OFF), яскравість (0..100 %), ефект і відображуваний контент належать **модулю** `panel`, який є єдиним писарем через `BlePanel`. Декомпозиція йде через singleton `BlePanel`: драйвер відповідає за транспорт, модуль — за керування й «що показати».

## Що далі

- [`ble_xiaomi_th.md`](../drivers/ble_xiaomi_th.md) — пасивний BLE-сенсор (observer).
- [`ble_led_panel.md`](../drivers/ble_led_panel.md) — драйвер LED-панелі iPixel (central transport).
- [`panel.md`](../modules/panel.md) — модуль контенту панелі (гліфи, іконки, кольори, анімація).

## Джерела

- [`components/modesp_ble`](../../../../components/modesp_ble)
- [`docs/ble/panel_protocol.md`](../../../../docs/ble/panel_protocol.md)
