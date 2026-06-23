# `panel` — модуль LED-панелі (годинник/температура/вологість)

> 📖 **In English:** [documentation/en/03-framework-reference/modules/panel.md](../../../en/03-framework-reference/modules/panel.md)

Бізнес-модуль, що володіє **тим, ЩО показує** BLE LED-панель iPixel Color / LED_BLE 64×16 RGB. **Чистий споживач SharedState** (як [`presence`](presence.md) / [`simple_thermo`](simple_thermo.md)): сам BLE не чіпає — драйвер [`ble_led_panel`](../drivers/ble_led_panel.md) тримає лише BLE-лінк, а цей модуль володіє і контентом, **і живленням/яскравістю/ефектом** (через singleton `BlePanel`). Транспорт і керування розв'язані саме через `BlePanel` (фіча на гілці `feat/ble-led-panel`).

Модуль ротує **три поля** кожні ~4 c — настінний **ГОДИННИК** (HH:MM з локального часу SNTP), **ТЕМПЕРАТУРА** (`equipment.room_temp`) і **ВОЛОГІСТЬ** (`equipment.room_humid`). Кожне поле health-гейтнуте (`equipment.<role>_ok`), де-дублюється і переоцінюється ~4 Гц.

## Потік даних

```
SNTP local time ─┐
equipment.room_temp  (equipment.room_temp_ok)  ─┼─▶ panel-модуль
equipment.room_humid (equipment.room_humid_ok) ─┘   (ротація ~4 c · health-gate · de-dup · ~4 Гц)
                                                          │
                                          BlePanel::show_text (нативний iPixel TEXT-кадр)
                                                          │
                                       ble_led_panel ──BLE──▶ LED_BLE 64×16
```

## Рендер (BlePanel::show_text)

Текст рендериться нативним iPixel **TEXT-кадром** (по-символьні блоки гліфів 8×16 + CRC32). Шрифт генерується `tools/gen_osd_font.py --target panel` → `generated/panel_font_data.h`.

```cpp
void show_text(const char* s,
               uint8_t r = 255, g = 255, b = 255,
               uint8_t anim = 0, uint8_t speed = 0x32, uint8_t rainbow = 0);
```

## Піктограми (іконки)

Малюнки-піктограми рендеряться інлайн як звичайні гліфи (байти з Private-Use). Напр. `"[clock]12:34"`, `"[thermo]28.5C"`, `"[drop]50%"`.

| Байт | Іконка |
|---|---|
| `0x80` | термометр |
| `0x81` | крапля |
| `0x82` | людина |
| `0x83` | дзвоник |
| `0x84` | wifi |
| `0x85` | годинник |

## Порогові кольори

| Величина | Діапазон | Колір |
|---|---|---|
| Температура | < 18 | синій |
| | 18–27 | зелений |
| | > 27 | червоний |
| Вологість | < 30 | помаранчевий |
| | 30–60 | зелений |
| | > 60 | синій |

## Нативна анімація (байт `anim`)

HW-підтверджено на `LED_BLE_E6C5EBE2`:

| `anim` | Ефект |
|---|---|
| 0 | статика |
| 1 | скрол справа→наліво |
| 2 | скрол зліва→направо |
| 3 | знизу→вгору |
| 4 | згори→вниз |
| 5 | блимання |
| 6 | дихання |
| 7 | drop-in (збирається порядково) |

`speed` 0..100, `rainbow` 0..9 (циклічна зміна кольору). Ефект **web-керований** (`panel.anim`, вкладка iPixel) і застосовується до всіх полів ротації; деф. `0` (статика — найчитабельніше).

## Веб-керування (вкладка «iPixel»)

`manifest.json` оголошує веб-сторінку **iPixel** (manifest-driven UI → generic frontend рендерить її з `ui.json`, **без перебілда фронтенду**) з контролями. Той самий стан керується і по MQTT (`mqtt_subscribe`), і зберігається між перезавантаженнями (`persist`).

| Ключ | Тип / доступ | Віджет | Призначення |
|---|---|---|---|
| `panel.connected` | bool · read | indicator | Звʼязок з панеллю по BLE |
| `panel.text` | string · read | status_text | Поточний текст на панелі |
| `panel.power` | bool · rw (деф. ON) | toggle | Живлення панелі (ON/OFF) |
| `panel.brightness` | int 5..100 % · rw (деф. 80) | slider | Яскравість |
| `panel.rotate` | bool · rw (деф. авто) | toggle | Авто-ротація / пауза (утримати кадр) |
| `panel.anim` | int 0..7 · rw (деф. 0) | slider | Ефект тексту (див. таблицю анімації) |
| `panel.message` | string ≤31 · rw (деф. порожнє) | **text_input** | Свій текст (порожнє = ротація) |
| `panel.color` | string `#RRGGBB` · rw (деф. білий) | **color_picker** | Колір повідомлення |

**Єдиний власник:** живлення/яскравість/ефект пише цей модуль через `BlePanel` (драйвер [`ble_led_panel`](../drivers/ble_led_panel.md) більше **не self-driving** — лише тримає лінк), тож немає гонки двох письменників. На (пере)конекті модуль перевідправляє power+brightness зі стану (sentinel-скид), тож налаштування користувача переживають реконект.

> ℹ️ **Вільний текст** дає віджет `text_input` (доданий у фреймворк: `webui/src/components/widgets/TextInput.svelte` + `WIDGET_TYPE_COMPAT["text_input"]={string}`; bundle перебілдено `npm run deploy`). Непорожнє `panel.message` показується **замість** ротації (у кольорі `panel.color` — нативний `color_picker`, деф. білий, парситься `parse_hex_color` у модулі; з обраним ефектом); очистити поле → ротація відновлюється. Колір — лише для повідомлення; сенсори лишають порогові кольори. Кап **31 символ** (стеля — `etl::string<32>` у SharedState; рендер `n<31` у `ble_service.cpp`); панель скролить довгий текст ефектом; POST на blur/Enter (не щоклавішу).

## Графіка (історія дизайну)

Багатшу графіку досліджували й відкинули:
- **DIY per-pixel** — один BLE round-trip на піксель (надто повільно).
- **Full-frame PNG upload** — працює для статичних картинок, але **PNG-компресія на пристрої** (ROM miniz) вичерпує вільний heap (~64 КБ) на цьому залізі, тож невиправдана.

Робочий і обраний шлях для readout — нативний **TEXT-кадр** (гліфи шрифту + іконки + колір + анімація).

## Підключення

1. `project.json` → `"modules"` += `"panel"`.
2. `main/CMakeLists.txt` → `PRIV_REQUIRES` += `panel`.

Контент панелі дає цей модуль, а лінк/живлення/яскравість — драйвер [`ble_led_panel`](../drivers/ble_led_panel.md); сам BLE-host — спільна інфраструктура [`modesp_ble`](../components/modesp_ble.md).

## Джерела

- [`modules/panel/manifest.json`](../../../../modules/panel/manifest.json)
- [`modules/panel/src/panel_module.cpp`](../../../../modules/panel/src/panel_module.cpp)
- [`drivers/ble_led_panel.md`](../drivers/ble_led_panel.md) — драйвер панелі (транспорт + control-plane).
- [`tools/gen_osd_font.py`](../../../../tools/gen_osd_font.py) — генератор шрифту (`--target panel`).
