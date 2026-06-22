# `ble_led_panel` — BLE RGB LED-панель 64×16 (iPixel / LED_BLE)

> 📖 **In English:** [documentation/en/03-framework-reference/drivers/ble_led_panel.md](../../../en/03-framework-reference/drivers/ble_led_panel.md)

Драйвер керує китайською RGB LED-матрицею **iPixel Color / LED_BLE 64×16** через BLE. Це **actuator** з `hardware_type: ble_device`, який підключається до панелі **за ім'ям реклами (ADV-NAME)**, а не за MAC. Драйвер володіє BLE-лінком і control-plane (живлення + яскравість); **контент** (що саме світиться) належить модулю [`panel`](../modules/panel.md) і подається через синглтон `BlePanel`.

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

Драйвер володіє лінком і control-plane через `BlePanel`. Послідовність виходу на READY:

```
скан ADV-NAME "LED_BLE_" ──▶ connect ──▶ discover (service 0x00FA)
   write char fa02 + notify char fa03 ──▶ READY
```

- **service** `0x00FA`, **write** характеристика `fa02`, **notify** характеристика `fa03`.
- Драйвер **самокерований**: при підключенні вмикає панель на заданій яскравості (ON).

Керування — живленням (ON/OFF) та яскравістю (0..100 %).

## Команди (control-байти)

| Дія | Байти |
|---|---|
| Power ON | `05 00 07 01 01` |
| Power OFF | `05 00 07 01 00` |
| Яскравість | `05 00 04 80 <pct>` (`<pct>` = 0..100) |

## Розподіл відповідальності: драйвер vs модуль

Драйвер відповідає **лише за транспорт і control-plane** (link, power, brightness). **Контент** — текст, іконки, кольори, анімація — належить модулю [`panel`](../modules/panel.md). Зв'язок розв'язаний через синглтон `BlePanel`:

```cpp
// власник лінку (драйвер) → control-plane:
BlePanel::set_target("LED_BLE_");   // префікс ADV-NAME
bool ready = BlePanel::is_connected();
BlePanel::write_cmd(data, len, with_response);

// контент (модуль panel) → нативний TEXT-кадр:
void show_text(const char* s,
               uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
               uint8_t anim = 0, uint8_t speed = 0x32, uint8_t rainbow = 0);
```

Таке розділення дозволяє драйверу тримати фізичний BLE-лінк живим незалежно від того, що саме модуль вирішує показати.

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
