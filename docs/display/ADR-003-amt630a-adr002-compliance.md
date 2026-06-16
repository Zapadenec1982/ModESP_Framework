# ADR-003 — Приведення реалізації AMT630A до відповідності ADR-002 (чистий шов + 3 шари)

- **Статус:** ЧЕРНЕТКА — на рев'ю архітектора (код НЕ змінюється до затвердження)
- **Дата:** 2026-06-16
- **Контекст:** ModESP Framework (ESP-IDF, C++17 + ETL, zero-heap у hot-path, single main-loop task)
- **Спирається на:** [ADR-002](ADR-002-display-architecture.md) (дворівнева display-архітектура, семантичний шов `IDisplayPort`), [REVIEW-display-subsystem](REVIEW-display-subsystem.md) (борг arch-1/2/5/6)
- **Причина:** під час bench-розробки AMT630A (вибір входу, no-signal, калібровка, power, recovery) у семантичний шов і абстрактний модуль протекли **геометрія** та **залізна lifecycle** — пряме порушення ADR-002.

---

## 1. Інваріанти ADR-002 (еталон)

| Шар | Дерево | Знає |
|---|---|---|
| Чіп-драйвер | `components/modesp_osd::Amt630a` | лише I²C-регістри; **0 ModESP-семантики** |
| Адаптер `Amt630aPort : IDisplayPort` | `modules/display/src/` | семантичний View→чіп, **увесь layout** (`CharGridLayout`), capabilities |
| Абстрактний модуль | `modules/display/` (`DisplayModule`, `MenuEngine`) | **лише intent** — НЕ пікселі/кольори/чіп/клавіші |

Дозволена поверхня шва `IDisplayPort`: `init()`, `caps()`, `present_main/menu/edit/notice`, `clear_notice()`, нормалізовані `set_backlight/contrast/brightness/saturation` (0..100%), `as_video_inputs()/as_graphic()`.
**Заборонено у шві:** геометрія (cols/rows/px), залізна lifecycle (ADR-002 §3.2).

---

## 2. Порушення (з посиланнями)

### 2.1 Внесені нещодавно (регресії проти ADR-002)
| # | Порушення | Файл | Чому порушує |
|---|---|---|---|
| **V1** | `set_calibration(int dx, int dy)` на `IDisplayPort` | display_port.h | **пікселі в шві** — ADR-002 §3.2 прямо забороняє геометрію у шві |
| **V2** | `begin_reinit/reinit_busy/reinit_tick(dt_ms)` на `IDisplayPort`; `DisplayModule::on_update` крутить tick | display_port.h, display_module.cpp | абстрактний модуль оркеструє **чіп-специфічну recovery-машину** — порушує «модуль знає лише intent» |
| **V3** | `display.reinit` политься в модулі (`prev_reinit_`) | display_module.cpp/.h, manifest.json | тригер апаратного відновлення в абстрактному модулі |
| **V4** | `apply_screen_params` — hand-written if-список залізних ключів (`cal_x/cal_y/...`) | display_module.cpp | контроль-роутинг зашитий у модуль; зростає без caps()-гейту |

### 2.2 Раніший борг (визнаний REVIEW, ADR-002 §8 відклав)
| # | Борг | Джерело |
|---|---|---|
| **D1** | `caps()` ніде не читається; меню не гейтиться capability; контур set_* замкнено ad-hoc, не через MenuEngine | REVIEW arch-1/2 |
| **D2** | `present_main` — bespoke 2-віконний layout в обхід `CharGridLayout` (на відміну від чистого `At7456ePort`) | порівняння з at7456e_port.cpp |
| **D3** | дубльоване `input_count`; `as_graphic()` не реалізовано | REVIEW arch-6 |

### 2.3 Що вже відповідає (НЕ чіпати)
- Чіп-драйвер `Amt630a` (`components/modesp_osd`) — чистий, переносний. ✅
- `set_backlight/contrast/brightness/saturation` — нормалізовані scalar-и шва. ✅
- `set_backdrop` — capability-гейтований (`has_backdrop`), за патерном scalar. ✅
- `select_input` через `IVideoInputs::as_video_inputs()` — ADR-capability. ✅
- `present_menu/present_edit` через `CharGridLayout::layout_*`. ✅
- Boot-flash fix (вікна OFF до готовності BGMAP) — коректна поведінка. ✅

---

## 3. Рішення

### 3.1 Калібровка → board.json (ПІДТВЕРДЖЕНО архітектором)
Overscan-зсув — це **per-panel hardware-конфіг**, як піни. Не runtime-параметр, не у шві.

- Додати у `I2CDisplayConfig` (hal_types.h:121): `int8_t cal_x = 0; int8_t cal_y = 0;`
- Парсити в `config_service.cpp` (поряд з `cols`/`rows`, ~рядок 602): ключі `"cal_x"`/`"cal_y"`.
- `board.json` `i2c_displays`: `{"id":"disp_0",...,"cols":20,"rows":10,"cal_x":-8,"cal_y":-8}`.
- Фабрика `amt630a_factory` передає `dcfg->cal_x/cal_y` у ctor `Amt630aPort` (поряд з cols/rows).
- `Amt630aPort` зберігає як члени; `win0_`/`win_` додають їх (вже є — лишаються).
- **Видалити:** `IDisplayPort::set_calibration`, `Amt630aPort::set_calibration`, `display.cal_x/cal_y` з manifest (state+ui+mqtt+menu), блок калібровки в `apply_screen_params`, `last_cal_x_/last_cal_y_` з модуля.
- **Наслідок:** зсув задається на плату (recompile), без live-слайдера; шов чистий від геометрії (V1 усунено).

### 3.2 Power-gate + recovery → GPIO в bindings + capability + generic heartbeat (ЗАТВЕРДЖЕНО)
Power-gate GPIO — **параметр драйвера в bindings.json** (механізм `Binding::settings`, як `beta`→3900):
- `bindings.json` дисплея: `{"hardware":"disp_0","driver":"amt630a","role":"display_main","module":"display","settings":{"power_gpio":7}}`.
- Фабрика `amt630a_factory` читає `b.setting_or("power_gpio", -1)` → передає у ctor `Amt630aPort`.
- `Amt630aPort`: якщо `power_gpio>=0` — конфігурує GPIO-output, керує живленням чіпа (rail on/off).
- **Прибрати зі шва** `begin_reinit/reinit_busy/reinit_tick` (V2 усунено).
- Додати **generic** `virtual void service(uint32_t dt_ms) {}` на `IDisplayPort` — `DisplayModule::on_update` кличе щотіку. Порт **внутрішньо** крутить chunked recovery-машину; модуль НЕ знає про reinit.
- Power-control через capability `as_power()` → `IDisplayPower { void set_rail(bool on); }`. `set_rail(true)` усередині порту: GPIO high → стартує chunked-recovery (await cold-boot → re-config). `set_rail(false)`: GPIO low (0 мА).
- **Прибрати `display.reinit`** з модуля/manifest (V3 усунено). Натомість маніфест-ключ `display.power` (on/off) роутиться модулем у `as_power()->set_rail()` — гейтовано новим `caps().has_power` (як інші capability-контроли, §3.3).
- Chunked-font-примітиви в чіп-драйвері (`begin_font_upload`/`upload_font_chunk`/`end_font_upload`) — лишаються (чисті).
- **Без power_gpio** (binding без settings): `caps().has_power=false`, recovery-машина пасивна, `service()` no-op — деградація чиста.

### 3.3 Замкнути caps()-контур чисто (D1) — ВКЛЮЧЕНО в цей рефактор (ЗАТВЕРДЖЕНО)
- `DisplayModule::on_init`: прочитати `port_->caps()` (вже є `caps_`), **передати у `MenuEngine`** → меню показує «Вибір входу»/«Насиченість»/«Фон»/«Живлення» **лише якщо** відповідний `caps`.
- Роутинг SharedState→port у `apply_screen_params` — строго caps()-гейтований; прибрати звідти калібровку (3.1); додати `display.power`→`as_power()` (3.2). Опційно — таблично-кероване мапування ключ→`set_*` замість if-списку.
- Додати `caps().has_power` (power-gate доступний).

### 3.4 present_main layout → (C) presentation-вибір драйвера (ЗАТВЕРДЖЕНО)
`present_main` лишається в `Amt630aPort` як багата презентація **впорядкованого** `MainView` (hero-значення W0×2 + дрібні рядки W1). Це **НЕ порушення**: ADR-002 §6 віддає презентацію драйверу, `CharGridLayout` — ОПЦІЙНА утиліта (SSD1306 не вживає), і present_main цілком у порті (не тече в модуль). «Головне значення» несе ПОРЯДОК `MainView.items` (items[0]=головне) — чистий семантичний вхід.
- **Перекласифікувати D2:** не «борг/порушення», а легітимний layout драйвера. Дія: лише косметика (документувати, прибрати магічні константи де є; брати cal з board.json через `win_`).
- (B) роль HERO у `CharGridLayout` — НЕ робимо (передчасне узагальнення; лише AMT630A має scale). Зробити, ЯКЩО другий бекенд захоче спільний hero.

---

## 4. План міграції (інкрементально, build-green на кожному кроці)

| Крок | Зміст | Усуває |
|---|---|---|
| **1** | Калібровка → board.json: `cal_x/cal_y` у `I2CDisplayConfig` + парсер `config_service.cpp` + ctor; прибрати `set_calibration` зі шва+порту, `display.cal_*` з manifest, блок із `apply_screen_params`, `last_cal_*` з модуля | V1 |
| **2** | Power-gate: `power_gpio` у bindings → фабрика → ctor; capability `as_power()/IDisplayPower{set_rail}`; generic `service(dt_ms)`; прибрати `reinit_*` зі шва, `display.reinit` з модуля | V2, V3 |
| **3** | caps()-контур: `caps()`→`MenuEngine` (гейт меню); `caps().has_power`; почистити `apply_screen_params`; роутинг `display.power`→`as_power` | V4, D1 |
| **4** | present_main: документувати як driver-presentation + косметика (без зміни поведінки) | D2 (reclass.) |

**Інваріант:** після кожного кроку — `cmake --build build` exit 0; host-тести зелені; bench-фічі (вибір входу, no-signal backdrop, boot-flash) не зламані; zero-heap збережено.

**Що НЕ ламати:** SharedState-контракт (`display.enabled/btn_*/screen/backlight/brightness/contrast/saturation/backdrop/input`); `init()`-fail ⇒ display disabled; згенерований `display_screens.h`; чіп-драйвер `Amt630a`.

---

## 5. Наслідки
- **Шов знову чистий:** `IDisplayPort` = present_* + caps + scalar set_* + as_*() + generic `service()`. Геометрії й lifecycle немає.
- **Калібровка** — board-конфіг (як піни), без забруднення UI/шва.
- **Recovery** — у правильному шарі (порт), тригер через capability, не через абстрактний модуль.
- **Мінус:** калібровка не тюниться наживо (recompile per-board) — свідомий розмін на чистоту (рішення архітектора).

---

## 6. Рішення (затверджено архітектором 2026-06-16)
1. **Power-gate/recovery:** GPIO — параметр драйвера в **bindings** (`settings.power_gpio`); capability `as_power()` + generic `service(dt)`; recovery лишається в порті. ✅
2. **caps()-контур (D1):** **включити зараз** (крок 3). ✅
3. **present_main:** **(C)** — presentation-вибір драйвера, не порушення; лише косметика/документування. ✅
4. **Калібровка:** ключі `cal_x`/`cal_y`, тип `int8_t` (±127px достатньо для overscan ≤48px) — за замовчуванням; скажи, якщо інші імена.
