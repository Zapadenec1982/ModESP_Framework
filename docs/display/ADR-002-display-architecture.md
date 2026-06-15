# ADR-002 — Дворівнева архітектура display-підсистеми: семантичний шов `IDisplayPort` + драйвер-адаптер заліза

- **Статус:** Прийнято
- **Дата:** 2026-06-14
- **Контекст проєкту:** ModESP Framework (ESP-IDF v5.5, C++17 + ETL, zero-heap у hot-path, single main-loop task)
- **Автор рішення:** провідний архітектор ModESP
- **Пов'язані:** [ADR-001](../amt630a/ADR-001-osd-notifications.md) (доставка сповіщень), `AMT630A_driver_design.md` (стає адаптером під цей ADR)
- **Спирається на:** 4 звіти ревізії display-підсистеми (current-module, modesp-driver-conventions, three-backends-test, best-practices)

---

## 1. Контекст і проблема

Display-підсистема ModESP уже частково розділена правильно: `DisplayModule` (бізнес-glue) → `MenuEngine` (FSM меню) → `IDisplayRenderer` (шов) → конкретний рендерер (`LogRenderer`, `AT7456ERenderer`). Це **наближення** до цільового патерну, але має три класи протікання характеристик заліза у нібито апаратно-незалежний шар.

### 1.1. Геометрія заліза зашита у спільний тип `DisplayFrame`

`modules/display/include/display/renderer.h:18-20`:

```cpp
struct DisplayFrame {
    static constexpr size_t MAX_ROWS  = 4;   // типовий OLED 128×64, шрифт 8px
    static constexpr size_t MAX_BYTES = 40;  // UTF-8 байтів на рядок
    etl::string<MAX_BYTES> rows[MAX_ROWS];
};
```

Число `4` (кількість рядків конкретного OLED) **протікає крізь усю доменну логіку**:

- `MenuEngine::VISIBLE_ITEMS = DisplayFrame::MAX_ROWS - 1` (`menu_engine.h:55`) — математика пагінації/скролу прив'язана до 4-рядкового екрана.
- `main_pages()`, `build_main/build_menu/build_edit` обмежені `MAX_ROWS` (`menu_engine.cpp`).
- Навіть AT7456E-адаптер (16/13 рядків PAL/NTSC) змушений **центрувати 4-рядковий кадр руками** (`at7456e_renderer.cpp:76-79`, `top_row_`).

AMT630A-меню хоче 16+ логічних рядків (design doc §6.3), SSD1306 дає 4–8, AT7456E — 13–16. Поточний `DisplayFrame[4][40]` фізично не може це виразити — він нав'язує найменший екран усім.

### 1.2. Шов несе готовий layout, а не intent

`MenuEngine` сам приймає рішення layout-рівня (центрування, скрол-вікно `Level{scroll}`, рядок `"< Назад"`, маркер курсора `">"`, рамки `"== ... =="`) і кладе **готові рядки** у `DisplayFrame`. Рендерер лише мапить рядок → гліфи. Тобто **layout-рішення живе у спільному движку**, а не в драйвері. Драйвер не може показати меню «по-своєму» (інверсія вибраного рядка, банер-вікно, колір) — він отримує вже зверстаний текст без атрибутів.

### 1.3. Вибір backend через `#ifdef` замість registry (відхилення від конвенції ModESP)

`display_module.cpp:39-45`:

```cpp
modesp::display::IDisplayRenderer* default_renderer() {
#ifdef CONFIG_MODESP_DISPLAY_AT7456E
    return &s_at7456e_renderer;   // модуль знає конкретний клас!
#else
    return &s_log_renderer;
#endif
}
```

Для sensor/actuator-драйверів ModESP має наскрізний патерн **«інтерфейс + явний registry (тип-рядок → фабрика) + авто-Kconfig-toggle + генерований register-all»** (`driver_registry.h`, `MODESP_REGISTER_SENSOR/ACTUATOR`, `DriverManager`). Модуль `equipment` не знає жодного concrete-драйвера. Display ж — знає `AT7456ERenderer` через `#include` + `#ifdef`. Додавання SSD1306/AMT630A вимагає **ручної правки модуля-абстракції** — рівно та хардкод-диспетчеризація, яку `DriverRegistry` усунув для драйверів.

### Підсумок проблеми

`IDisplayRenderer`/`DisplayFrame` — це **under-abstracted семантичний шов**: правильний рівень (UTF-8 рядки, не пікселі), але (а) з зашитою геометрією, (б) з layout у движку замість драйвера, (в) з `#ifdef`-вибором замість registry. Мета ADR — підняти шов до чистого intent-рівня і узгодити вибір backend з конвенцією драйверів ModESP, **не вводячи нічого чужорідного**.

---

## 2. Рішення: дворівнева архітектура

Поділяємо display-підсистему на **два чіткі шари** (за аналогією з парою `EquipmentBase` ↔ `ISensorDriver`/`IActuatorDriver`):

### Шар 1 — АБСТРАКТНИЙ модуль дисплея (UI-логіка, агностична до заліза)

- **Де:** `modules/display/` (без змін розташування).
- **Що:** `DisplayModule` (lifecycle, кнопки, сповіщення, вибір port'а з registry), `MenuEngine` (FSM меню — навігація/редагування/таймери), `NotificationQueue` (priority-черга банерів з ADR-001).
- **Знає лише:** доменну модель меню (`generated/display_screens.h`) + семантичний шов `IDisplayPort`. **Не знає** ні рядків/колонок, ні пікселів, ні кольорів, ні конкретного чипа.
- **Вихід:** замість `DisplayFrame` віддає **семантичні View** (`present_main/present_menu/present_notice`) і читає `capabilities()`.

### Шар 2 — ДРАЙВЕР-АДАПТЕР заліза (надає ендпоінти, інкапсулює layout)

- **Адаптер `XxxPort : IDisplayPort`** — міст «семантичний View → залізо». Бере на себе **весь layout**: скрол, видиме вікно, центрування, інверсію вибраного, мапінг level→колір/рамка, diff проти тіньового буфера. Живе у `modules/display/src/` (для вбудованих) **або** власному `drivers/display_xxx/` (для великих/окремо-постачених).
- **Переносний драйвер чипа** — низькорівневий bit-bang/SPI/I²C драйвер у `components/modesp_*` (спільний між проектами, без знання ModESP-семантики). Приклади: `components/modesp_osd/` (`At7456e`, майбутній `Amt630a`).

### Розкладка по дереву (узгоджена з трьома рівнями ModESP)

| Рівень | Призначення | Display-приклади |
|--------|-------------|------------------|
| **`components/`** | переносні низькорівневі драйвери чипів | `modesp_osd::At7456e`, `modesp_osd::Amt630a`, (майб.) `modesp_ssd1306::Ssd1306` |
| **`drivers/` або `modules/display/src/`** | адаптери `XxxPort : IDisplayPort` (layout + семантика→чип) | `At7456ePort`, `Amt630aPort`, `Ssd1306Port`, `LogPort` |
| **`modules/`** | UI-логіка / абстракція (BaseModule) | `modules/display/` (`DisplayModule`, `MenuEngine`, `NotificationQueue`) |

### Узгодження з патерном драйверів ModESP (Kconfig + інтерфейс)

> **РІШЕННЯ (узгоджено з користувачем 2026-06-14): БЕЗ повного `DisplayPortRegistry`.** Дисплей у системі — одинак, тож реєстр зайвий шар. Беремо мінімалістичний, але одно-патерновий варіант: **Kconfig `choice` для вибору backend + тонкий `default_port()` (як наявний `default_renderer()`, але повертає обраний `IDisplayPort*`) + DI `IDisplayPort&` у `DisplayModule`**. Тестованість зберігається через DI (host-тест інжектує fake-port напряму, без реєстру). Нижчеописаний registry-варіант лишено для контексту — **НЕ застосовується**.

Замінюємо `#ifdef`-вибір на той самий механізм, що для sensor/actuator — **1:1**, без чужорідних сутностей:

1. **Registry** (`DisplayPortRegistry`, аналог `DriverRegistry`): мапить ім'я backend → фабрику `IDisplayPort* (*)()`. `DisplayModule` бере port за іменем — **не знає жодного concrete-класу**.
2. **Макрос реєстрації** в `.cpp` адаптера (аналог `MODESP_REGISTER_ACTUATOR`):
   ```cpp
   static modesp::display::IDisplayPort* at7456e_port_factory() { return &s_at7456e_port; }
   MODESP_REGISTER_DISPLAY_PORT(at7456e, &at7456e_port_factory)
   ```
3. **Авто-Kconfig — `choice`, не `bool`.** Дисплей у системі **одинак** (на відміну від N датчиків), тож backend обирається радіо-вибором `choice MODESP_DISPLAY_BACKEND { at7456e | amt630a | ssd1306 | log }`. Вимкнені backend'и не компілюються (менший бінарник), точно як вимкнені драйвери.
4. **Генерований `display_port_register_all.h`** (аналог `driver_register_all.h`): викликає `modesp_register_display_port_<name>()` обраного backend під `#if CONFIG_...`.

**Чому НЕ повний `bindings.json` з `hardware_id` (як у sensor/actuator):** дисплей одинак, його піни вже задаються через власний Kconfig (`MODESP_DISPLAY_AT7456E_CS` тощо), а не як board hardware-ресурс. Повний binding-рядок надлишковий — достатньо **Kconfig `choice` + registry-lookup за іменем**. Це менший, але повністю одно-патерновий варіант (підтверджено звітом modesp-driver-conventions).

---

## 3. Шов `IDisplayPort`: остаточний вибір і ескіз інтерфейсу

### 3.1. Вибір рівня шва: **C — гібрид (семантичні ендпоінти + спільний helper для символьних дисплеїв)**

Розглянуто три рівні:

- **A) Низькорівневі примітиви (піксель/буфер, стиль LVGL/embedded-graphics).** **Відхилено.** Працює лише для однорідно-растрового парку. Для AT7456E/AMT630A (символьні tile-чипи з власним шрифтом у NVM/FONT-RAM) піксельний буфер — катастрофа: чип мусив би тримати software-framebuffer + растеризатор кирилиці в RAM → **heap у hot-path, прямо заборонено CLAUDE.md**. Найменший спільний знаменник нашого парку — **символ/рядок, не піксель**. Деградація йде лише в один бік: тонший пристрій (SSD1306) спрощується до символів — грубіший НЕ може емулювати тонший.

- **B) Чисто семантичні ендпоінти (intent).** Достатньо для коректності шва (тест на 3 backend'ах підтверджує: шов не протікає, якщо View несе логічні елементи + семантичні ролі). Але **залишає реальне дублювання layout** між двома символьними backend'ами (AT7456E + AMT630A-text): обидва — сітка `cols×rows`, скрол-вікно навколо `selected`, маркер курсора, центрування, обрізання. Без спільного коду це двічі написаний нетривіальний layout.

- **C) Гібрид = B + опційний спільний helper `CharGridLayout` для символьних драйверів.** **Обрано.** Інтерфейс лишається чисто семантичним (B), а повторюваний char-grid layout виноситься в **host-тестовану утиліту**, якою користуються лише символьні адаптери (AT7456E, AMT630A-text). SSD1306 (піксельний) і Log (текстовий) helper НЕ використовують — він **опційна утиліта, не базовий клас**. Це усуває головну ціну семантичного шва (дублювання рендеру), не нав'язуючи helper тим, кому він чужий.

**Чому C, а не B:** гіпотеза «семантичний шов тримає модуль чистим» підтверджена для **обох** B і C — модуль однаково чистий. Різниця між ними не в чистоті модуля, а в **дублюванні коду в драйверах**: B змушує AT7456E і AMT630A двічі реалізувати ідентичний char-grid layout; C дає їм спільну утиліту. Тому C строго кращий за B за ціною коду при тій самій чистоті шва. C обрано **за умови суворого формулювання** (тест three-backends): View несуть logical items + семантичні ролі, `Caps` несе лише можливості (НЕ геометрію), опційні апаратні параметри — capability-гейтовані.

### 3.2. Ескіз інтерфейсу (zero-heap, ETL)

```cpp
// modules/display/include/display/display_port.h
#pragma once
#include <cstdint>
#include "etl/string.h"
#include "etl/vector.h"
#include "modesp/types.h"   // StateValue

namespace modesp::display {

// ── Доменні View-структури (logical items + семантичні ролі; БЕЗ координат/кольорів/пікселів) ──

/// Стан FSM меню — СЕМАНТИЧНИЙ, не presentation-hint.
enum class ScreenKind : uint8_t { MAIN, MENU, EDIT };

/// Семантична важливість сповіщення. Драйвер мапить у колір/інверсію/рамку.
enum class NoticeLevel : uint8_t { INFO = 0, WARN = 1, ALARM = 2 };  // дзеркалить ADR-001

/// Один пункт головного екрана (idle): назва + відформатоване значення + одиниця.
struct MainItem {
    etl::string<24> label;
    etl::string<16> value;   // вже відформатоване движком (format рядок з маніфесту)
    etl::string<8>  unit;
};
static constexpr size_t MAX_MAIN_ITEMS = 8;   // логічний максимум, не екранний
struct MainView {
    etl::vector<MainItem, MAX_MAIN_ITEMS> items;
    bool has_menu = false;   // показати підказку входу в меню
};

/// Один пункт списку меню. editable=false → перегляд/підменю.
struct MenuItem {
    etl::string<24> label;
    etl::string<16> value;   // порожньо для SUBMENU/BACK
    bool is_submenu = false;
    bool is_back    = false;
};
static constexpr size_t MAX_MENU_ITEMS = 16;  // логічний максимум пунктів одного рівня
struct MenuView {
    etl::string<24>                       title;
    etl::vector<MenuItem, MAX_MENU_ITEMS> items;
    uint8_t                               selected = 0;   // ІНДЕКС, не рядок. Скрол рахує драйвер.
    ScreenKind                            kind = ScreenKind::MENU;  // MENU vs EDIT (стан FSM)
};

/// Банер сповіщення (ADR-001). level семантичний.
struct Notice {
    NoticeLevel     level = NoticeLevel::INFO;
    etl::string<48> text;
};

// ── Можливості (СЕМАНТИЧНІ, не геометрія!) — керують СКЛАДОМ меню, не виглядом ──
struct DisplayCaps {
    bool has_color        = false;  // AMT630A
    bool has_backlight    = false;  // AMT630A (PWM)
    bool has_video_params = false;  // AMT630A (brightness/contrast/saturation відео)
    bool has_inputs       = false;  // AMT630A (CVBS select)
    uint8_t input_count   = 0;
    // НЕ КЛАСТИ сюди cols/rows/width_px — це протікання розміру (див. §8 пастки).
};

// ── СЕМАНТИЧНИЙ ШОВ — те, що вміють УСІ backend'и ──
class IDisplayPort {
public:
    virtual ~IDisplayPort() = default;

    /// Ініціалізація заліза. false ⇒ дисплей вимкнено, система продовжує.
    virtual bool init() { return true; }

    /// Можливості — читаються модулем ОДИН раз (фільтрація складу меню).
    virtual DisplayCaps caps() const { return {}; }

    // present_* — модуль штовхає ПОВНИЙ семантичний стан. Diff/тіньовий буфер —
    // приватна справа драйвера (НЕ передавати дельти через шов).
    virtual void present_main(const MainView& view) = 0;
    virtual void present_menu(const MenuView& view) = 0;
    virtual void present_notice(const Notice& notice) = 0;
    virtual void clear_notice() = 0;

    // ── Скалярні параметри екрана: no-op default (вміють не всі) ──
    // Нормалізовано 0..100%. Драйвер мапить у свій діапазон.
    virtual void set_backlight(uint8_t pct)  { (void)pct; }
    virtual void set_contrast(uint8_t pct)   { (void)pct; }
    virtual void set_brightness(uint8_t pct) { (void)pct; }
    virtual void set_saturation(uint8_t pct) { (void)pct; }

    // ── Структурно-чужорідні можливості — capability-інтерфейси через as_*()→nullptr
    //    (zero-cost, без RTTI; патерн уже в AMT630A design doc §6.3 — as_graphic) ──
    virtual IVideoInputs*    as_video_inputs() { return nullptr; }  // CVBS select (лише AMT630A)
    virtual IGraphicRenderer* as_graphic()     { return nullptr; }  // icon/bar (лише AMT630A)
};

// Опційні capability-інтерфейси (тягнуть власні структури — не в базовому vtable):
class IVideoInputs {
public:
    virtual ~IVideoInputs() = default;
    virtual void    select_input(uint8_t n) = 0;
    virtual uint8_t input_count() const = 0;
};
class IGraphicRenderer {
public:
    virtual ~IGraphicRenderer() = default;
    virtual void draw_icon(uint8_t win, uint8_t x, uint8_t y, uint16_t tile) = 0;
    virtual void draw_bar (uint8_t win, uint8_t x, uint8_t y, uint8_t pct)  = 0;
};

} // namespace modesp::display
```

**Ключові інваріанти шва (з тесту three-backends):**

1. `MenuView.items` — `etl::vector<MenuItem, N>` логічних пунктів, **N = бізнес-максимум**, не екранний розмір. Скрол/видиме вікно рахує драйвер. (Виправляє головну ваду `DisplayFrame[4][40]`.)
2. `selected` — **індекс**, не номер рядка. `kind` — стан FSM (`MENU`/`EDIT`), **заборонено** presentation-семантику («компактно/2 колонки»).
3. `Notice.level` — семантичний enum. Мапінг level→(червоний AMT630A / інверсія AT7456E / рамка SSD1306) — у драйвері.
4. `present_*` штовхає **повний стан**; diff — приватний тіньовий буфер драйвера (AMT630A критично залежить від diff по I²C; AT7456E робить `clear()` щокадру — прийнятно для SPI).
5. `caps()` керує **складом** меню (чи показувати пункт «Вибір входу»/«Насиченість»), а не виглядом. `Caps` несе можливості, **не геометрію**.

---

## 4. Як кожен backend реалізує шов + спільні helper-и

### 4.1. Спільні helper-и (host-тестовані, без заліза)

- **`CharGridLayout`** (`modules/display/include/display/char_grid.h`) — **опційна утиліта для символьних драйверів**. Бере семантичний `MenuView`/`MainView` + `(cols, rows)` і повертає **сітку клітинок з атрибутами**: `Cell{ row, col, utf8/glyph, attr{invert, fg, bg, scale} }`. Інкапсулює: скрол-вікно навколо `selected`, маркер курсора / інверсію вибраного, вертикальне/горизонтальне центрування, обрізання рядка під `cols`, «back»-рядок. Це той layout, що зараз помилково живе в `MenuEngine` (`Level{scroll}`, `top_row_`). **НЕ базовий клас** — SSD1306 його не використовує.
- **UTF-8 декодер** — спільний (`osd::osd_decode_utf8`). А **таблиця cp→tile/glyph різна в кожного** (AT7456E NVM-розкладка ≠ AMT630A FONT-RAM-розкладка) — НЕ зливати.
- **Font-upload** — патерн уже є (`At7456e::upload_font` + sentinel-guard, NVM не зношується). AMT630A повторює цей патерн для свого 16×22 1bpp шрифту.

### 4.2. Реалізація по backend'ах

| Backend | Чип-драйвер | Як реалізує шов |
|---------|-------------|------------------|
| **LogPort** (default, без заліза) | — | `present_*` → `ESP_LOGI` рядки View як текст. `caps()` = усе false. Helper не потрібен. Збереження поведінки поточного `LogRenderer`. |
| **At7456ePort** (символьний) | `modesp_osd::At7456e` (SPI, 30×16/13) | `CharGridLayout(cols=30, rows=16)` → сітка клітинок; `osd_map_utf8` → індекси NVM-гліфів; `dev_.write_glyphs`. `selected`→інверсний атрибут char-RAM. `present_notice`→інверсний рядок. `caps()`: усе false (OSD-оверлей, не керує підсвіткою/входами). Font-upload в `init()`. |
| **Amt630aPort** (символьний + багатий) | `modesp_osd::Amt630a` (I²C, банки 0x58-0x5F) | `CharGridLayout(cols=20, rows=N)` → `osd_print` по BGMAP з **diff проти тіні** (clear щокадру шкідливий по I²C). `selected`→`FB10h` колір/інверсія. `present_notice`→окреме вікно-банер W0 (scale ×2, колір палітри за level, напівпрозорість). Реалізує `set_backlight/contrast/brightness/saturation`, `as_video_inputs()` (CVBS `select_input`), `as_graphic()` (icon/bar). `caps()`: усе true. |
| **Ssd1306Port** (піксельний) | (майб.) `modesp_ssd1306::Ssd1306` (I²C, 128×64) | **Helper НЕ використовує** — піксельний рендер шрифтом 6×8/8×16 у GDDRAM, скрол вручну, `selected`→XOR-прямокутник рядка, `present_notice`→рамка/інверс. `caps()`: усе false (контраст ≈ `set_contrast`). Layout приватний у драйвері. |

**Підтвердження не-протікання:** усі чотири споживають **лише семантичні View**; жоден не емулює чужу модель рендеру; helper спільний лише для двох символьних. Шов витримує всі три backend'и без протікання (тест three-backends, за умов §3.2).

---

## 5. Куди дівається `MenuEngine`/`NotificationQueue` і ADR-001

### `MenuEngine` — лишається в модулі, стає **повністю агностичним**

- **Лишається** у `modules/display/` як FSM меню: навігація MAIN/MENU/EDIT, редагування з clamp по min/max/step, idle-timeout, ротація, live-refresh, dirty-tracking. Споживає `generated/display_screens.h` через DI (`MenuData`) — без змін.
- **Втрачає layout:** замість `build_main/build_menu/build_edit` → `DisplayFrame` тепер будує **семантичні View** (`MainView/MenuView`). Скрол-вікно (`Level{scroll}`, `VISIBLE_ITEMS`), центрування, маркер курсора, «back»-рядок, рамки/чроме — **переїжджають у `CharGridLayout`/драйвер**. `MenuEngine` віддає `selected` як індекс і повний список logical items.
- Наслідок: зникає залежність `VISIBLE_ITEMS = MAX_ROWS - 1`. `MenuEngine` більше не знає геометрію екрана.

### `NotificationQueue` — лишається в модулі (ADR-001), агностична

- Priority-черга банерів (`etl::vector<MsgSystemNotice, 8>`), TTL у `on_update(dt_ms)`, preemption — **повністю за ADR-001, без змін логіки**. Живе в `DisplayModule`.
- Змінюється лише **вихід**: замість `renderer_->notify(text, level)` → `port_->present_notice(Notice{level, text})` / `port_->clear_notice()`. Дзеркало `display.banner*` у SharedState — без змін.

### Узгодження з ADR-001

ADR-001 (доставка сповіщень через `etl::message_bus` → `MsgSystemNotice` → priority-черга в `DisplayModule`) **повністю сумісний** і не переглядається. ADR-002 лише перейменовує точку виходу банера: `IDisplayRenderer::notify()` → `IDisplayPort::present_notice()/clear_notice()`. `NoticeLevel` уніфікується (один enum для ADR-001 і §3.2). Z-order банера над меню — рішення драйвера (на AMT630A вікно W0 поверх W1; на символьних банер витісняє рядок — гарантії одночасності немає).

---

## 6. План міграції (backend'и зеленими на кожному кроці)

Міграція **інкрементальна, зворотно-сумісна**, без «великого вибуху». Інваріант: після кожного кроку прошивка білдиться, host-тести зелені.

> **Уточнення (steer 2026-06-14): чиста універсальна архітектура, БЕЗ парності з legacy AT7456E.** `CharGridLayout` проєктується з нуля — повна сітка `cols×rows` (без 4-рядкового центрування), семантичні `RowRole` (TITLE/SELECTED/HINT/NORMAL); презентацію ролей (колір/інверсія/маркер `>`/чрома `== ==`/масштаб) вирішує **драйвер**, не layout. Мета тестів — коректність **чистої** поведінки (скрол/курсор/обрізання-по-гліфах/багаторядковість на заданих cols×rows), а **НЕ** байт-в-байт відтворення старого виводу AT7456E. Старі `AT7456ERenderer` / `MenuEngine::build_*(DisplayFrame)` / `DisplayFrame` — **замінюються, не зберігаються**; `test_display_menu.cpp` переписується під View-модель (Крок 3), не використовується як golden-еталон.

1. **Ввести `IDisplayPort` + View-структури** поряд із наявним `IDisplayRenderer` (новий заголовок, нічого не ламає). Додати `DisplayCaps`, capability-інтерфейси.
2. **Витягти `CharGridLayout`** як host-тестовану утиліту з наявного `MenuEngine::build_*`/`at7456e_renderer.cpp:top_row_`. Покрити host-тестами (скрол, курсор, центрування, обрізання) — еталон поведінки до рефактора.
3. **`MenuEngine` → віддає View** замість `DisplayFrame`. Старий `build_*`(DisplayFrame) тимчасово зберегти за прапором/адаптером, щоб порівняти кадри в тестах (golden-frame regression: семантичний View → `CharGridLayout(30×16)` має дати той самий вивід, що старий AT7456E-шлях).
4. **Адаптери: `LogPort` + `At7456ePort`** реалізують `IDisplayPort` (LogPort тривіально; At7456ePort = `CharGridLayout` + наявний `write_glyphs`). Перевірити: вивід байт-в-байт як зараз.
5. **`DisplayPortRegistry` + `MODESP_REGISTER_DISPLAY_PORT` + Kconfig `choice` + генерований `display_port_register_all.h`.** `DisplayModule` бере port з registry за обраним backend — **прибрати `#ifdef`/`#include` конкретного класу з модуля** (`display_module.cpp:12-14,34-45`). Згенерувати toggle у `generate_ui.py` (як для драйверів).
6. **Перенести `NotificationQueue` вихід** на `present_notice/clear_notice`.
7. **Видалити `IDisplayRenderer`/`DisplayFrame`** після того, як обидва наявні backend'и зелені на новому шві. Оновити `renderer.h` → `display_port.h`.
8. **(Окремо) `Ssd1306Port` + `Amt630aPort`** — уже zero-edit у модулі (лише новий адаптер + чип-драйвер + рядок Kconfig-choice), рівно як новий sensor/actuator.

**Що НЕ зламати:**
- DI-контракт `MenuEngine(IMenuStateIO&, MenuData)` — `test_display_menu.cpp` спирається на нього; зберегти.
- `display.enabled/btn_*/screen` SharedState-контракт — без змін.
- `init()` failure ⇒ display disabled, система живе (`on_init` повертає true) — зберегти.
- Згенерований `display_screens.h` і `DisplayScreensGenerator` — не чіпати (домен незмінний).
- Zero-heap: View несуть `etl::string`/`etl::vector` фіксованого розміру — без heap у hot-path.

---

## 7. Наслідки + оновлення AMT630A design-доку

### Позитив

- **Чистий модуль:** `DisplayModule`/`MenuEngine` не знають геометрії, кольору, пікселів, чипа — лише intent. Додавання дисплея = новий адаптер + чип-драйвер + рядок Kconfig, **нуль правок абстракції** (як новий драйвер).
- **Консистентність із ModESP:** display-вибір тепер той самий патерн, що sensor/actuator (registry + Kconfig + register-all). Жодної чужорідної сутності.
- **3 backend'и без протікання:** AMT630A (колір/вікна/входи/графіка), AT7456E (символьний), SSD1306 (піксельний), Log — усі за одним швом; кожен робить свій layout.
- **Дублювання layout усунуто** для символьних через `CharGridLayout`; пікселний SSD1306 не обтяжений чужим helper'ом.
- **Геометрія більше не зашита** — AMT630A отримує свої 16+ рядків, SSD1306 — свої 4–8.

### Негатив / на що зважати

- Більше шарів і структур, ніж у поточному `DisplayFrame` — виправдано лише тому, що backend'ів реально ≥3 і вони різнотипні (hexagonal: адаптери виправдані при >1 різнотипному призначенні).
- `CharGridLayout` — спільний код двох драйверів; треба не піддатися спокусі зробити його обов'язковим базовим класом (зламає SSD1306).
- View-структури копіюються щокадру (zero-heap, але копія `etl::vector`). Прийнятно при 100 Гц з фіксованими N; diff лишається в драйвері.

### Оновлення `AMT630A_driver_design.md` (AMT630A стає адаптером під ADR-002)

1. **§6.2/§6.3 — `AMT630ARenderer` → `Amt630aPort : IDisplayPort`.** Замінити `render(DisplayFrame)` на `present_main/present_menu/present_notice/clear_notice`. `RowAttr`-розширення `DisplayFrame` (§6.3 п.1) **скасувати** — атрибути тепер несе `CharGridLayout::Cell`/семантичний `MenuView`, а не зворотно-сумісний хак над `DisplayFrame`.
2. **§6.3 п.1 (`RowAttr` у `DisplayFrame`)** — замінити на: «колір/scale/інверсія обчислюються `Amt630aPort` з `MenuView.selected` + `caps().has_color` + level». `DisplayFrame` більше не існує.
3. **§6.3 п.2 (`as_graphic()→nullptr`)** — **зберегти, узгоджено** з §3.2 цього ADR (тепер метод `IDisplayPort`). Додати `as_video_inputs()` тим самим патерном для CVBS-select.
4. **§4 параметри екрана (`set_backlight/contrast/brightness/saturation`, `select_input`)** — `set_*` стають no-op-default методами `IDisplayPort` (AMT630A перевизначає); `select_input` переїжджає в `IVideoInputs` (capability-гейт). Меню «Вибір входу»/«Насиченість» показувати **лише якщо `caps().has_inputs`/`has_video_params`**.
5. **§6.3 п.3 (сповіщення)** — уже за ADR-001; уточнити точку виходу: `present_notice(Notice)`/`clear_notice()` замість `notify(text, level)`.
6. **§ roadmap (етапи 4–7)** — переформулювати під ADR-002: етап 5 «RowAttr у DisplayFrame» → «семантичний MenuView + CharGridLayout»; решта без зміни суті.

---

## 8. Відкриті питання

1. **Розмір View-векторів.** `MAX_MENU_ITEMS=16` / `MAX_MAIN_ITEMS=8` — логічні бізнес-максимуми. Чи вистачить для найбільшого продукту? Звірити з реальними маніфестами (зараз `simple_thermo` — одиниці пунктів). Ризик: занизько → обрізання меню; зависоко → RAM на копію щокадру.
2. **`CharGridLayout` — де живе фізично.** `modules/display/include/display/` (доступний обом символьним адаптерам) чи окремий `components/modesp_chargrid` (якщо знадобиться поза display)? Поки що — в модулі.
3. **`as_graphic()` і меню.** Графічні елементи (бари/іконки) поки не мають доменної моделі в маніфестах — `MenuView` їх не виражає. Чи розширювати маніфест-стандарт, чи лишити графіку поза меню (лого/банери)? Відкласти до етапу 7 AMT630A.
4. **Diff vs повний present для символьних.** AT7456E зараз `clear()` щокадру (SPI — ок). При переході на `present_menu` варто додати тіньовий буфер і в At7456ePort для консистентності, чи лишити clear? Бенч SPI-трафіку.
5. **`Ssd1306Port` шрифт.** Векторний 6×8 vs власний кириличний (як AT7456E NVM) — SSD1306 не має NVM, шрифт у flash. Узгодити з `gen_osd_font.py` (третя ціль після at7456e/amt630a).
6. **Registry vs одинак — ВИРІШЕНО (2026-06-14, з користувачем): БЕЗ registry.** Дисплей одинак → Kconfig `choice` + тонкий `default_port()` + DI `IDisplayPort&` у `DisplayModule`. Тестованість (fake-port у host-тестах) забезпечує DI, а не реєстр. Простіше, без чужорідного шару, узгоджено з рішенням «дисплей один».
