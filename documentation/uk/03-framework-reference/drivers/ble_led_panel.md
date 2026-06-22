# `ble_led_panel` — BLE RGB LED-панель 64×16 (iPixel / LED_BLE)

> 📖 **In English:** [documentation/en/03-framework-reference/drivers/ble_led_panel.md](../../../en/03-framework-reference/drivers/ble_led_panel.md)

Драйвер керує китайською RGB LED-матрицею **iPixel Color / LED_BLE 64×16** через BLE. Це **actuator** з `hardware_type: ble_device`, який підключається до панелі **за ім'ям реклами (ADV-NAME)**, а не за MAC. Драйвер володіє **лише BLE-лінком** (ціллю підключення); живлення, яскравість, ефект і **контент** (що саме світиться) належать модулю [`panel`](../modules/panel.md) і подаються через синглтон `BlePanel`.

Драйвер їде на спільному BLE-хості [`modesp_ble`](../../03-framework-reference/components/modesp_ble.md) (роль **CENTRAL**). Підключення до панелі ставить на паузу пасивний скан observer'а, потім відновлює його. Модулі ніколи не торкаються BLE напряму — I/O робить драйвер, модуль читає/пише SharedState.

ВИМАГАЄ: `modesp_ble` (CENTRAL), `modesp_hal`.

## Залізо (board.json)

Панель оголошується у `ble_devices` через **`name`** — це ADV-NAME, до якого драйвер під'єднується (а не MAC, як у пасивних сенсорів):

```json
"ble_devices": [
  { "id": "led_panel", "name": "LED_BLE_E6C5EBE2" }
]
```

| Поле | Опис |
|---|---|
| `id` | Локальний ідентифікатор пристрою, на нього посилається binding. |
| `name` | ADV-NAME панелі для підключення. Драйвер сканує префікс **`LED_BLE_`**. |

## Прив'язки (bindings.json)

```json
{"hardware": "led_panel", "driver": "ble_led_panel", "role": "panel", "module": "equipment"}
```

| Поле | Значення |
|---|---|
| `hardware` | `id` пристрою з `ble_devices`. |
| `driver` | `ble_led_panel`. |
| `role` | `panel`. |
| `module` | `equipment`. |

## Control-plane

Драйвер володіє лінком; control-plane (живлення/яскравість) пише **модуль** через `BlePanel`. Послідовність виходу на READY:

```
скан ADV-NAME "LED_BLE_" ──▶ connect ──▶ discover (service 0x00FA)
   write char fa02 + notify char fa03 ──▶ READY
```

- **service** `0x00FA`, **write** характеристика `fa02`, **notify** характеристика `fa03`.
- `update()` драйвера лише **логує** фронт підключення; `set()` — no-op. Драйвер **не** надсилає живлення чи яскравість сам.
- Живленням (ON/OFF) та яскравістю (0..100 %) керує модуль [`panel`](../modules/panel.md): він єдиний писар і повторно застосовує їх на кожному (пере)підключенні (скидання sentinel), тож налаштування користувача переживають реконект, без гонки на фронті підключення.

## Команди (control-байти)

Байти незмінні — але надсилає їх **модуль** `panel` (через `BlePanel`), а не драйвер:

| Дія | Байти |
|---|---|
| Power ON | `05 00 07 01 01` |
| Power OFF | `05 00 07 01 00` |
| Яскравість | `05 00 04 80 <pct>` (`<pct>` = 0..100) |

## Розподіл відповідальності: драйвер vs модуль

Драйвер відповідає **лише за транспорт** (BLE-лінк, ціль підключення). Живлення, яскравість, ефект і **контент** — текст, іконки, кольори, анімація — належать модулю [`panel`](../modules/panel.md), який є єдиним писарем над `BlePanel`. Зв'язок розв'язаний через синглтон `BlePanel`:

```cpp
// власник лінку (драйвер) → лише ціль підключення:
BlePanel::set_target("LED_BLE_");   // префікс ADV-NAME
bool ready = BlePanel::is_connected();

// control-plane + контент (модуль panel):
BlePanel::write_cmd(data, len, with_response);   // живлення / яскравість
void show_text(const char* s,                    // нативний TEXT-кадр
               uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
               uint8_t anim = 0, uint8_t speed = 0x32, uint8_t rainbow = 0);
```

Модуль читає `panel.power` (bool), `panel.brightness` (int %), `panel.anim` (int 0..7 ефект) і `panel.rotate` (bool) з SharedState (їх задає веб-вкладка «iPixel» / MQTT) і пише їх через `BlePanel`. Таке розділення дає єдиного писаря без гонки на фронті підключення, а драйвер тримає фізичний BLE-лінк живим незалежно від того, що саме модуль вирішує показати.

## Опційність (Kconfig)

CENTRAL-роль (підключення до панелі) вмикається у menu **«ModESP BLE»**:

```
CONFIG_MODESP_BLE_ENABLE     (master — спільний BLE-хост)
CONFIG_MODESP_BLE_CENTRAL    (connect до панелі)
```

Без `CONFIG_MODESP_BLE_CENTRAL` драйвер не має чим під'єднатися до панелі. Сам BLE-хост (`modesp_ble`) лінкується у `main` лише при `CONFIG_MODESP_BLE_ENABLE`.

## Що далі

- **[modules/panel.md](../modules/panel.md)** — модуль контенту панелі (годинник/темп/вологість, іконки, кольори, нативна анімація).
- **[components/modesp_ble.md](../components/modesp_ble.md)** — спільний BLE-хост (OBSERVER / CENTRAL / PERIPHERAL).
- **[`docs/ble/panel_protocol.md`](../../../../docs/ble/panel_protocol.md)** — байтовий протокол керування панеллю.

## Джерела

- [`drivers/ble_led_panel/manifest.json`](../../../../drivers/ble_led_panel/manifest.json)
- [`drivers/ble_led_panel/include/ble_led_panel_driver.h`](../../../../drivers/ble_led_panel/include/ble_led_panel_driver.h)
- [`drivers/ble_led_panel/src/ble_led_panel_driver.cpp`](../../../../drivers/ble_led_panel/src/ble_led_panel_driver.cpp)
