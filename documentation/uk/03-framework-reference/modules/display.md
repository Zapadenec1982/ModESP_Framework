# `display` — екранне меню з маніфестів

> 📖 **In English:** [documentation/en/03-framework-reference/modules/display.md](../../../en/03-framework-reference/modules/display.md)

`display` — generic-модуль фреймворку, що рендерить локальне екранне меню пристрою. Як і WebUI та MQTT, меню **генерується з маніфестів**: кожен модуль описує секцію `display:` у своєму manifest.json, генератор збирає їх в одне constexpr-дерево (`generated/display_screens.h`), а цей модуль виконує навігацію, показ значень та редагування параметрів через SharedState.

Модуль **апаратно-незалежний**: він формує текстовий кадр (`DisplayFrame`, 4 рядки UTF-8) і віддає його рендереру через інтерфейс `IDisplayRenderer`. Без заліза працює `LogRenderer` — кадр видно у серійному лозі. Драйвер реального дисплея (SSD1306, HD44780, TFT) реалізує той самий інтерфейс і підключається через `set_renderer()`.

ВИМАГАЄ: `modesp_core`. Жодного GPIO — лише SharedState.

## Як це працює

```
manifest.json (display:) ──┐
manifest.json (display:) ──┼→ generate_ui.py → display_screens.h (MENU_NODES, MAIN_VALUES)
manifest.json (display:) ──┘                          │
                                                      ▼
кнопки (WebUI / MQTT / GPIO) → SharedState → DisplayModule → MenuEngine → DisplayFrame → IDisplayRenderer
```

- **MenuEngine** — чиста логіка (host-тестована, zero heap): FSM `MAIN → MENU → EDIT`.
- **DisplayModule** — BaseModule-обгортка: зчитує кнопки зі SharedState, тікає движок на 100 Гц, рендерить кадр лише при зміні.

## Екрани

| Екран | Поведінка |
|---|---|
| `MAIN` | Головні значення модулів (`main_value` з маніфестів): «Термостат 22.5°C». Сторінки ротуються кожні 4 с; `[OK]` відкриває меню. |
| `MENU` | Список: підменю модулів → пункти. Курсор `>`, прокрутка, віртуальний пункт `< Назад` / `< Вихід`. |
| `EDIT` | Редагування значення: UP/DOWN ± `step` із clamp по `min`/`max` (зі state-декларації), для enum — перебір `options`, для bool — перемикач. `[OK]` зберігає у SharedState. |

Через 30 с без натискань — авто-повернення на `MAIN` (незбережені зміни відкидаються). Збережені значення проходять стандартний шлях SharedState: persist у NVS (якщо `persist: true`), WS-broadcast у WebUI, MQTT publish.

## Навігація: три кнопки

Модуль читає momentary-ключі зі SharedState і самостійно скидає їх у `false` після обробки:

| Ключ | Подія |
|---|---|
| `display.btn_up` | Вгору / збільшити |
| `display.btn_down` | Вниз / зменшити |
| `display.btn_select` | Вибір / зберегти |

Джерело натискань — будь-яке: **WebUI** (сторінка «Дисплей» має віртуальні кнопки — повноцінний тест без заліза), **MQTT** або **фізичні кнопки** через `digital_input`/`pcf8574_input` драйвер, що пише ці ключі.

## Ключі стану

| Ключ | Тип | Примітки |
|---|---|---|
| `display.enabled` | bool | Вимикає рендер і обробку кнопок. Зберігається. |
| `display.btn_up/down/select` | bool | Momentary, самоскидаються. |
| `display.screen` | string | Поточний екран: `main`, `menu:root`, `menu:<label>`, `edit:<label>`. |

## Підключення драйвера дисплея

```cpp
#include "display/renderer.h"

class Ssd1306Renderer : public modesp::display::IDisplayRenderer {
public:
    bool init() override { /* i2c init */ return true; }
    void render(const modesp::display::DisplayFrame& f) override {
        // f.rows[0..3] — UTF-8 рядки; намалювати і flush
    }
};
```

`render()` викликається лише при зміні кадру. Кількість видимих символів визначає рендерер; рядки кадру обмежені 40 байтами UTF-8.

### Готовий рендерер: AT7456E (OSD composite-оверлей)

Фреймворк має вбудований рендерер на **AT7456E** (MAX7456-сумісний OSD-чіп) — накладає символьну сітку (PAL 16×30 / NTSC 13×30) на аналоговий composite-відеосигнал. Потрібен відеомонітор на CVBS-виході; чіп може працювати на власному internal-sync, тож екран горить і без вхідного відео.

Драйвер переносний — компонент `components/modesp_osd/` (спільний для цього фреймворку і проектів-сиблінгів). Вмикається в `idf.py menuconfig` → **ModESP Display → AT7456E OSD renderer**; там же піни (CS/DATA/CLK/MISO), відеостандарт і sync. Коли опція увімкнена, `DisplayModule` за замовчуванням бере `AT7456ERenderer` замість `LogRenderer`.

**Шрифт і кирилиця.** AT7456E зберігає шрифт у character-NVM (256 гліфів 12×18px). Стандартний шрифт **не має кирилиці**, тож для українського UI заливаємо власний шрифт у NVM. Розкладку визначає [osd_charmap.h](../../../../components/modesp_osd/include/modesp/osd/osd_charmap.h) (ASCII тотожно, кирилиця U+0410-044F → 0x80+, українські спецлітери Є/І/Ї/Ґ — фіксовані індекси), а драйвер уміє залити шрифт (`upload_font`, sentinel-перевірка щоб не перепрошувати NVM). Сам `.mcm`-шрифт генерується `tools/gen_osd_font.py` *(у розробці)*; доки шрифт не залито, кирилиця рендериться як `?`.

## Додавання модуля у меню

У manifest.json вашого модуля — секція `display:` (повна специфікація у [manifest.md](../../02-module-author-guide/manifest.md)):

```json
"display": {
  "main_value": {"key": "my.temp", "format": "%.1f°C"},
  "menu_label": "Мій модуль",
  "menu_items": [
    {"label": "Уставка", "key": "my.setpoint"}
  ]
}
```

Редагованість, межі, крок, одиниці та options движок бере зі `state`-декларації ключа — нічого дублювати не треба.

## Тести

- `tools/tests/test_generator.py::TestDisplayScreensGenerator` — генерація дерева (pytest).
- `tests/host/test_display_menu.cpp` — 16 doctest-кейсів MenuEngine: навігація, clamp, enum/bool, idle-timeout. Запуск: `python -m pytest tools/tests/test_cpp_host.py -v`.
