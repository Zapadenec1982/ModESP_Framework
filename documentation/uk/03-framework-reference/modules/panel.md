# `panel` — модуль LED-панелі (годинник/температура/вологість)

> 📖 **In English:** [documentation/en/03-framework-reference/modules/panel.md](../../../en/03-framework-reference/modules/panel.md)

Бізнес-модуль, що володіє **тим, ЩО показує** BLE LED-панель iPixel Color / LED_BLE 64×16 RGB. **Чистий споживач SharedState** (як [`presence`](presence.md) / [`simple_thermo`](simple_thermo.md)): сам BLE не чіпає і **не має жодного BLE-хедера**. Драйвер [`ble_led_panel`](../drivers/ble_led_panel.md) володіє BLE-лінком і всім форматом дротового протоколу iPixel (UUID'и, control-байти, кодер тексту, шрифт, render-задача); цей модуль володіє **лише контентом**. Backend панелі модуль резолвить в override `on_bind` **за роллю** — `find_actuator(role)->as_panel()` → `modesp::panel::IPanelPort*` (панель — звичайний актуатор, якого створює DriverManager) — і подає все крізь цей порт (`connected()` / `set_power()` / `set_brightness()` / `show_text()`). Якщо драйвер панелі не прив'язано — порт `null`, і модуль не дає виводу. Фіча на гілці `feat/ble-led-panel`.

Модуль ротує **три поля** кожні ~4 c — настінний **ГОДИННИК** (HH:MM з локального часу SNTP), **ТЕМПЕРАТУРА** (`equipment.room_temp`) і **ВОЛОГІСТЬ** (`equipment.room_humid`). Кожне поле health-гейтнуте (`equipment.<role>_ok`), де-дублюється і переоцінюється ~4 Гц.

## Потік даних

```
SNTP local time ─┐
equipment.room_temp  (equipment.room_temp_ok)  ─┼─▶ panel-модуль
equipment.room_humid (equipment.room_humid_ok) ─┘   (ротація ~4 c · health-gate · de-dup · ~4 Гц)
                                                          │
                                          port_->show_text (IPanelPort)
                                                          │
                              ble_led_panel (кодер TEXT-кадру) ──BLE──▶ LED_BLE 64×16
```

## Рендер (port_->show_text)

Модуль подає текст через `port_->show_text(...)` на резолвнутому `IPanelPort`. Далі *драйвер* рендерить його нативним iPixel **TEXT-кадром** (по-символьні блоки гліфів 8×16 + CRC32). Шрифт генерується `tools/gen_osd_font.py --target panel` → `generated/panel_font_data.h` (володіє драйвер, не модуль).

```cpp
// modesp::panel::IPanelPort — поверхня, яку подає модуль (сам байтів не кодує):
void show_text(const char* s, uint8_t r, uint8_t g, uint8_t b,
               uint8_t anim, uint8_t speed, uint8_t rainbow);
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

**Єдиний власник:** живлення/яскравість/ефект/контент подає цей модуль через `IPanelPort` (`set_power` / `set_brightness` / `show_text`); драйвер кодує байти, але контент не вирішує. На (пере)конекті модуль перевідправляє power+brightness зі стану (sentinel-скид), тож налаштування користувача переживають реконект. (Драйвер [`ble_led_panel`](../drivers/ble_led_panel.md) *також* пише яскравість через `EquipmentBase` `set_value` — як і раніше — але модуль лишається єдиним писарем контенту, тож гонки на тексті немає.)

> ℹ️ **Вільний текст** дає віджет `text_input` (доданий у фреймворк: `webui/src/components/widgets/TextInput.svelte` + `WIDGET_TYPE_COMPAT["text_input"]={string}`; bundle перебілдено `npm run deploy`). Непорожнє `panel.message` показується **замість** ротації (у кольорі `panel.color` — нативний `color_picker`, деф. білий, парситься `parse_hex_color` у модулі; з обраним ефектом); очистити поле → ротація відновлюється. Колір — лише для повідомлення; сенсори лишають порогові кольори. Кап **31 символ** (стеля — `etl::string<32>` у SharedState; рендер `n<31` у `ble_service.cpp`); панель скролить довгий текст ефектом; POST на blur/Enter (не щоклавішу).

## API виводу тексту (слоти модулів)

Будь-який модуль може вивести свій текст на панель через **5 спільних текст-слотів** (`panel.slot0`..`panel.slot4`) — тонка конвенція над SharedState (слоти це звичайні рядкові state-ключі). API — хедер [`panel_text.h`](../../../../modules/panel/include/panel_text.h) (належить модулю `panel`; модуль-постувальник залежить від компонента `panel`):

```cpp
#include "panel_text.h"
// ... зсередини методу модуля (on_update / on_init), де доступний state_set:
state_set(modesp::panel_text::slot(0), "ALARM");     // вивести у слот 0
state_set(modesp::panel_text::slot(1), "DEFROST");   // слот 1
state_set(modesp::panel_text::slot(0), "");          // очистити слот 0
```

| Символ | Опис |
|---|---|
| `modesp::panel_text::SLOTS` | Кількість слотів (5). |
| `modesp::panel_text::slot(i)` | SharedState-ключ слота `i` (0..4); поза діапазоном → слот 0. |

- Непорожні слоти **ротуються на екрані** (нейтральним білим, з поточним ефектом `panel.anim`) поряд із годинником/сенсорами. Порожній (`""`) або незаписаний слот не показується.
- Довжина — до **31 символа** (стеля рядка стану).
- Прочитати слот назад: `read_string(modesp::panel_text::slot(i), buf, sizeof(buf))`.
- Панель ініціалізує слоти порожніми в `on_init`; у веб-морді (вкладка iPixel) є read-only картка **«Слоти модулів»**.

> `state_set` — `protected`-метод `BaseModule`, тож API викликається **зсередини модуля** (не з вільної функції). Слоти **не персистяться** — це транзитні повідомлення часу виконання.

## Графіка (історія дизайну)

Багатшу графіку досліджували й відкинули:
- **DIY per-pixel** — один BLE round-trip на піксель (надто повільно).
- **Full-frame PNG upload** — працює для статичних картинок, але **PNG-компресія на пристрої** (ROM miniz) вичерпує вільний heap (~64 КБ) на цьому залізі, тож невиправдана.

Робочий і обраний шлях для readout — нативний **TEXT-кадр** (гліфи шрифту + іконки + колір + анімація).

## Підключення

1. `project.json` → `"modules"` += `"panel"`.

Контент панелі дає цей модуль, а BLE-лінк і весь формат дротового протоколу (UUID'и, control-байти, кодер тексту, шрифт) — драйвер [`ble_led_panel`](../drivers/ble_led_panel.md), який публікує свій `IPanelPort`; сам BLE-host — спільна інфраструктура [`modesp_ble`](../components/modesp_ble.md). Вивід зʼявляється лише коли драйвер панелі прив'язано (`on_bind` резолвить непорожній порт).

## Джерела

- [`modules/panel/manifest.json`](../../../../modules/panel/manifest.json)
- [`modules/panel/src/panel_module.cpp`](../../../../modules/panel/src/panel_module.cpp)
- [`drivers/ble_led_panel.md`](../drivers/ble_led_panel.md) — драйвер панелі (BLE-лінк + формат протоколу + `IPanelPort`).
- [`tools/gen_osd_font.py`](../../../../tools/gen_osd_font.py) — генератор шрифту (`--target panel`).
