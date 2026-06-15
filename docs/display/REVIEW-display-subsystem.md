# Ревью display-підсистеми ModESP Framework

> Адверсарне код-ревью підсистеми дисплея (menu-engine, char-grid, notification-queue,
> display-module, AT7456E/AMT630A порти та OSD-драйвери).
> Платформа: ESP32 / ESP-IDF v5.5, C++17 + ETL (без STL), zero-heap у hot-path,
> єдиний main-loop task, синхронна шина `etl::message_bus`.
> Дата: 2026-06-15.

---

## 0. Статус виправлень (2026-06-15)

**Виправлено й перевірено** (host **89/89** кейсів / **312** assertions; Python-генератор **356 passed**; device-build з AMT630A — **exit 0**):
- **major:** mem-1 (формат за типом-форматом, не за рантайм-варіантом), menu-1/mem-3 (клемп `cursor_` + build-гард `child_count<=15` у `generate_ui.py`), cov-1 (новий `test_display_module.cpp` з FakePort, 6 кейсів).
- **minor:** notif-1/2/3 (рефактор `tick()` — старіння ВСІХ finite + `seq` FIFO), editenum-1 (touched-guard), amt-1 (палітра B↔R/G за спекою), amt-2 (BGMAP addr lsb→msb carry), amt-6 (засів тіні FED8=0xA3), amt-8 (підсвітка після bring-up), mem-2 (Kconfig `range` + `render` від `g.cols/g.rows`), amt-7 (Kconfig help), amt-3 (font-контракт у header), amt-9 (коментар).
- **nit:** arch-3 (`BaseModule::on_message`), module-1 (один present_notice/тік), menu-2 (`refresh_acc_` reset).
- **тести +:** cov-2/3/4 (NotificationQueue), editenum-1 (MenuEngine).

**Відкладено за дизайном** (ADR-002 §8 — capability-фічі на етап 7/8): arch-1/2 (замкнути контур `caps()`/`set_*`), arch-5 (банер окремим вікном W1), arch-7 (`NoticeLevel` у core). Чисто косметичне (arch-6/9, kv-1) — опційно.

**Bench-pending** (валідувати при увімкненні AMT630A, §4): полярність PWM (amt-8), byte-order палітри (amt-1) і FONT-data, bit-значення CVBS (amt-6), i2c pull-up, RAM-шрифт для кирилиці (amt-7).

---

## 1. Резюме

**Загальна оцінка: підсистема ЗДОРОВА. Можна відвантажувати у дефолтній конфігурації.**

Жодного **critical**-дефекту не знайдено. Підтверджено **35** знахідок + **4** bench/uncertain.
Усі підтверджені баги — або вузькі крайові випадки за межами відвантаженого маніфесту/Kconfig,
або косметика, або прогалини тестів. Memory-safety, zero-heap і ETL-інваріанти **ніде не порушені**:
немає `new`/`malloc`/`std::*` у hot-path, усі буфери (`tiles[64]`, `etl::vector`, `etl::string`)
захищені від переповнення.

| Severity | К-сть | Примітка |
|----------|------|----------|
| critical | **0** | — |
| major    | **3** | усі латентні (manifest/UB-авторинг) або прогалина тестів glue-шару |
| minor    | ~17 | коректність TTL, спека-розбіжності AMT630A, незавершена capability-інтеграція |
| nit      | ~11 | документація, мікро-неефективність, читабельність |
| bench/uncertain | 4 | потребують валідації на реальному залізі |

**Найважливіше:** найбільший конкретний ризик — це **відсутність інтеграційних тестів `DisplayModule`**
(major, cov-1) — увесь glue-шар (on_message → черга → present_*, edge-detect кнопок, дзеркало банера
у SharedState) компілюється в test_runner, але не покритий жодним тестом. Два інших major —
**printf-format vs runtime-тип (UB)** і **overflow меню >16 пунктів** — латентні й не спрацьовують
у відвантаженій конфігурації, але є footgun-ами при майбутньому авторингу маніфестів.

**Контекст довіри:** увесь AMT630A-шлях (драйвер + порт) ще **не валідований на залізі** —
драйвер сам себе позначає bench-pending (полярність PWM, біти CVBS-входів, RGB-порядок, FONT bit-order).
AMT630A не є дефолтним backend (дефолт — `MODESP_DISPLAY_LOG`), тож його дефекти не впливають
на поточний відвантажений білд.

---

## 2. Critical + Major (детально, з фіксами)

Critical-знахідок немає. Нижче 3 major, згруповані по підсистемах.

### 2.1. `menu_engine.cpp` — printf format vs runtime StateValue type = UB при мисматчі

**[mem-1] major** — `modules/display/src/menu_engine.cpp:288-291, 318-321`

`format_value()` і `build_main()` обирають printf-гілку за **рантайм**-тегом `StateValue`, але
передають **задекларований у маніфесті** `n.format`/`mv.format`. Якщо вузол декларує `"%.1f"`, а
SharedState тримає ключ як `int32_t` (або навпаки `"%d"` з `float`) — виклик стає
`snprintf(buf, "%.1f", static_cast<int>(*i))` або `snprintf(buf, "%d", static_cast<double>(*f))`.
Передача `int` туди, де `%f` чекає `double` (і навпаки), — **undefined behavior** (varargs type
mismatch), не просто хибний вивід. На Xtensa це читає не той слот / FPU vs GP-регістр → сміття або краш.

Гірший варіант — `main_value` без явного `format`: `tools/generate_ui.py` дефолтить `"%s"`, що **не
виводиться з типу**; числовий main_value без format дав би `"%s"` + double/int → гарантований
pointer-from-non-pointer UB на idle-екрані. (Відвантажений `display_screens.h` задає `"%.1f°C"`
явно, тож шипнута конфіга безпечна, але дефолт — footgun.)

**Чому не спрацьовує сьогодні:** дефолтні формати в генераторі узгоджені з типом
(`DEFAULT_FORMATS={"float":"%.1f","int":"%d"}` беруться з того ж state-info, що й тип). Баг
досяжний лише коли автор **явно** перевизначає `format` несумісним специфікатором (генератор
ніде не валідує `format↔type`), або рантайм-публікатор пише інший варіант, ніж задекларовано.

**Фікс:** форматувати строго за **задекларованим** типом вузла, а не рантайм-варіантом:
для `EDIT_FLOAT`/VALUE-float → `%f`-родина з `static_cast<double>`, для `EDIT_INT`/VALUE-int →
`%d`-родина з `static_cast<int>`, попередньо коерсуючи `StateValue` до задекларованого типу.
Або санітизувати `n.format`/тип на етапі генерації (`generate_ui.py` валідує `format↔_item_type`).
`build_edit` (373/377) уже безпечний — `edit_f_` завжди float.

---

### 2.2. `menu_engine.cpp` / `char_grid.cpp` — меню з >16 пунктів: курсор десинхронізується з рендером

**[menu-1 / mem-3] major** *(дедуплікований; це одна знахідка з двох вимірів)* —
`modules/display/src/menu_engine.cpp:123-125, 345-362` + `char_grid.cpp:114-138`

`list_count()` повертає РЕАЛЬНУ к-сть (`child_count+1`) без обмеження `MAX_MENU_ITEMS(=16)`.
`nav_down()` дозволяє `cursor_` дійти до `list_count()-1` (напр. 20). Але `build_menu()` усікає
`v.items` рівно до 16 (`v.items.size() < MAX_MENU_ITEMS`), а рядок 362 ставить
`v.selected = cursor_` БЕЗ усічення. Драйвер не може показати selected: `layout_menu` рахує
`total = view.items.size() ≤ 16`, скрол клемпиться до `max_first = total - visible` → видиме
вікно ніколи не містить selected; роль SELECTED не призначається ні одному рядку → «мертвий»
курсор. `nav_select()` при цьому індексує `child_node(cursor_)` коректно (генератор кладе дітей
суміжно), тож **memory-safety НЕ порушено** — це деградація навігації/UX.

**Чому не спрацьовує сьогодні:** латентний/конфігураційний. Поточний `display_screens.h`
має `root_count=1`, max `child_count=4`; усі host-фікстури ≤4 пункти/рівень. Спрацьовує лише
за маніфесту з одним рівнем меню >16 пунктів (`child_count` — `uint8_t`, тож теоретично до 255).

**Фікс (єдине джерело істини):** додати у `generate_ui.py` build-time валідацію
`child_count/root_count <= MAX_MENU_ITEMS` (найдешевше, ловить на білді). Як мінімум — клемпити
`cursor_` у `nav_down()` до `min(list_count()-1, MAX_MENU_ITEMS-1)`, щоб курсор не виходив за
рендеровний набір.

> **Пов'язано (cov-6, minor):** окремий, вужчий ризик — при `child_count >= 16` back-пункт
> («Назад»/«Вихід») не потрапляє у відмальований `menu_view_.items` (обрізання спрацьовує до
> останньої ітерації). Але навігація керується `list_count()`/`cursor_`, а НЕ View-вектором:
> `nav_select()` все одно виконає back/to_main незалежно від вмісту View. Тобто **«лок меню»
> не виникає** — це той самий клас десинку View↔cursor, фіксується тим самим build-time гардом.

---

### 2.3. Тести — `DisplayModule` glue-шар повністю без покриття

**[cov-1] major** — `modules/display/src/display_module.cpp:132-185` + `tests/host/CMakeLists.txt`

`display_module.cpp` компілюється в test_runner (`CMakeLists.txt:62`), але **немає
`test_display_module.cpp`** у TEST_SOURCES. Уся glue-логіка непокрита:

- `on_message()` — фільтр UI_NOTICE → `notif_.push`;
- `on_update()` — edge-detect кнопок з самоскиданням momentary (`poll_button`);
- порядок present (`engine_dirty` → `present_current` → відновити банер поверх);
- дзеркало `display.banner`/`display.banner_level` у SharedState;
- gate `display.enabled == false`.

Регресія в порядку present / banner-mirror / edge-detect пройде непоміченою. Точка ін'єкції вже
готова (`set_port()`), host-SharedState працює (`base_module_host.cpp`).

**Severity скоригована: major, не critical** — це відсутність тесту, а не доведений дефект коду;
працюючий код не зламано.

**Фікс:** додати `tests/host/test_display_module.cpp` з `FakePort : IDisplayPort`, що лічить
виклики present_* / clear_notice і зберігає останній Notice. Кейси:
- (a) push UI_NOTICE через `on_message` → `on_update` → `present_notice` раз, `display.banner==text`, `display.banner_level==level`;
- (b) edge-detect: `state_set("display.btn_select",true)` → SELECT обробився ОДИН раз, кнопка самоскинулась у false;
- (c) банер поверх меню: активний банер + зміна екрана → `present_current` І `present_notice` обидва;
- (d) expiry → `clear_notice` + `display.banner==""`, `banner_level==0`;
- (e) `display.enabled==false` → ранній вихід, present не викликається.
Зареєструвати у `tests/host/CMakeLists.txt:66-74`.

---

## 3. Minor / nit

### 3.1. NotificationQueue (`notification_queue.cpp`)

- **[notif-1 / mem-4] minor** — *TTL неактивних банерів не відлічується.* `tick()` декрементує
  `remaining` ЛИШЕ для `active_index()`. Скінченний банер, прихований вищим (напр. WARN ttl=2000
  під infinite ALARM), застигає на повному TTL і «воскресає» після зняття вищого, попри те що
  реальний час минув. Те саме для двох finite-банерів РІВНОГО рівня: тільки перший старіє, другий
  отримує ефективний TTL `first.ttl + own.ttl`. Не memory-safety; розбіжність із семантикою
  ADR-001 «кожен банер живе власний ttl_ms незалежно від видимості».
  **Фікс:** у `tick()` пройтись по ВСІХ `!infinite` записах, декрементувати `remaining`, стерти
  всі прострочені (не лише active), потім `recompute()`. Цей же фікс природно усуває notif-2.

- **[notif-3] minor** — *Витіснення при overflow порушує FIFO для рівного рівня.* `push()` при
  повній черзі робить **in-place** заміну `q_[mi] = e`, новий запис успадковує ранній індекс
  звільненого low-рівня. `active_index()` обирає earliest за позицією → новий «омолоджується» й
  може виграти активність над реально старішим записом того ж рівня. Гарантія «новий ALARM ніколи
  не дропається» НЕ порушена — змінюється лише ЯКИЙ із рівних активний.
  **Фікс:** монотонний `seq` у Entry, вибір активного за `(level, seq)`; або `erase+push_back`
  замість in-place заміни.

- **[notif-2] nit (uncertain)** — *Залишок `dt_ms` при простроченні активного втрачається.* Перевитрата
  `dt_ms - remaining` не переноситься на наступний банер. Похибка ≤ один `dt_ms` на подію (десятки
  мс при TTL у сотні/тисячі мс) — непомітна. Узгоджується з моделлю «TTL = тривалість показу
  активного». Усувається фіксом notif-1 (єдиний `dt_ms` до всіх).

### 3.2. MenuEngine (`menu_engine.cpp`)

- **[editenum-1] minor** — *EDIT_ENUM з невідомим поточним значенням тихо мутує дані.* Якщо стан
  містить `value` поза списком опцій (напр. зовнішній модуль виставив `t.mode=99`), цикл пошуку
  нічого не знаходить, `edit_opt_` лишається 0 → редактор показує ПЕРШУ опцію як «поточну». SELECT
  без жодного adjust запише `options[0].value`, мовчки змінивши 99 → options[0]. Простий перегляд
  мутує стан. Тригер вузький (інвалідний value від зовнішнього модуля), стан відновлюваний, без UB.
  **Фікс:** трекати `touched`-прапорець і не писати при SELECT без фактичного `edit_adjust`; або
  sentinel «невідомо», що вимагає явного вибору.

- **[menu-2] nit** — *`refresh_acc_` не скидається у `handle_event`.* Після негайного `rebuild()`
  по події можливий «подвійний» rebuild за лічені мс. Зайвий rebuild дешевий (dirty виставиться
  лише при реальній зміні View через `operator==`). **Фікс (опц.):** `refresh_acc_ = 0` у кінці
  `handle_event`.

### 3.3. CharGrid (`char_grid.cpp`)

- **[kv-1] nit** — *`build_kv`: при `valW == cols-1` label зникає повністю, перехід від «1 гліф
  label» (valW=cols-2) різкий.* `append_clamped` обрізає без маркера, тож label може стиснутись до
  1 символу («Setpoint» → «S») без індикації. Вивід завжди валідний UTF-8, ≤ cols гліфів, без
  переповнення — це навмисна graceful degradation (пріоритет value над label). **Фікс (енхансмент):**
  поріг показу label ≥ K гліфів, інакше лише value; або маркер обрізання; або задокументувати.

### 3.4. DisplayModule (`display_module.cpp`)

- **[module-1] nit** — *Подвійний `present_notice` при одночасному `engine_dirty` + `notif_dirty`
  + активному банері.* Той самий банер штовхається в порт двічі за тік (рядки 166 і 171) — зайва
  SPI/I2C-транзакція нижнього рядка. Логіка коректна (другий запис ідемпотентний). Для дефолтного
  LogPort наслідок нульовий. **Фікс:** обчислити «показати банер» один раз; дзеркалити state лише
  при `notif_dirty`.

### 3.5. AMT630A драйвер (`components/modesp_osd/src/amt630a.cpp`)

- **[amt-1] minor** — *`set_palette()` пише RGB444 у переставленому порядку (B↔R/G свопнуто).*
  Спека §2.3/§3.5: парний регістр (0x56) = MSB = Blue, непарний (0x57) = LSB = `(G<<4)|R`. Код
  робить НАВПАКИ (рядки 153-154). Наслідок: `set_palette(1,10,0,0)` (alarm-червоний) виходить
  синюватим. Косметика, на bench-pending шляху палітри. **Фікс:** поміняти місцями два `amt_w`:
  `amt_w(OSD, base, b & 0x0F)` (MSB=Blue), `amt_w(OSD, base+1, (r&0x0F)|((g&0x0F)<<4))`.

- **[amt-2] minor** — *`osd_print()` не обробляє перенос BGMAP addr lsb→msb (HW НЕ автоматичний).*
  msb (FB0D) виставляється один раз перед циклом; якщо `(bgmap_addr & 0xFF) + n` перетинає 0x100,
  lsb обертається, msb лишається старим → хвостові тайли тихо затирають нижню половину BGMAP. Не
  спрацьовує за дефолту (20×10 → max addr 199<256), але osd_print — публічний driver-примітив без
  задокументованого інваріанта. **Фікс:** відстежувати msb інкрементально при обертанні lsb; або
  ассертити `(bgmap_addr & 0xFF) + n <= 256` і задокументувати.

- **[amt-3] minor** — *`upload_font`: `font_addr_msb` маскується `&0x0F` → тихе обрізання, якщо
  `first_tile` передано як BGMAP-код (0x1C0+).* Контракт «first_tile = RAM word-tile 0-based» не
  зафіксований у сигнатурі/header. `upload_font` AMT630A ще НЕ викликається ніде в репо (латентний
  contract-gap). **Фікс:** doxygen-контракт `first_tile = RAM word-tile (0-based)` + ассерт
  `ysiz*(first_tile+count) <= 0xFFF`.

- **[amt-4] minor** — *`is_danger()` не покриває весь §10:* бракує FDF1, FDB0/B2/B4/B5 (ADC config),
  опційно VIDEO 0xD5 bit7. (Контр-перевірка: FD17 **вже** блокується — підпункт знахідки про FD17
  хибний.) Жоден реальний runtime-шлях не пише у непокриті регістри, тож живого бага немає —
  послаблена сітка безпеки. **Фікс:** додати `if (reg==0xF1) return true;` і
  `if (reg==0xB0||reg==0xB2||reg==0xB4||reg==0xB5) return true;` у GLOBAL-гілку.

- **[amt-6] minor** — *`select_input()`: тіньова копія `sh_fed8_` стартує з 0, не синхронізована з
  заліза.* Init-таблиця пише FED8=0xA3 (kOn `{0x59,0xD8,0xA3}`), а тінь = 0; перший `select_input`
  затирає молодші config-біти 0x23 нулями. Біти вибору входу (6,7) коректні щоразу. **Фікс:**
  засіяти `sh_fed8_` значенням з init-таблиці (0xA3) або прочитати `amt_r(AV,0xD8)` у тінь.

- **[amt-9] nit** — *Коментар header (`amt630a.h:7`) «3 байти `[reg, val]`» суперечить сам собі:*
  payload = 2 байти, 3 байти на шині (з адресою банку). Код коректний. **Фікс:** уточнити коментар.

### 3.6. AMT630A порт (`modules/display/src/amt630a_port.cpp`)

- **[mem-2] minor** — *Kconfig COLS/ROWS без `range` → розбіжність із клампленою CharGrid.* `cols_`/
  `rows_` беруться напряму з Kconfig (`int` без range). CharGrid клемпить до `MAX_GRID_COLS=40` /
  `MAX_GRID_ROWS=24`, а `render()` ітерує сирим `rows_` і пише `min(cols_,64)` тайлів → для COLS
  41..64 праве поле порожнє, для COLS>64 стовпці втрачаються. Плюс адреса `r*cols` маскується до
  9 біт (512) у драйвері → при `rows_*cols > 512` пізні рядки завертаються. Memory-safe (tiles[64]
  захищено). За дефолту (20×10) усе коректно. **Фікс:** `range 1 40`/`range 1 24` у Kconfig +
  керувати циклом `render()` від `g.cols`/`g.rows`, не від сирого `cols_`/`rows_`. *(Перетин з
  amt-1/uncertain — той самий мисматч джерела ширини.)*

- **[amt-7] minor** — *Кириличний текст рендериться як суцільні пробіли.* `rom_tile()` мапить лише
  ASCII цифри/латиницю; кирилиця і будь-що невідоме → 0x00, який ТАКОЖ використовується як
  пробіл/очистка. Тобто кириличне меню ModESP (заради якого чіп і обрано) виходить функціонально
  порожнім. Задокументований TODO (RAM-font ще не зроблено), але `caps().has_color=true` і Kconfig
  рекламує бекенд без застереження. Не дефолтний backend. Вторинно — fold a-z→A-Z втрачає регістр.
  **Фікс:** до RAM-шрифту — уточнити Kconfig help «лише латиниця/цифри»; або видавати видимий
  placeholder («?») для unknown-glyph, лишивши 0x00 лише для справжнього пробілу.

- **[amt-8] minor** — *Підсвітка лишається OFF після bring-up.* init-таблиця ставить PWM0 duty=0
  (`{0x58,0x28,0x00},{0x58,0x29,0x00}`), а ні `apply_init_table()`, ні `Amt630aPort::init()` не
  викликають фінальний `set_backlight(preset)` (крок `changeBrightness` з FIZIK §7.2 пропущено).
  За прямої полярності екран лишається темним. Полярність bench-pending. **Фікс:** додати
  `dev_.set_backlight(<дефолт ~70>)` у кінець `Amt630aPort::init()`.

### 3.7. Архітектура (capability-шар, шви, контракти)

- **[arch-1 / arch-2] minor** — *Уся capability-поверхня — мертвий код зі сторони модуля.*
  `caps()` ніде не читається; `set_backlight/contrast/brightness/saturation`,
  `as_video_inputs()->select_input()` реалізовані в `Amt630aPort`, але жоден не викликається з
  `DisplayModule`/`MenuEngine`. `MenuEngine` не отримує `DisplayCaps` (немає поля в `MenuData`).
  `edit_save` пише ТІЛЬКИ в `io_.set(SharedState)` — ніхто не читає ключі назад, щоб викликати
  `port_->set_*()`. Інваріант ADR-002 §3.2(5)/§7 «caps() керує складом меню» не під'єднаний.
  Без runtime-наслідків (capability-залежних пунктів меню у відвантаженому дереві взагалі немає) —
  незавершена інтеграція feature-in-progress, ADR §8 явно відкладає на «етап 7/8 AMT630A».
  **Фікс:** замкнути контур (прочитати `port_->caps()` в `on_init`, передати в `MenuEngine`;
  після `edit_save` мапити SharedState-ключ → `port_->set_*()`); або прибрати невживані `set_*`/
  `IGraphicRenderer`/`IVideoInputs` зі шва з явним TODO, поки немає консумента.

- **[arch-5] minor** — *Банер затирає рядок контенту замість оверлею; вигляд нижнього рядка
  залежить від dirty-гілки.* Обидва порти пишуть банер у НИЖНІЙ рядок тієї ж BGMAP/char-RAM
  (не окреме вікно). `present_current()` перемальовує весь кадр лише під `engine_dirty`; гілка
  `notif_dirty` при знятті банера кличе `clear_notice()` (забиває рядок пробілами) і НЕ відновлює
  контент меню → після спливу TTL нижній рядок меню лишається порожнім до наступної зміни екрана.
  ADR-002 §5 ЯВНО санкціонує «на символьних банер витісняє рядок» — це задокументоване спрощення,
  не приховане відхилення. «Race» — неточність (шина синхронна, єдиний task); це ордер-залежний
  візуальний артефакт. **Фікс:** резервувати рядок банера в `CharGridLayout` коли notice активний;
  або реальне вікно W1 поверх W0 на AMT630A; прибрати залежність нижнього рядка від dirty-гілки.

- **[arch-6] minor** — *Дубльоване магічне `input_count=2` (caps + `input_count()`); `as_graphic()`
  не перевизначено попри `has_color=true` і спеку.* **Фікс:** винести у const-член; реалізувати
  `as_graphic()` або прибрати `IGraphicRenderer` зі шва.

- **[arch-7] minor** — *`NoticeLevel` задвоєно:* на шині `MsgUiNotice::level` — сирий `uint8_t`,
  семантичний enum живе лише в `modesp::display`; ADR-001/002 передбачали ОДИН enum на шині.
  `to_level()` клемпить будь-яке >=2 у ALARM (без втрати/UB). Борг типобезпеки. **Фікс:** винести
  `modesp::NoticeLevel` у core і типізувати `MsgUiNotice::level`; або задокументувати свідоме
  рішення тримати `uint8_t` на шині.

- **[arch-3] nit** — *`on_message` не викликає `BaseModule::on_message(msg)`* всупереч ADR-001.
  Зараз нешкідливо (база — no-op; маршрутизація per-adapter, не catch-all, тож нічого не
  «з'їдається»). Латентний ризик лише якщо база отримає спільну логіку. **Фікс:** додати виклик
  базового в кінці override.

- **[arch-9] nit** — *`At7456ePort` передає `cols()-1` у layout (резервування колонки маркера
  протікає у present_* як магічне `-1` тричі).* Геометрія в модуль НЕ протікає (все в порту).
  **Фікс:** `private const kMarkerCols=1` + `content_cols()`.

- **[arch-4] none** — *Підтвердження:* `on_message` робить ЛИШЕ `notif_.push()` (zero-heap,
  in-memory, без I²C/рендеру/реентрабельного publish) — синхронний контракт ADR-001 дотримано точно.

- **[arch-8] nit** — *Підтвердження:* Kconfig choice + `default_port()` каскад консистентні,
  взаємовиключність backend-макросів гарантована choice, LogPort коректно семантичний (геометрії
  не вигадує). Опційно — `static_assert`/коментар про взаємовиключність.

---

## 4. Bench / Uncertain — що валідувати на залізі

Усе нижче — на AMT630A-шляху, який ще не запускався на платі KOZHAN. Перед увімкненням
AMT630A backend у продакшні **обов'язково пройти bench-чекліст** (`AMT630A_driver_design.md §9, §12`).

| # | Що валідувати | Як перевірити на bench |
|---|---------------|------------------------|
| **amt-1 (palette)** | RGB444 byte-order у `set_palette()` (B↔R/G свопнуто за спекою) | Записати один відомий колір (червоний), зчитати/подивитись на TFT; підтвердити, який регістр = MSB=Blue |
| **amt-8 (backlight)** | Полярність PWM0 duty: чи `duty=0` = темно (пряма) чи = яскраво (інверсна) | Виставити duty 0 і 100, спостерігати яскравість; від цього залежить, чи потрібен `set_backlight` у init |
| **amt-6 (FED8/FEDC)** | bit-значення вибору входу (bit6,7 = av3?0:2; FEDC 0x20/0x00) + початковий вміст FED8 від OEM | Перемкнути AV1↔AV3, перевірити сигнал; прочитати `amt_r(AV,0xD8)` після OEM-init для засіву тіні |
| **amt-10 (i2c pullup)** | `enable_internal_pullup=true` на 100кГц спільній шині (AMT630A + LCD) | Перевірити стабільність probe/ACK на 0x5B; виміряти rise-time SDA/SCL; з'ясувати, чи є зовнішні pull-up на платі |
| **(uncertain) amt-1 render** | `render()` width-mismatch (layout клемпить до 40, render до 64 на сирому `cols_`) | Лише при COLS 41..64 (нереалістична OSD-геометрія); фіксується разом із mem-2 |

**Додатково перед bench (не uncertain, але теж потребують перевірки на залізі при увімкненні AMT630A):**
amt-2 (BGMAP wrap при rows*cols>512), amt-3 (FONT addr при custom-font), amt-7 (кирилиця потребує
RAM-font pipeline), apply_init_table vs OEM-firmware взаємодія.

---

## 5. Прогалини тестів + конкретні нові кейси

Host-тести (`tests/host/`) покривають `MenuEngine`, `NotificationQueue`, `CharGridLayout`
ізольовано, але мають реальні діри. Усі — прогалини покриття, **не runtime-баги** (крім glue-шару).

### 5.1. `test_display_module.cpp` — СТВОРИТИ (major, cov-1)
Новий файл з `FakePort`. Кейси (a)-(e) — див. §2.3.

### 5.2. `test_notification_queue.cpp` — доповнити
- **[cov-2] minor** — витіснення АКТИВНОГО запису та рівний-найнижчому дроп:
  - (a) `[2,0,...,0]`, consume_dirty; push lvl-1 → витісняється low, active лишається ALARM, `consume_dirty()==false`;
  - (b) CAP×lvl-0 з різними текстами 't0'..'t7', active=='t0'; push lvl-1 'hi' → active=='hi', `consume_dirty()==true`;
  - (c) черга повна lvl-1; push lvl-1 'x' → дроп (рядок 28 `not >`), has_active незмінний, `consume_dirty()==false`.
- **[cov-3] minor** — FIFO-при-рівних ('earliest wins', захист `>` від мутації у `>=`):
  push(1,0,'first'); consume_dirty; push(1,0,'second') → `active().text=='first'`, `CHECK_FALSE consume_dirty()`; tick до expiry 'first' → `active=='second'`.
- **[cov-4] minor** — TTL рівно на межі + старіння лише активного:
  - (a) push(1,100,'w'); tick(100) → `has_active()==false`, `consume_dirty()==true` (точна межа експайрить);
  - (b) push(0,50,'low'); push(2,0,'alarm'); consume_dirty; tick(100) → active лишається alarm, 'low' ВСЕ ЩЕ є (фіксує «старіє лише активний»; після фіксу notif-1 цей кейс зміниться).

### 5.3. `test_display_menu.cpp` — доповнити
- **[cov-5] minor** — глибокий стек підменю до `MAX_DEPTH`: фікстура з ланцюгом SUBMENU глибини ≥5.
  Послідовні SELECT занурюють до `stack_.size()==4`; ще один SELECT на SUBMENU — screen лишається
  MENU, `stack_.size()` НЕ зростає; послідовні back з відновленням cursor кожного рівня;
  `screen_name == 'menu:<label>'`.
- **[cov-6] minor** — overflow `MAX_MENU_ITEMS` + порожнє підменю:
  - рівень ≥17 пунктів → View містить back-пункт АБО build-time гард (фіксується разом із §2.2);
  - порожній SUBMENU (`child_count==0`): SELECT входить → `items.size()==1`, `items[0].is_back`, label=='Назад', selected==0; SELECT знову → повернення;
  - `nav_down` на single-back рівні не виходить за межі (cursor==0).

### 5.4. `test_char_grid.cpp` — доповнити
- **[cov-7] minor** — `rows==1` / вузька сітка / value-overflow:
  - (a) `layout_menu(3 пункти, cols=20, rows=1)` → `lines.size()==1`, `lines[0].role==TITLE`;
  - (b) `build_kv` через `layout_main`: label='VeryLongLabel', value='123456789012345678', cols=10 → у рядку лише значення, label відсутній, `glyph_len<=cols`;
  - (c) value з кирилицею що перевищує cols → `glyph_len(line)==cols`, рядок валідний UTF-8 (не розрізаний).
- **[cov-8] nit** — підсилити scroll-тест (потенційно тавтологічний `!= 'i0'`):
  `selected=9, visible=3` → `lines[1]=='i7'`, `lines[3]=='i9'` з SELECTED (точне вікно [7,8,9]);
  додати mid-кейс `selected=5, visible=3` → вікно [3,4,5].

---

## 6. Рекомендований порядок виправлень

**Етап 1 — захист від регресій (зробити першим, дешево, високий ROI):**
1. **cov-1** — створити `test_display_module.cpp` з FakePort (закриває весь glue-шар).
2. **menu-1/mem-3** — build-time гард `child_count/root_count <= MAX_MENU_ITEMS` у `generate_ui.py`
   (закриває major-overflow + cov-6 одним рядком валідації) + клемп `cursor_` у `nav_down`.
3. **mem-1** — форматувати за задекларованим типом вузла (усуває UB-footgun); додати валідацію
   `format↔type` у генераторі.

**Етап 2 — коректність TTL та FIFO (один цілісний рефактор черги):**
4. **notif-1 + notif-2 + notif-3** — переписати `tick()` (декремент усіх finite, erase усіх
   прострочених, потім recompute) + `seq`-поле для FIFO. Додати cov-2/3/4 у той же PR.
5. **editenum-1** — `touched`-прапорець у EDIT, не писати без фактичної зміни.

**Етап 3 — AMT630A bring-up (перед першим bench-запуском, разом):**
6. **mem-2** — `range` у Kconfig + `render()` від `g.cols`/`g.rows`.
7. **amt-1, amt-2, amt-3, amt-4, amt-6, amt-8** — виправити перед bench (палітра, BGMAP-wrap,
   FONT-контракт, is_danger, тінь FED8, backlight). Пройти bench-чекліст §4.
8. **amt-7** — RAM-font pipeline для кирилиці (або тимчасово уточнити Kconfig help).

**Етап 4 — архітектурний борг (коли capability-фічі стануть на роадмап / етап 7-8):**
9. **arch-1/arch-2** — замкнути capability-контур АБО прибрати мертвий шов із TODO.
10. **arch-5** — резервування рядка банера в layout (або вікно W1).
11. **arch-7** — уніфікувати `NoticeLevel` у core.

**Етап 5 — косметика / nit (опортуністично):**
12. arch-3, arch-6, arch-9, amt-9, kv-1, menu-2, module-1 — дрібні читабельність/документація.

---

*Дедуплікація: menu-1≡mem-3 (об'єднано у §2.2); notif-1⊃mem-4 та notif-2 (один фікс tick);
arch-1⊃arch-2 (один capability-шов); mem-2 перетинається з amt-1/uncertain (width-mismatch).
Спростовані під-твердження: amt-4 (FD17 уже блокується). Refuted-знахідки (2) до звіту не включені.*
