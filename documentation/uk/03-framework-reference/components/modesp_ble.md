# `modesp_ble` — спільний BLE-хост (observer + central + peripheral)

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_ble.md](../../../en/03-framework-reference/components/modesp_ble.md)

`modesp_ble` — спільна BLE-інфраструктура для всієї прошивки. Один NimBLE-хост, що співіснує з Wi-Fi (coexist), і три **одночасні** ролі: пасивне сканування реклами сенсорів (observer), підключення до пристрою з записом команд (central) і власний GATT-сервер з рекламою (peripheral). Компонент живе в `components/modesp_ble` і реєструється в `main.cpp` як сервіс `BaseModule` з пріоритетом 1 — **не** в `modules/`. Окремі фічі (Xiaomi-сенсор і LED-панель iPixel) їдуть поверх цього хоста незалежно одна від одної.

ЗАЛЕЖНОСТІ (PRIV_REQUIRES): `bt`, `esp_coex`, `nvs_flash`. `main/CMakeLists` лінкує `idf::modesp_ble`, коли увімкнено `CONFIG_MODESP_BLE_ENABLE`.

## Три ролі

| Роль | Призначення |
|---|---|
| **OBSERVER** | Пасивне сканування. Роздає кожен кадр 16-бітного service-data декодерам, які реєструють BLE-сенсорні драйвери (`adv_decoder.h`); впізнаний показник кешується **за MAC** для прив'язаного драйвера. Транспорт формату пристрою не знає — самі декодери (напр. BTHome `0xFCD2`, pvvx/ATC `0x181A`) живуть у драйвері, напр. `ble_xiaomi_th`. |
| **CENTRAL** | Підключається до пристрою, пише/підписується на його GATT-характеристики. Connect-драйвер реєструє `ConnectProfile` (adv-name + write/notify UUID) через `central_link.h` і отримує генеричний `ICentralLink` — транспорт формату пристрою не знає. Підключення **ставить на паузу** сканування observer, потім **відновлює** його. |
| **PERIPHERAL** | Власний GATT-сервер (телеметрія/керування + Wi-Fi provisioning), рекламує ім'я `"ModESP"`. |

## Kconfig

Меню **«ModESP BLE»**:

| Опція | Опис |
|---|---|
| `CONFIG_MODESP_BLE_ENABLE` | Майстер-перемикач (вмикає компонент і лінк у `main`). |
| `CONFIG_MODESP_BLE_CENTRAL` | Central — підключення до панелі. |
| `CONFIG_MODESP_BLE_PROVISIONING` | Wi-Fi provisioning через peripheral GATT. |

## Central link (`central_link.h` seam)

Central-роль генерична: транспорт володіє радіо й машиною connect/discover/write, але
знань про пристрій не має. Connect-драйвер реєструє профіль на етапі factory
(ідемпотентно) і пише через повернений лінк:

```cpp
struct ConnectProfile {                 // знання про пристрій дає драйвер (це ДАНІ)
    const char*       name_prefix;      // префікс adv-name для скан+connect
    const ble_uuid_t* write_uuid;       // характеристика → write handle
    const ble_uuid_t* notify_uuid;      // підписка (CCCD); nullptr = лише write
    CentralNotifyCb   on_notify; void* ctx;   // приймач notify (host task); nullptr ок
};
ICentralLink* register_connect_profile(const ConnectProfile&);

class ICentralLink {
    bool connected() const;
    bool write(const uint8_t* data, uint16_t len, bool with_response);
    bool write_frame(bool (*body)(ICentralLink*, void*), void* arg);   // атомарний багатозапис
};
```

`write_frame` тримає recursive write-mutex транспорту через увесь `body`, тож драйвер
будує **й ріже на чанки** багатозаписний кадр (напр. текст/зображення) атомарно проти
control-записів інших писачів. Усе байтове кодування, GATT UUID і шрифт живуть у драйвері —
це connect-аналог `adv_decoder.h` (observer-сенсори). Модуль `panel` керує контентом через
драйверний `IPanelPort` (резолв через `DriverRegistry::panel_port()`), не торкаючись BLE.
Байтовий протокол панелі: [`docs/ble/panel_protocol.md`](../../../../docs/ble/panel_protocol.md).

## Фічі поверх хоста

`modesp_ble` — це лише інфраструктура; конкретні пристрої під'єднуються через драйвери з `hardware_type: "ble_device"`. Модулі **ніколи** не чіпають BLE/GPIO напряму — I/O роблять драйвери, модулі читають/пишуть SharedState.

| Фіча | Роль хоста | Драйвер / модуль |
|---|---|---|
| Xiaomi гігро-термо-сенсор | OBSERVER (без підключення, пасивний broadcast) | [`ble_xiaomi_th`](../drivers/ble_xiaomi_th.md) (sensor) |
| LED-панель iPixel | CENTRAL (connect) + контент | [`ble_led_panel`](../drivers/ble_led_panel.md) (actuator) + модуль [`panel`](../modules/panel.md) |

### Observer → сенсор

`ble_xiaomi_th` читає Xiaomi LYWSD03MMC з кастомною прошивкою (pvvx/ATC/BTHome) **пасивно** — observer ловить broadcast, а декодери самого драйвера (зареєстровані через `adv_decoder.h`) розбирають байти й кешують показник за MAC. Один фізичний сенсор → 3 канали (temperature / humidity / battery), які прив'язка обирає полем `address`. Публікується в `equipment.room_temp` / `equipment.room_humid` / `equipment.room_batt` (+ health-прапорці `equipment.<role>_ok`). Stale-timeout 60 c: немає broadcast → не healthy.

### Central → панель

`ble_led_panel` — це **connect**-пристрій, що матчиться за **adv-name**, не за MAC. Драйвер реєструє `ConnectProfile` (adv-name + write/notify UUID) у central-лінку й **володіє всім iPixel-форматом**: control-байти, нативний енкодер текст-кадру + шрифт, фонова render-задача. Він також реалізує `IPanelPort`, який **модуль** `panel` (власник контенту — годинник/темп/вологість, іконки, порогові кольори, анімація) резолвить через `DriverRegistry::panel_port()` і керує через `set_power` / `set_brightness` / `show_text`. Модуль BLE не торкається. Транспорт (central-лінк) знань про панель не має.

## Що далі

- [`ble_xiaomi_th.md`](../drivers/ble_xiaomi_th.md) — пасивний BLE-сенсор (observer).
- [`ble_led_panel.md`](../drivers/ble_led_panel.md) — драйвер LED-панелі iPixel (central transport).
- [`panel.md`](../modules/panel.md) — модуль контенту панелі (гліфи, іконки, кольори, анімація).

## Джерела

- [`components/modesp_ble`](../../../../components/modesp_ble)
- [`docs/ble/panel_protocol.md`](../../../../docs/ble/panel_protocol.md)
