# 08 — Поверхня інтеграції AMT630A в ModESP

> Дослідницький звіт: куди і як вставити `AMT630ARenderer` у модуль `modules/display/`,
> як `DisplayFrame` мапиться на BGMAP AMT630A, і які розширення потрібні
> інтерфейсу `IDisplayRenderer`/`DisplayFrame` для багаторозмірного+багатоколірного
> тексту, графіки та сповіщень.
>
> Джерела (read-only): репо ModESP (`modules/display/`, `components/modesp_osd/`,
> `tools/gen_osd_font.py`, `generated/display_screens.h`,
> `components/modesp_core/include/modesp/types.h`), канонічна спека
> `docs/amt630a/AMT630A_user_spec.md`, доведений драйвер Fizik `AMT630.h`.

---

## 0. TL;DR

- AMT630A підключається **рівно тим самим патерном, що й AT7456E**: новий клас
  `modesp::display::AMT630ARenderer : public IDisplayRenderer`, скомпільований під
  Kconfig-прапором `CONFIG_MODESP_DISPLAY_AMT630A`, інстанс у `display_module.cpp`,
  гілка в `default_renderer()`. Жодних змін у `MenuEngine`/`DisplayModule` API не треба
  для базового текстового кадру.
- Транспорт **інший**: AT7456E = bit-bang SPI на `modesp_osd::At7456e`; AMT630A = **I²C
  master ESP-IDF** до вже працюючого SoC (банки `0x58–0x5C`). Тому **потрібен новий
  переносний драйвер** `components/modesp_osd/.../amt630a.h/.cpp` (паралель до `at7456e.*`),
  а `AMT630ARenderer` — тонкий міст `DisplayFrame → osd_print()`.
- `DisplayFrame` (4×40, чистий UTF-8 текст) мапиться на **Window 0** BGMAP AMT630A 1-в-1:
  рядок кадру → ряд символів BGMAP; codepoint → номер символу через карту, аналогічну
  `osd_charmap.h`, але з **ІНШОЮ розкладкою гліфів** (див. §3 — це головний конфлікт).
- Поточний `DisplayFrame` **недостатній** для багаторозмірного/багатоколірного тексту,
  графіки і сповіщень. Запропоновано **зворотно-сумісне** розширення (per-row атрибути +
  опційний overlay-канал) — §5.

---

## 1. Контракт `IDisplayRenderer` / `DisplayFrame` (як є зараз)

`modules/display/include/display/renderer.h`:

```cpp
struct DisplayFrame {
    static constexpr size_t MAX_ROWS  = 4;    // OLED 128×64, шрифт 8px
    static constexpr size_t MAX_BYTES = 40;   // UTF-8 байтів на рядок
    etl::string<MAX_BYTES> rows[MAX_ROWS];
    void clear();
    bool operator==/!=(...) const;            // покадрове порівняння для dirty-логіки
};

class IDisplayRenderer {
public:
    virtual ~IDisplayRenderer() = default;
    virtual bool init() { return true; }                  // true = залізо готове
    virtual void render(const DisplayFrame& frame) = 0;   // лише коли кадр змінився
};
```

Ключові властивості контракту (їх **мусить** дотримати будь-який рендерер):

| Властивість | Деталь | Джерело |
|-------------|--------|---------|
| Рядки — **UTF-8 байти**, не гліфи | `MAX_BYTES=40` — це байтовий ліміт; багатобайтова кирилиця ≈ 2 байти/символ → ~20 кир. символів/рядок | `renderer.h:8`, `menu_engine.cpp:append_utf8` |
| `render()` викликається **лише при зміні** | `DisplayModule::on_update` → `if (engine_.consume_dirty()) renderer_->render(...)` | `display_module.cpp:112` |
| `init()` повертає `false` → дисплей вимикається, **система не падає** | `on_init`: `if (!renderer_->init()) { ... return true; }` | `display_module.cpp:72` |
| Скільки колонок видно — **вирішує рендерер** | MenuEngine форматує до 40 байт; рендерер ріже/центрує сам | `renderer.h:8`, `at7456e_renderer.cpp:write_glyphs(col=1)` |

`MenuEngine` будує кадр у 3 станах (FSM `MAIN/MENU/EDIT`), всі рядки — чистий текст
з маркерами `>`, `==`, `[OK]`, `:`. Жодного поняття кольору/розміру/графіки в кадрі **немає**.

---

## 2. Патерн дзеркалення: AT7456E renderer (зразок для AMT630A)

`AT7456ERenderer` (`at7456e_renderer.h/.cpp`) — еталон, який треба скопіювати:

```cpp
// at7456e_renderer.cpp — увесь TU під ifdef
#ifdef CONFIG_MODESP_DISPLAY_AT7456E
class AT7456ERenderer : public IDisplayRenderer {
    osd::At7456e dev_;                 // переносний драйвер з modesp_osd
    uint8_t top_row_;                  // вертикальне центрування кадру
public:
    bool init() override {
        dev_.init();
        if (!dev_.present()) return false;             // → дисплей disabled
        if (font_) dev_.upload_font(font_, ...);       // власний шрифт у NVM (sentinel-skip)
        top_row_ = (dev_.rows() > MAX_ROWS) ? (dev_.rows()-MAX_ROWS)/2 : 0;
        dev_.clear(); dev_.enable_osd(true);
        return true;
    }
    void render(const DisplayFrame& frame) override {
        dev_.clear();
        for (uint8_t r = 0; r < DisplayFrame::MAX_ROWS; ++r) {
            const auto& row = frame.rows[r];
            if (row.empty()) continue;
            uint8_t idx[30];
            size_t n = osd::osd_map_utf8(row.c_str(), idx, sizeof(idx)); // UTF-8 → glyph idx
            dev_.write_glyphs(/*col*/1, top_row_ + r, idx, n);
        }
    }
};
#endif
```

Підключення (`display_module.cpp`):

```cpp
#ifdef CONFIG_MODESP_DISPLAY_AT7456E
modesp::display::AT7456ERenderer s_at7456e_renderer;
#endif
IDisplayRenderer* default_renderer() {
#ifdef CONFIG_MODESP_DISPLAY_AT7456E
    return &s_at7456e_renderer;
#else
    return &s_log_renderer;     // LogRenderer — кадр у серійний лог
#endif
}
```

CMake-нюанс (`modules/display/CMakeLists.txt`): `at7456e_renderer.cpp` компілюється
**завжди** (REQUIRES не може залежати від Kconfig), але стає порожнім TU без прапора;
linker викидає його через `-ffunction-sections/gc-sections`.

**Висновок:** AMT630A йде тим самим шляхом — три точки дотику (новий TU рендерера,
інстанс, гілка `default_renderer()`) + один блок у Kconfig.

---

## 3. Карта `DisplayFrame → BGMAP AMT630A`

### 3.1 Геометрія
AT7456E: апаратна сітка PAL 16×30 / NTSC 13×30, фіксований розмір символу.
AMT630A: **Window 0** — довільні `width`(`FB07h`,1–127) × `height`(`FB08h`,1–63) у
символах, розмір символу `FB76h`(X 1–24) / `FB77h`(Y 1–32) px, позиція `FB0Ah/0Bh`.
→ Маппінг `DisplayFrame` (4×40) тривіальний: `width = MAX_BYTES`(або фактична ширина),
`height = MAX_ROWS = 4`. Рядок `r` кадру = ряд `r` BGMAP, BGMAP-адреса рядка = `r * width`.

### 3.2 Запис рядка (доведений патерн)
Спека §5 + Fizik `AMT630::osdPrint` дають однаковий алгоритм (dev=`0x5B`=OSD):

```c
// BGMAP стартова адреса рядка
amt_w(0x5B, 0x0D, (addr>>8)&1);   // FB0Dh addr msb (вручну при переповненні lsb)
amt_w(0x5B, 0x00, addr & 0xFF);   // FB00h addr lsb (АВТО-інкремент при записі data!)
amt_w(0x5B, 0x10, attr);          // FB10h атрибут (FG bit0-2, BG bit4-6)
amt_w(0x5B, 0x0E, 0x00);          // FB0Eh char msb — раз на рядок (коди <256)
for (i...)  amt_w(0x5B, 0x01, glyph[i]); // FB01h char lsb → у VRAM, addr++
```

Нюанси (реверс Korth, спека §5):
- `addr_lsb` (`FB00h`) авто-інкрементується **лише** при записі `data_lsb` (`FB01h`);
  переносу в `addr_msb` немає — стежити за переповненням 8-біт **самому**.
- **Читання BGMAP/FONT неможливе** → тримати тіньову копію в RAM ESP32 (вже є:
  `MenuEngine::frame_` і dirty-логіка дають останній кадр; рендерер може кешувати
  попередній, щоб писати лише змінені рядки/символи).
- Текстові оновлення BGMAP **атомарні й безпечні** під час активного відео — мерехтіння,
  що було на AT7456E, на AMT630A **не відтворюється** (спека §9). Тому `dev.clear()`
  перед кожним кадром (як у AT7456E renderer) тут **не потрібен** і навіть шкідливий
  (зайвий I²C-трафік + можливий blank). Краще diff-оновлення.

### 3.3 Карта codepoint → номер символу (КЛЮЧОВИЙ КОНФЛІКТ ДЖЕРЕЛ)

| Платформа | ASCII '0'–'9' | ASCII 'A'–'Z' | Пробіл | Кирилиця | Джерело |
|-----------|---------------|---------------|--------|----------|---------|
| **AT7456E** (`osd_charmap.h`) | **identity** `0x30–0x39` | **identity** `0x41–0x5A` | `0x20` | RAM-шрифт `0x80–0xC7` (А-я суцільно) | `osd_charmap.h`, `gen_osd_font.py` |
| **AMT630A ROM** (Fizik `osdFontCode`) | `0x01–0x0A` | `0x0B–0x24` | `0x00` | ROM `02Ch–04Ch` «нетиповий порядок» / RAM `1C0h+` | `AMT630.h:163`, спека §4 |

> **Конфлікт:** розкладки гліфів AT7456E і AMT630A **несумісні**. `osd_charmap.h::osd_map_utf8`
> **не можна** перевикористати для AMT630A напряму — потрібна **окрема карта** (напр.
> `amt630a_charmap.h`): ASCII зсунутий (`'0'→0x01`, `'A'→0x0B`), пробіл `→0x00`,
> кирилиця → кастомні номери RAM-шрифту `1C0h+`. Fizik покриває лише цифри+латиницю;
> повна кирилиця потребує завантаження власного шрифту у FONT RAM (спека §7) і власної
> номерації — рекомендовано вибрати суцільний блок `1C0h..` за конвенцією, аналогічною
> `osd_charmap.h` (А=`0x1C0`, ...), і згенерувати таблицю варіантом `gen_osd_font.py`
> (формат тайлу інший: 1bpp 16×22, слова `FB03h/04h`, а не 2bpp 12×18 .mcm — див. §4).

### 3.4 Атрибут кольору (`FB10h`)
`bit0–2` = колір тексту (`0`=прозорий, `1–6`=палітра `FB56h..FB61h`, `7`=чорний);
`bit4–6` = колір фону. Поточний `DisplayFrame` кольору **не несе** → дефолт `attr=0x09`
(Fizik: FG=білий-палітра, BG=прозорий). Багатоколірність потребує розширення кадру (§5).

---

## 4. Потрібний новий driver-компонент (`modesp_osd::Amt630a`)

Дзеркало `modesp/osd/at7456e.h`, але I²C-транспорт ESP-IDF (`driver/i2c_master.h`).
Мінімальний публічний API, який покриває §3, §7, §10, спеки:

```cpp
namespace modesp::osd {
struct Amt630aPins { int sda; int scl; uint32_t freq_hz; };  // або готова i2c_master_bus

class Amt630a {
public:
    bool init();                              // bus + (опц.) §13 startup; present()-чек
    bool present();                           // probe 0x5B ACK
    void amt_w(uint8_t dev7, uint8_t reg, uint8_t val);   // §2 транзакція
    uint8_t amt_r(uint8_t dev7, uint8_t reg);             // write-ptr + repeated-start

    // OSD (dev 0x5B):
    void window0_setup(uint8_t w, uint8_t h, uint8_t x, uint8_t y,
                       uint8_t char_w, uint8_t char_h);    // FB05/07/08/09/0A/0B/76/77
    void osd_print(uint16_t bgmap_addr, const uint8_t* glyphs, size_t n,
                   uint8_t attr);                          // §5 алгоритм
    void set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b); // FB56..FB61
    void upload_font_glyph(uint16_t font_addr, const uint16_t* words, size_t nwords); // §7
    void upload_font(...);                    // вимкнути вікна FB05=0 → запис → відновити

    // Brightness/PWM (dev 0x58, ОБЕРЕЖНО — лише FD1Fh–FD29h):
    void set_backlight_duty(uint16_t total, uint16_t high);  // §8
};
}
```

**DANGER-обмеження (спека §12) — закодувати в драйвер як заборонені:** ніколи не писати
`FDD0h`, `FDDEh`bit6, `FDE0h`, `FD32h/FD33h` (банк `0x58`), `FFD5h`bit7 (банк `0x5A`).
Тримати whitelist банків: OSD `0x5B`, AV `0x59`, яскравість `0x5A`, PWM-частина `0x58`
(`0x1F–0x29`). Це варто винести в код-коментар + (опц.) debug-assert на адресу регістра.

**Формат шрифту для §7 ≠ формат `gen_osd_font.py`:** генератор зараз пакує 12×18 2bpp у
`.mcm`/`OSD_FONT[]` (54 байти/гліф) для AT7456E NVM. AMT630A FONT RAM — **1bpp 16×22**,
слова по 16 біт (`FB03h` lsb=лівий піксель, `FB04h` msb). → Потрібен **окремий режим
генератора** (`--target amt630a`): рендер 16×22, пакування у `uint16_t[]` слова, таблиця
`AMT630A_FONT[]` + карта `cp→font_addr`. Це окрема задача (поза цим звітом).

---

## 5. Потрібні РОЗШИРЕННЯ `IDisplayRenderer`/`DisplayFrame`

Поточний `DisplayFrame` — **простий монохромний текст 4×40**. AMT630A дає 5 вікон,
6 кольорів, масштаб ×2–×5, графіку (4bpp RAM-шрифт), напівпрозорість. Щоб це
використати — кадр треба збагатити. Принцип: **зворотна сумісність** (старі рендерери
й `MenuEngine` працюють без змін; нові поля опційні, дефолти = поточна поведінка).

### 5.1 Per-row атрибути (колір + розмір) — мінімальне, не ламає API

```cpp
struct RowAttr {
    uint8_t fg     : 3;   // 0=прозорий,1-6 палітра,7 чорний  (мапа на FB10h)
    uint8_t bg     : 3;
    uint8_t scale  : 2;   // 0=×1 ... 3=×4   (FB32h масштаб вікна / FB76/77 розмір)
    uint8_t flags;        // bit0=blink, bit1=center, bit2=invert ...
};
struct DisplayFrame {
    static constexpr size_t MAX_ROWS  = 4;
    static constexpr size_t MAX_BYTES = 40;
    etl::string<MAX_BYTES> rows[MAX_ROWS];
    RowAttr attrs[MAX_ROWS] = {};   // дефолт {fg=1(біл.),bg=0(прозор.),scale=0} = поточна поведінка
    // operator== має враховувати attrs для dirty-логіки
};
```

- Текстові рендерери (LogRenderer, AT7456E) `attrs` **ігнорують** — поведінка та сама.
- AMT630A renderer читає `attrs[r]` → `FB10h` атрибут + `FB76/77` розмір.
- `MenuEngine` можна **поступово** навчити ставити `attrs` (напр. `MAIN`-температура крупно,
  заголовок `==..==` іншим кольором) — але це опційно, не блокує інтеграцію.

### 5.2 Багаторозмірний текст
AMT630A: розмір символу = `FB76h`(X)/`FB77h`(Y) px + масштаб вікна `FB32h`(×2–×5).
Велика проблема: один `Window` має **єдиний** розмір символу. Для дійсно різних розмірів
у різних рядках треба **різні вікна** (Window 0–4). → Розширення §5.1 `scale` найкраще
реалізувати **розкладанням рядків по вікнах** усередині AMT630A renderer
(напр. великий рядок MAIN → Window 1 з масштабом, решта меню → Window 0). Контракт
кадру цього не нав'язує — лише дає рендереру `scale`-підказку.

### 5.3 Графіка / іконки / бари
Потрібен **окремий overlay-канал** поза текстовою сіткою (статус-іконки, бари сигналу/
батареї — у ROM AMT630A вже є `137h–13Bh`, спека §4; 4bpp RAM-шрифт для 16-кольор. графіки).
Пропозиція — новий **необов'язковий** інтерфейс-розширення, який AMT630A renderer реалізує,
а текстові — ні:

```cpp
class IGraphicRenderer {                 // optional capability
public:
    virtual ~IGraphicRenderer() = default;
    virtual void draw_icon(uint8_t win, uint8_t x, uint8_t y, uint16_t glyph) = 0;
    virtual void draw_bar (uint8_t win, uint8_t x, uint8_t y, uint8_t pct) = 0;
};
// AMT630ARenderer : public IDisplayRenderer, public IGraphicRenderer
// DisplayModule робить dynamic-ішну перевірку: if (auto* g = renderer_as<IGraphicRenderer>())
```

(Без RTTI: додати `virtual IGraphicRenderer* as_graphic() { return nullptr; }` у
`IDisplayRenderer` — zero-cost, дефолт `nullptr`.)

### 5.4 Сповіщення / system-messages — **зараз відсутні в модулі**

Grep по `modules/display/` (`notif|alarm|notify|toast|popup|system.message`) → **0 збігів**.
Тобто маршрутизації сповіщень, згаданої в ТЗ, у модулі **ще немає** — це треба
**спроєктувати з нуля**. ModESP має готову інфраструктуру повідомлень
(`modesp/types.h::msg_id`): `ALARM_TRIGGERED=150`, `ALARM_CLEARED=151`, `SYSTEM_ERROR=2`,
`SYSTEM_SAFE_MODE=7`. Рекомендована схема:

1. **Транспорт через SharedState** (узгоджено з кнопками `display.btn_*`): додати ключі
   `display.notify` (string<32>) + `display.notify_level` (int: info/warn/alarm) +
   `display.notify_ms`. Бізнес-модулі/alarm пишуть туди; `DisplayModule::on_update`
   читає й вставляє **overlay-рядок поверх кадру** на N мс (поверх FSM, з пріоритетом
   над MAIN/MENU/EDIT), потім очищає.
2. У `DisplayFrame` — опційний **банер**:
   ```cpp
   struct Banner { etl::string<40> text; uint8_t level; bool active; };
   Banner banner = {};   // active=false → рендерер ігнорує
   ```
   AMT630A renderer малює банер окремим вікном (Window 1) червоним (палітра) з
   напівпрозорістю (`FB06h` bit7, рівень `FB0Ch`) — текст поверх живого відео не
   закриваючи картинку (спека §6). Текстові рендерери — у вільний рядок або лог.
3. `MenuEngine` лишається без сповіщень; банер — рівень `DisplayModule` (він уже єдина
   точка, що читає SharedState і має доступ до рендерера).

> Це **нова функціональність**, не реверс. Перед кодуванням варто узгодити ключі
> сповіщень з рештою фреймворку (alarm-модуль, error_service).

---

## 6. Точний план інтеграції (мінімальний, текстовий кадр)

| # | Файл | Дія |
|---|------|-----|
| 1 | `components/modesp_osd/include/modesp/osd/amt630a.h` + `src/amt630a.cpp` | Новий I²C-драйвер (§4). REQUIRES `driver` (i2c_master). |
| 2 | `components/modesp_osd/include/modesp/osd/amt630a_charmap.h` | Окрема карта codepoint→номер символу (§3.3) — **не** перевикористовувати `osd_charmap.h`. |
| 3 | `modules/display/include/display/amt630a_renderer.h` + `src/amt630a_renderer.cpp` | `class AMT630ARenderer : public IDisplayRenderer`, увесь TU під `#ifdef CONFIG_MODESP_DISPLAY_AMT630A` (дзеркало AT7456E). |
| 4 | `modules/display/Kconfig` | Новий блок `MODESP_DISPLAY_AMT630A` (I²C SDA/SCL/freq, CVBS-вхід, PWM-duty). **Зробити взаємовиключним** з `MODESP_DISPLAY_AT7456E` (`choice`), бо `default_renderer()` вибирає один. |
| 5 | `modules/display/src/display_module.cpp` | `#ifdef CONFIG_MODESP_DISPLAY_AMT630A` інстанс `s_amt630a_renderer` + гілка в `default_renderer()` + include. |
| 6 | `modules/display/CMakeLists.txt` | Додати `src/amt630a_renderer.cpp` у SRCS (порожній TU без прапора, як AT7456E). |
| 7 | `tools/gen_osd_font.py` | (Окрема задача) режим `--target amt630a`: 1bpp 16×22, `uint16_t[]` слова, таблиця `cp→font_addr`. |

Жодних змін у `MenuEngine`, `IMenuStateIO`, `manifest.json` для базового кадру не треба.
Розширення §5 (RowAttr/Banner/IGraphicRenderer) — окремий, зворотно-сумісний крок.

---

## 7. Хардверні / стартові деталі (з §13 спеки + Fizik)

- Прошивка AMT630A вже піднімає декодер/PLL/LCD-тайминг сама → `init()` рендерера лише
  **поверх** працюючого чіпа: probe `present()` (ACK на `0x5B`), (опц.) backlight 50%
  (`FD20/21=0x1000`, `FD28/29=0x0800`, `FD1F=0x01`, dev `0x58`), вибір CVBS
  (`FED7→FED8→FED7→FEDC`, dev `0x59`), завантаження кирилиці у FONT RAM `1C0h+`
  (вимкнути вікна `FB05=0` на час!), `window0_setup`, палітра.
- I²C 7-біт адреси (звірено спека §2 ↔ Fizik `AMT630.h`): Global `0x58`, AV `0x59`,
  яскравість `0x5A`, **OSD `0x5B`**, LCD/Tcon `0x5C`. (Fizik також юзає `0x5F` для
  низькорівневого init — це поза практичним OSD-набором; спека його не документує як банк.)
- `writeCommand` Fizik має `delay(10)` після кожної транзакції — для ESP-IDF i2c_master
  з ACK це зайве; покладатися на ACK/таймаут шини.

---

## 8. Перелік невирішених конфліктів між джерелами

1. **Розкладка гліфів AT7456E ≠ AMT630A** (головне): `osd_charmap.h` (ASCII identity,
   кирилиця `0x80+`) проти AMT630A ROM (`'0'→0x01`, `'A'→0x0B`, пробіл `0x00`, кирилиця
   `02Ch–04Ch` «нетиповий порядок»). → Потрібна **окрема** `amt630a_charmap.h`; повна
   кирилиця = власний RAM-шрифт `1C0h+` з власною номерацією. Fizik покриває лише
   цифри+латиницю — для меню недостатньо.
2. **Формат шрифту:** `gen_osd_font.py` робить 12×18 2bpp `.mcm`/`OSD_FONT[]` (AT7456E NVM);
   AMT630A FONT RAM = 16×22 1bpp, 16-біт слова. Несумісно — потрібен новий режим генератора.
3. **`0x58` в Fizik названо `TFT_LCD_REG`**, але спека §2 прямо зазначає, що `0x58`=Global
   (PWM/ADC/SPI-flash/PLL), а Tcon/LCD = `0x5C`; Fizik «LCD названо 0x58 помилково».
   → При портуванні Fizik-послідовностей звіряти призначення банку по спеці, **не** по назві.
4. **Сповіщення/system-messages** у `modules/display/` **відсутні** (grep=0). ТЗ припускає
   їхню наявність — насправді це треба **спроєктувати** (§5.4), а не «дзеркалити».
5. **PWM-яскравість** (§8 спеки) спрацює по I²C лише якщо пін PWM0 вже в PWM-режимі;
   інакше потрібен CPU-SFR (недоступний по I²C) → економія лише через реплейс прошивки.
   Перевірити на бенчі.
6. **vsync по I²C недоступний** (`SFR 91h` — CPU-only). Для тексту BGMAP не критично
   (атомарні записи), але важка кадрова графіка ззовні не синхронізується — аргумент
   за Шлях B (поза цим звітом).
