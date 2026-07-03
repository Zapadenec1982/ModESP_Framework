# `display` — екранне меню, згенероване з маніфестів

> 📖 **In English:** [documentation/en/03-framework-reference/modules/display.md](../../../en/03-framework-reference/modules/display.md)

`display` — це загальний модуль фреймворку, що малює локальне екранне меню пристрою. Як WebUI і MQTT, меню **генерується з маніфестів**: кожен модуль описує секцію `display:` у своєму manifest.json, генератор зводить їх в одне constexpr-дерево (`generated/display_screens.h`), а цей модуль веде навігацію, показ значень і редагування параметрів через SharedState.

Модуль **апаратно-незалежний**: він ніколи не торкається пікселів, кольорів, рядків/колонок чи конкретного чипа. Він штовхає **семантичні View** (`MainView` / `MenuView` / `EditView` / `Notice`) крізь шов `IDisplayPort` і читає `caps()` backend-у. Драйвер-адаптер (`LogPort`, `At7456ePort`, `Amt630aPort`) перетворює ці View на залізо. Без заліза використовується дефолтний `LogPort` — View друкуються в серійний лог.

Цей дворівневий поділ — **ADR-002** ([docs/display/ADR-002-display-architecture.md](../../../../docs/display/ADR-002-display-architecture.md)); адаптер AMT630A приведено у відповідність до нього в **ADR-003** ([docs/display/ADR-003-amt630a-adr002-compliance.md](../../../../docs/display/ADR-003-amt630a-adr002-compliance.md)).

REQUIRES: `modesp_core`. Власних GPIO немає — лише SharedState (шиною/пінами володіє backend).

## Архітектура: три шари

| Шар | Де | Що знає |
|---|---|---|
| **Абстрактний модуль** | `modules/display/` (`DisplayModule`, `MenuEngine`, `NotificationQueue`) | лише *намір* — семантичні View + `caps()`; ніколи пікселі/кольори/чіп |
| **Адаптер порту** `XxxPort : IDisplayPort` | `modules/display/src/` (`LogPort`, `At7456ePort`, `Amt630aPort`) | View → чіп; володіє **усім layout** (через опційний `CharGridLayout`), кольором, скролом, capabilities |
| **Переносний чіп-драйвер** | `drivers/at7456e/`, `drivers/amt630a/` (`At7456e`, `Amt630a` — чіп-код живе разом зі своїм драйвером) | сирі регістри SPI/I²C; **0 ModESP-семантики** |

```
manifest.json (display:) ──┐
manifest.json (display:) ──┼→ generate_ui.py → display_screens.h (MENU_NODES, MENU_NODE_CAPS, MAIN_VALUES)
manifest.json (display:) ──┘                          │
                                                      ▼
кнопки (WebUI / MQTT / GPIO) → SharedState → DisplayModule → MenuEngine → MainView/MenuView/EditView
                                                      │
                                          IDisplayPort (present_*, caps, set_*, as_*) → backend → чіп
```

- **MenuEngine** — чиста логіка (host-тестована, zero heap): FSM `MAIN → MENU → EDIT`. Віддає семантичні View; скрол/курсор/колір рахує драйвер. Гейтить пункти меню за `caps()` (нижче).
- **DisplayModule** — обгортка BaseModule: читає кнопки зі SharedState, тікає движок на 100 Гц, маршрутизує параметри екрана → backend, веде банер сповіщень і дає порту періодичний «пульс» `service(dt)`.
- **CharGridLayout** — опційний host-тестований helper для *символьних* backend-ів: `MenuView` + `(cols, rows)` → сітка рядків із семантичними `RowRole`. Як роль виглядає — вирішує драйвер. Піксельні backend-и його не вживають.

## Екрани

| Екран | Поведінка |
|---|---|
| `MAIN` | Головні значення модулів (`main_value` з маніфестів), напр. «Термостат 22.5°C». `[OK]` відкриває меню. |
| `MENU` | Список: підменю модулів → пункти. Курсор `>`, скрол, віртуальний пункт `< Назад` / `< Вихід`. Пункти **фільтруються за `caps()`**. |
| `EDIT` | Редагування значення: UP/DOWN ± `step` із клампом до `min`/`max` (з декларації стану); enum-и циклять `options`; bool перемикається. `[OK]` зберігає у SharedState. |

Через 30 с без вводу движок авто-повертається на `MAIN` (незбережені правки відкидаються). Збережені значення йдуть стандартним шляхом SharedState: NVS-persist (якщо `persist: true`), WS-broadcast у WebUI, MQTT-publish — і потім маршрутизуються в backend через `apply_screen_params()`.

## Навігація: три кнопки

Модуль читає momentary-ключі зі SharedState і скидає їх назад у `false` після обробки:

| Ключ | Подія |
|---|---|
| `display.btn_up` | Вгору / +крок |
| `display.btn_down` | Вниз / −крок |
| `display.btn_select` | Вибір / зберегти |

Натискання можуть приходити звідусіль: **WebUI** (сторінка «Дисплей» має віртуальні кнопки — повний тест без заліза), **MQTT** або **фізичні кнопки** через драйвер `digital_input`/`pcf8574_input`, що пише ці ключі.

## Можливості — `caps()` гейтить меню

Backend повідомляє структуру `DisplayCaps`; модуль читає її один раз (`on_init`) і використовує, щоб **фільтрувати, які пункти меню показувати** — пункт оголошує потрібну можливість (`cap`) у маніфесті, а `MenuEngine` ховає його, коли backend цього не вміє. Тож той самий маніфест дає багате меню на AMT630A і скорочене на простому backend-і.

| Можливість | Значення | Гейтований пункт / контрол |
|---|---|---|
| `has_color` | програмована палітра | колір тексту/ролей |
| `has_backlight` | PWM-підсвітка | `display.backlight` |
| `has_video_params` | brightness/contrast/saturation декодера | `display.brightness/contrast/saturation` |
| `has_inputs` | вибір CVBS-входу (`as_video_inputs()`) | `display.input` |
| `has_backdrop` | фон no-signal (сніг/синій/чорний) | `display.backdrop` |
| `has_power` | power-gate (load-switch на GPIO, `as_power()`) | `display.power` |

Структурно-чужорідні можливості доступні через zero-cost `as_*()` (повертають `nullptr`, якщо нема): `as_video_inputs()`, `as_graphic()`, `as_power()`.

## Ключі стану

| Ключ | Тип | Примітки |
|---|---|---|
| `display.enabled` | bool | Вимикає рендер + обробку кнопок (підсвітка off). Persist. |
| `display.btn_up/down/select` | bool | Momentary, самоскидаються. |
| `display.screen` | string | Поточний екран: `main`, `menu:<label>`, `edit:<label>`. |
| `display.banner` / `display.banner_level` | string / int | Дзеркало активного банера сповіщень (для WebUI/MQTT). |
| `display.backlight` | int 0–100 | PWM-підсвітка % (за `has_backlight`). Persist. |
| `display.brightness/contrast/saturation` | int 0–100 | Video-параметри декодера (за `has_video_params`). Persist. |
| `display.backdrop` | int (0=Сніг,1=Синій,2=Чорний) | Фон no-signal (за `has_backdrop`). Persist. |
| `display.input` | int (0=AV1,1=AV3) | Вибір CVBS-входу (за `has_inputs`). Persist. |
| `display.power` | bool | Power-gate живлення чіпа (за `has_power`). Persist. |

Сповіщення приходять як `MsgUiNotice` на шину повідомлень (ADR-001) → `NotificationQueue` (пріоритет + TTL) → `present_notice()`.

## Backend-и

Активний backend обирається на етапі компіляції (`idf.py menuconfig` → **ModESP Display**, Kconfig `choice`) і резолвиться у рантаймі з `bindings.json` (`{"driver":"…","role":"display_main","module":"display"}`); геометрія/піни — з `board.json`.

| Backend | Чіп-драйвер | Примітки |
|---|---|---|
| **LogPort** (дефолт) | — | Друкує View у серійний лог. `caps()` усе false. Працює без заліза. |
| **At7456ePort** | `modesp_osd::At7456e` (SPI) | OSD-оверлей MAX7456 на аналоговому CVBS (PAL 16×30 / NTSC 13×30). `caps()` усе false (чистий оверлей). |
| **Amt630aPort** | `modesp_osd::Amt630a` (I²C) | Повнофункціональний: колір, апаратний масштаб per-window, 5 OSD-вікон, вибір CVBS-входу, no-signal фон, PWM-підсвітка, video-параметри, опційний power-gate. `caps()` усе true. |

### Особливості AMT630A

- **Конфіг:** запис `i2c_displays` у `board.json` — `cols`/`rows`, плюс `cal_x`/`cal_y` (per-panel overscan-зсув у px, застосовується до кожного OSD-вікна). `bindings.json` може додати `"settings": {"power_gpio": N}` для load-switch power-gate.
- **Кириличний шрифт:** вантажиться у FONT RAM (16×20 1bpp) — ROM-шрифт чипа не має кирилиці. Мапа: `drivers/amt630a/include/modesp/osd/amt630a_charmap.h`. Генерується `tools/gen_osd_font.py --target amt630a`.
- **Живлення / відновлення:** `display.power=false` ріже живлення (≈0 мА); `true` повертає живлення, і порт **внутрішньо** переініціалізує OSD (неблокуюче, chunked — через `service(dt)`), бо холодний старт губить увесь ESP-side OSD-стан.
- **Повна довідка регістрів:** [docs/amt630a/AMT630A_control_reference.md](../../../../docs/amt630a/AMT630A_control_reference.md). Енергорежими: [docs/amt630a/AMT630A_power_modes.md](../../../../docs/amt630a/AMT630A_power_modes.md).

## Підключення нового backend-у

Реалізуй `IDisplayPort` (семантичний шов) — а для символьного дисплея перевикористай `CharGridLayout`:

```cpp
#include "display/display_port.h"
#include "display/char_grid.h"

class MyPort : public modesp::display::IDisplayPort {
public:
    bool init() override { /* шина/піни */ return true; }
    modesp::display::DisplayCaps caps() const override { return {}; }   // оголоси, що вмієш
    void present_main(const modesp::display::MainView& v) override { /* idle-екран */ }
    void present_menu(const modesp::display::MenuView& v) override {
        modesp::display::CharGrid g;
        modesp::display::CharGridLayout::layout_menu(v, cols_, rows_, g);  // скрол/курсор/кламп
        /* рендер g.lines (кожен має RowRole) */
    }
    void present_edit(const modesp::display::EditView& v) override { /* ... */ }
    void present_notice(const modesp::display::Notice& n) override { /* банер */ }
    void clear_notice() override {}
    // опційно: set_backlight/contrast/brightness/saturation/set_backdrop, as_video_inputs/as_power
};
```

`present_*` викликається лише коли View змінився (драйвер тримає власну тінь для diff). Зареєструй backend (`MODESP_REGISTER_DISPLAY(myport, &factory)`) і додай пункт Kconfig-choice. `DisplayModule` не міняється — рівно як додавання драйвера датчика/актуатора.

## Додати свій модуль у меню

Додай секцію `display:` у manifest.json свого модуля (повна специфікація — [manifest.md](../../02-module-author-guide/manifest.md)):

```json
"display": {
  "main_value": {"key": "my.temp", "format": "%.1f°C"},
  "menu_label": "Мій модуль",
  "menu_items": [
    {"label": "Уставка", "key": "my.setpoint"},
    {"label": "Вхід",    "key": "my.input", "cap": "inputs"}
  ]
}
```

Редагованість, межі, крок, одиниці й опції беруться з декларації `state` ключа — нічого не дублюється. Опційний `"cap"` ховає пункт на backend-ах без цієї можливості.

## Тести

- `tools/tests/test_generator.py::TestDisplayScreensGenerator` — генерація дерева + `MENU_NODE_CAPS` (pytest).
- `tests/host/test_display_menu.cpp` — doctest-кейси `MenuEngine`: навігація, клампинг, enum/bool, idle-timeout, caps-gating.
- `tests/host/test_char_grid.cpp` / `test_notification_queue.cpp` / `test_display_module.cpp` — layout, черга банерів, glue. Запуск: `python -m pytest tools/tests/test_cpp_host.py -v`.
