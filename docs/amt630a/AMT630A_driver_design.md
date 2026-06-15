# AMT630A — повна специфікація драйвера дисплея для ModESP (керування по I²C)

> **Статус:** інтегрований проєктний документ. Зводить канонічну user-спеку
> (`AMT630A_user_spec.md`) і 8 дослідницьких звітів (`research/01..08`) у єдину
> специфікацію драйвера ModESP. Усі факти перехресно звірені з чотирма джерелами;
> конфлікти між ними винесені окремо (розділ 12) і позначені ⚠ по тексту.
>
> **Джерела (за рівнем довіри):**
> - **SPEC** — `docs/amt630a/AMT630A_user_spec.md` (канонічна user-спека: Rosetta-карта, формати, DANGER).
> - **ENGELS** — `ENGELS.A22` (nocash/Martin Korth дизасемблер OEM-прошивки 4.3″/480×272). Найбагатше джерело: абсолютні адреси, імена `IO_*`, бітові коментарі.
> - **CLEAN** — `AMT630A.A22` (nocash дизасемблер прошивки 3.5″/320×240). Лабораторні експерименти Korth.
> - **FIZIK** — `AMT630.h` (доведений робочий I²C-драйвер на ESP32). **Єдине джерело, ПЕРЕВІРЕНЕ на залізі.**
>
> **Цільове залізо:** плата KOZHAN ZCD-630A-4.3D+SPI — AMT630A video-SoC + LCD 4.3″ + зовнішній SPI-flash з OEM-прошивкою. ESP32-WROOM-32 (ModESP) керує по I²C (GPIO21 SDA / GPIO22 SCL @ 100 кГц).
>
> Дата: 2026-06-14.
>
> **Верифікація (2026-06-14):** документ пройшов змагальну перевірку 3 незалежними скептичними рецензентами проти першоджерел (ENGELS/CLEAN/FIZIK + репо ModESP). Підтверджено **56/61** тверджень, **0 вигаданих адрес регістрів**; центральні тези (Шлях A, банк `0x58`=FDxx, init Fizik, формати) доведені. Виправлення внесено: §4.1 (атрибуція 44 Б), §7.1 (лічба 24→25), §6.1/§10 (дворівневий guard whitelist↔init), §6.3 (сповіщення — переглянуто в **ADR-001**: подія `MsgSystemNotice` на наявний `etl::message_bus`, НЕ polling), §8 (Kconfig bool→choice).

---

## Зміст

1. [Архітектурне рішення — Шлях A vs B](#1-архітектурне-рішення)
2. [Повна довідка регістрів по банках](#2-повна-довідка-регістрів)
3. [Модель OSD: BGMAP, 5 вікон, шрифт, палітра, масштаб](#3-модель-osd)
4. [Конвеєр кириличного шрифту (TTF → AMT630A)](#4-конвеєр-кириличного-шрифту)
5. [Мапінг 3 спроможностей на регістри](#5-мапінг-спроможностей-на-регістри)
6. [C++ API: `Amt630a` driver + `AMT630ARenderer` + розширення інтерфейсу](#6-c-api)
7. [Доведена init-послідовність (Fizik)](#7-доведена-init-послідовність)
8. [Поетапний план реалізації](#8-поетапний-план-реалізації)
9. [Чеклист бенч-перевірки + відкриті апаратні питання](#9-чеклист-бенч-перевірки)
10. [Безпека: DANGER-регістри і whitelist](#10-безпека-danger-регістри)
11. [Енергетика: PWM-підсвітка](#11-енергетика-pwm-підсвітка)
12. [Нотатки про конфлікти джерел і як вирішені](#12-конфлікти-джерел)

---

## 1. Архітектурне рішення

### 1.1 Що таке AMT630A

AMT630A — це **8051 (80C31/80C52) @ ~6.75 МГц з прошивкою на зовнішньому SPI-flash**
(256–512 КБ, код <48 КБ) плюс апаратний video-decoder + scaler + TCON + **OSD-рушій**.
SPI-маркована плата KOZHAN = AMT630A + цей flash. Чіп **автономний**: сам вантажить прошивку,
ініціалізує декодер/PLL/LCD-тайминг і виводить відео на LCD 4.3″ ще до будь-якого I²C.

### 1.2 Два шляхи керування

| Шлях | Що дає | Складність | Ризик |
|------|--------|-----------|-------|
| **A. I²C ззовні (ОБРАНО)** | Зміна OSD, вибору входу, яскравості, кольору поверх працюючої прошивки | Низька | Безпечно |
| B. Дамп + реплейс прошивки | Повний контроль (vsync-IRQ, FLASH→FONT DMA, режим піна) | Висока (реверс 8051) | DANGER-біти, цеглування |

### 1.3 Чому Шлях A, не B — обґрунтування

1. **Перевірено на залізі.** FIZIK керує тим самим чіпом (тієї ж родини плат) **виключно** по I²C
   поверх OEM-прошивки — вмикає відео, регулює яскравість, виводить OSD-текст. Доведений факт, не теорія.
2. **Не потрібен реверс 8051.** Шлях B вимагає дампу прошивки через DANGER-регістри SPI-flash
   (`FDD0h`/`FDD6h+`), реверс-інженерії та переписування 8051-коду — місяці роботи з ризиком цеглування.
3. **Усе практичне доступне по I²C.** AMT630A має **два адресні простори**:
   - **XRAM I/O-порти `FBxx–FFxx`** — доступні і внутрішньому MCU (через `MOVX`), **і ззовні через I²C slave**.
     Сюди входить **усе потрібне**: OSD-вікна, текст, шрифт, вибір CVBS, яскравість, підсвітка PWM, палітра.
   - **CPU SFR `80h–FFh`** — переважно тільки внутрішній MCU. ⚠ Але FIZIK доводить, що частина SFR
     (`C6h`, `BEh`, watchdog) **досяжна через окремий vendor-канал `0x5F`** у конкретній unlock-послідовності
     (розділ 12, конфлікт #3). Для Шляху A це не критично — vsync/DMA не потрібні (нижче).
4. **Мерехтіння OSD не відтворюється.** Проблема мерехтіння була на окремому чіпі **AT7456E**, де ESP32
   писав у display-memory під час активного відео. AMT630A — **інтегрований** video+OSD SoC: апаратний
   OSD-рушій безперервно читає BGMAP/FONT і композитить поверх відео з власним таймингом. ESP32 лише
   **оновлює дані** — рушій підхоплює сам. Текстові оновлення BGMAP **атомарні й безпечні**. Єдина
   glitch-небезпечна операція — запис FONT RAM під час рендера, і вона вирішується вимкненням вікон
   на час заливки (розділ 4.4), без потреби у vsync-синхронізації (`SFR 91h` — CPU-only, недоступний по I²C).

### 1.4 Наслідки для проєкту KOZHAN

- **AMT630A повністю заміняє AT7456E** і дає більше: 5 вікон, завантажуваний шрифт 16×22 (**кирилиця**),
  графіка, напівпрозорість, 3 CVBS-входи, per-window масштаб ×1..×4. Прибрати AT7456E → вільний `CS_OSD=IO5`,
  немає AT7456E-мерехтіння, україномовне меню на TFT.
- **Детекцію слабких сигналів лишити на ESP32** (ADC1, сирий CVBS). AMT630A має апаратний sync-lock —
  годиться для **відображення**, не для детекції слабких дронів.

---

## 2. Повна довідка регістрів

### 2.1 Rosetta-карта банків — MCU XRAM ↔ I²C-адреса (підтверджено дизасемблером + FIZIK)

Кожен MCU XRAM-банк `Fxxh` = окрема I²C-адреса; **низький байт MCU-адреси = номер регістра в I²C-транзакції**.

| MCU-банк | I²C 7-біт | I²C 8-біт (W) | Підсистема | Перевірено FIZIK |
|----------|-----------|---------------|------------|------------------|
| `FBxx` | **`0x5B`** | `0xB6` | **OSD** (вікна, BGMAP, FONT, палітра) | ✅ |
| `FCxx` | `0x5C` | `0xB8` | LCD-color-swap + Video/Tcon-тайминг (50/60 Гц) | ⚠ не чіпає (працює поверх OEM) |
| `FDxx` | **`0x58`** | `0xB0` | Global: **PWM (підсвітка)**, ADC, SPI-flash, PLL, PIN-mode | ✅ |
| `FExx` | **`0x59`** | `0xB2` | **AV-декодер / вибір CVBS** + детект сигналу | ✅ |
| `FFxx` | **`0x5A`** | `0xB4` | Video-process: gamma, **яскравість/контраст/насиченість**, backdrop, snow | ✅ |
| (SFR/init) | **`0x5F`** | `0xBE` | **vendor init/unlock** sub-протокол → CPU SFR (watchdog, memory_system) | ✅ |

> ⚠ **Розв'язано конфлікт іменування `0x58`/`0x5C`:** FIZIK називає `0x58` як `TFT_LCD_REG` — **назва оманлива**.
> Фізично `0x58` = банк **FDxx (Global/PWM/PLL)**, НЕ LCD-тайминг. Доказ: усі записи FIZIK у `0x58`
> (рег. `0x28/0x29/0x42/0x11/0x12/0x13/0x1F`) збігаються з ENGELS-іменами `FD28/29` (PWM0 backlight duty),
> `FD42` (PIN PWM), `FD11/12/13` (PLL/screen-on-off). Справжній LCD/Tcon-тайминг = банк FCxx = `0x5C`.

### 2.2 Формат I²C-транзакції (доведено FIZIK на залізі)

```c
// Запис одного XRAM-регістра Fxxnn = value:
//   dev7 = I²C-7-біт банку (0x58..0x5C, 0x5F)
//   reg  = низький байт nn
//   приклад: FB05h = 0x1F → amt_w(0x5B, 0x05, 0x1F)  (увімкнути всі 5 OSD-вікон)
void amt_w(uint8_t dev7, uint8_t reg, uint8_t val);   // START, dev<<1|W, reg, val, STOP
uint8_t amt_r(uint8_t dev7, uint8_t reg);             // write reg-ptr, repeated-START, read 1 байт
```
> FIZIK ставить `delay(10)` (10 мс!) після кожного запису — консервативно. Для ESP-IDF `i2c_master` з
> апаратним ACK/таймаутом це **зайве**: покладатися на ACK. Шина 100 кГц доведена; вище — не перевірено.

### 2.3 БАНК `FBxx` — OSD (I²C `0x5B`) ✅

| reg | Ім'я (ENGELS) | Призначення | Біти / нюанси |
|-----|---------------|-------------|---------------|
| `0x00` | `bgmap_addr_lsb` | BGMAP addr lsb | **авто-інкремент** при записі `0x01` |
| `0x01` | `bgmap_data_lsb` | № символу lsb | **запис → у VRAM** (латчить і msb), addr++ |
| `0x02` | `font_addr_lsb` | FONT addr lsb (ручний аплоад) | 12-біт адреса **слова** |
| `0x03` | `font_data_lsb` | FONT data lsb | ⚠ запис латчить ОБИДВА (пишеться ОСТАННІМ — див. 4.3) |
| `0x04` | `font_data_msb` | FONT data msb | пишеться ПЕРШИМ |
| `0x05` | `window_enable_bits` | bit0–4=вікна 0–4 ON; **bit6=TEXT hide**; bit7=BITMAP/4bpp шар | FIZIK: `0x1F`=всі, `0x01`=W0, `0x00`=off |
| `0x06` | `misc_transp_enable` | bit6=semi-transp фон, bit7=semi-transp текст | |
| `0x07`–`0x0B` | window 0: size_x/size_y/xyloc_msb/xloc_lsb/yloc_lsb | геометрія W0 (без vramaddr — фікс 000h) | |
| `0x0C` | `bright_transp_level` | bit5–7=яскравість OSD(0–7), bit0–2=рівень прозорості(0–7) | дефолт `0x80` |
| `0x0D` | `bgmap_addr_msb` | BGMAP addr msb | bit1–7 NOT R/W; **ручний** інкремент при переносі lsb |
| `0x0E` | `bgmap_data_msb` | № символу msb (біти 8–9) | bit2–7 NOT R/W |
| `0x0F` | `font_addr_msb` | FONT addr msb | bit4–7 NOT R/W |
| `0x10` | `bgmap_data_attr` | **bit0–2=FG, bit4–6=BG** (0=прозор,1–6=палітра,7=чорний) | bit7 NOT R/W |
| `0x11`/`0x70` | `bitmap_start_lsb/msb` | № першого 4bpp-bitmap-тайла (нижче=TEXT/1bpp) | OEM: `0x28` |
| `0x12`–`0x29` | window 1–4: size/loc/scale/vramaddr | блоки по 6 байт (див. 3.4) | |
| `0x2B`–`0x31` | window 0 vscale/hscale | по-рядковий/по-піксельний scale | |
| `0x32` | `window_0_scale` | bit0–1=ScaleX, bit2–3=ScaleY (×1..×4) | bit4–7 NOT R/W |
| `0x33`/`0x34` | `window_1&2 / 3&4 scale` | low nib=перше вікно, high nib=друге; 2×2-біт | ⚠ OEM-баг `and 0F0h` (12.6) |
| `0x35` | `bitmap_transp_misc` | bit4=4bpp TRANSP color0 | OEM fixed 00h |
| `0x36`–`0x55` | `bitmap_colors` | 16-кольорова палітра 4bpp (32 байти) | OEM не вживає |
| `0x56`–`0x61` | `color_1..6` | палітра 6 кольорів RGB444 (msb=B, lsb=(G<<4)\|R) | **програмована** (12.1) |
| `0x76` | `char_xsiz` | ширина символу px (bit0–4, 1..24) | OEM: 12; FIZIK: 16 |
| `0x77` | `char_ysiz` | висота символу px (bit0–5, 1..32) | OEM: 16; FIZIK: 22 |
| `0x78` | `xyflip` | bit4 TileXflip, bit5 MapXflip, bit6 TileYflip, bit7 MapYflip | |
| `0x89` | `screen_position` | глобальний зсув усіх вікон (⚠ bit1=jitter) | bit4–7 NOT R/W |

### 2.4 БАНК `FExx` — AV / вибір CVBS (I²C `0x59`) ✅

| reg | Ім'я | Призначення | Біти |
|-----|------|-------------|------|
| `0xD7` | `IO_AV_video_on_off` | enable/disable video | **bit3,4** = vid on(3)/off(0); FIZIK init=`0xFC` |
| `0xD8` | `IO_AV_input_select_reg_0` | вибір входу #0 | **bit6,7** (значення ⚠ ревізія-залежні, 12.2) |
| `0xDC` | `IO_AV_input_select_reg_1` | вибір входу #1 | **bit4,5**; bit4/6 = no-signal фон (11.5) |
| `0x26` | `IO_AV_stat_detect_0` | video DETECT (R) | **bit1** = головний detect-прапор |
| `0x28` | `IO_AV_stat_framerate_flag` | PAL/NTSC (R) | **bit2** = 50/60 Гц |
| `0x2A` | `IO_AV_stat_signal_detect` | детект per-вхід (R) | **bit4=HaveCVBS1, bit6=HaveCVBS3**, bit0–3=ErrorFlags |
| `0x15` | `IO_AV_ctrl_sensitivity_0` | чутливість декодера | `00h`=max, `05h`=med, `09h`=low |

### 2.5 БАНК `FFxx` — video brightness/contrast/saturation (I²C `0x5A`) ✅

| reg | Ім'я | Параметр | Medium | Безпечний діапазон (MED±0x28) |
|-----|------|----------|--------|-------------------------------|
| `0xD3` | `IO_LCD_basic_contrast` | контраст | `0x7E` | `0x56..0xA6` |
| `0xD4` | `IO_LCD_basic_brightness` | **video-яскравість** (не підсвітка!) | `0x8E` | `0x66..0xB6` |
| `0xD5` | `IO_LCD_basic_tint` | tint (лише NTSC) | `0x00` | ⚠ **bit7 ламає PAL-колір** — НЕ ставити |
| `0xD6` | `IO_LCD_basic_saturation` | насиченість | `0x38` | `0x10..0x60` |
| `0xD2` | `IO_LCD_forced_blank_color` | **display on/off** | `0x4F`=показ AV/OSD, `0x54`=blank чорний | |
| `0xB0` | `IO_LCD_snow_enable_and_misc` | snow/backdrop ctrl | `0x00`=off, `0xA3`=on | |

### 2.6 БАНК `FDxx` — Global/PWM (I²C `0x58`) ✅ — ТІЛЬКИ безпечні регістри

| reg | Ім'я | Призначення | I²C-безпека |
|-----|------|-------------|-------------|
| `0x13` | `IO_PLL_13h_used` | **screen on/off** | ✅ `0xFF`=on, `0x00`=off |
| `0x1F` | `IO_PWM_enable_flags` | bit0–3 = PWM0–3 enable | ✅ FIZIK `0x03` |
| `0x20`/`0x21` | `IO_PWM0_duty_total_lsb/msb` | PWM0 total (період, цикли **27 МГц**) | ✅ |
| `0x28`/`0x29` | `IO_PWM0_duty_high_lsb/msb` | PWM0 high (duty) = **«яскравість» FIZIK** | ✅ |
| `0x42` | `IO_PIN_P35_P36_pwm` | bit0–1 → пін у PWM-режим | ✅ FIZIK `0x03` |
| `0xBC`–`0xC1` | `IO_ADC_input_0..2` | ADC keypad (R) | ✅ читати |

> **Решта банку `0x58` (FDxx) — DANGER** (SPI-flash, ADC config, PLL `FD01/11/12`). Див. розділ 10.
> Пиши у FDxx **лише** перелічені вище регістри. Жодних «init-петель усього банку».

### 2.7 БАНК `FCxx` — LCD/Tcon (I²C `0x5C`) ⚠ — НЕ чіпати

Справжній LCD/Tcon-тайминг (50/60 Гц H-rate, V-позиція, кроп). OEM-прошивка вже все ініціалізувала.
Зміни масштабу/кропу легко вішають картинку (freeze/white screen). **Для Шляху A не чіпати.**

---

## 3. Модель OSD

### 3.1 Три області пам'яті

| Область | Розмір | Призначення |
|---------|--------|-------------|
| **BGMAP RAM** | **200h слів (512)** | «екран»: слово = 10-біт № символу + 7-біт атрибут |
| **FONT RAM** | 1000h слів (~8 КБ, «4096×16 bits») | завантажуваний шрифт (кирилиця/графіка), коди `1C0h+` |
| **FONT ROM** | 418 символів 16×22 1bpp | вбудований; ⚠ розкладка кодів прошивко-залежна (12.4) |

### 3.2 Номери символів (для BGMAP)

| Діапазон | Що |
|----------|-----|
| `020h..1BFh` | ROM-шрифт (⚠ прошивко-залежний — не покладатися, 12.4) |
| `1C0h..(1C0h+bitmap_start)` | **кастомний RAM-шрифт 1bpp** (наша кирилиця/іконки) |
| `(1C0h+bitmap_start)+` | RAM-шрифт 4bpp (16-кольорова графіка; OEM не вживає — ризик) |

Межу TEXT/BITMAP задає `FB11h`/`FB70h` (`bitmap_start`).

### 3.3 BGMAP-адреса ↔ клітинка — ПРОГРАМНА, не апаратна

Апаратура має лише лінійний 9-біт лічильник `addr` (`FB0Dh:FB00h`), що авто-інкрементується при кожному
записі `data_lsb` (`FB01h`). Перенос lsb→msb **апаратно НЕ робиться** — стежити вручну. Формула клітинки
(для повного 16px-шрифту, 1 символ/слово) — її задаємо ми:

```
bgmap_addr = row * stride + col       (stride = size_x вікна)
```

Для ModESP рекомендовано **`char_xsiz=16, char_ysiz=22/24`** (1 символ/слово, як FIZIK) — без overlap-граблів
малого xsiz (`char_xsiz<12` дає «CHAR 0 overlaps CHAR 1»). При stride=20 і 512 словах → до 25×20=500 клітинок.

### 3.4 5 вікон — пер-віконні регістри (ENGELS, точні адреси)

| Поле | W0 | W1 | W2 | W3 | W4 |
|------|:--:|:--:|:--:|:--:|:--:|
| `size_x` (симв.) | `0x07` | `0x12` | `0x18` | `0x1E` | `0x24` |
| `size_y` (симв.) | `0x08` | `0x13` | `0x19` | `0x1F` | `0x25` |
| `xyloc_msb` | `0x09` | `0x14` | `0x1A` | `0x20` | `0x26` |
| `xloc_lsb` | `0x0A` | `0x15` | `0x1B` | `0x21` | `0x27` |
| `yloc_lsb` | `0x0B` | `0x16` | `0x1C` | `0x22` | `0x28` |
| `vramaddr_lsb` | **немає (фікс 000h)** | `0x17` | `0x1D` | `0x23` | `0x29` |

- **W0** — найвищий пріоритет (поверх), BGMAP-addr **фіксована = 000h**, без vramaddr. 5 байт (`0x07..0x0B`).
- **W1–4** — нижчий пріоритет (1>2>3>4), кожне 6 байт, з власним `vramaddr` (9 біт: `vramaddr_lsb` + старший біт у `xyloc_msb.bit7`) → дивиться на довільну ділянку BGMAP.
- **Пріоритет = номер вікна** (фіксований апаратно; окремого z-order регістру немає).
- **Прозорий колір 0** у клітинці = крізь нього видно нижче вікно / живе відео.
- `xloc=10`=крайня ліва, `yloc=12`=крайня верхня (базове зміщення).
- ⚠ Точна бітова розкладка `xyloc_msb` (старші біти xloc/yloc) реверсом не дана — встановити емпірично (12.7).

### 3.5 Палітра / колір / прозорість

**6 програмованих кольорів** (1–6) + 2 фіксовані (0=прозорий/відео, 7=чорний). Формат **RGB444** у двох байтах:
```
lsb (напр. FB57h): bit0–3 = Red(0..0Fh),  bit4–7 = Green(0..0Fh)
msb (напр. FB56h): bit0–3 = Blue(0..0Fh), bit4–7 = 0
```
Дефолти (ENGELS `osd_init_six_colors`): 1=червоний, 2=зелений, 3=синій, 4=жовтий, 5=cyan, 6=білий.

**Атрибут `FB10h`:** `attr = (FG & 7) | ((BG & 7) << 4)`. Один атрибут «латчиться» для наступних
записів `data_lsb`; OEM тримає один колір на вікно. Для різнокольорового тексту в одному вікні — міняти
`FB10h` між групами символів.

**Напівпрозорість** (відео крізь OSD): `FB06h` bit6=фон, bit7=текст; рівень `FB0Ch` bit0–2 (0=невидимо..7=майже непрозоро). Дозволяє статус-текст поверх живого відео дрона. ⚠ Тримай рівень у 0..7; нетипові значення `FB0Ch` дають «flimmer».

### 3.6 Розмір тексту — три рівні керування

| Рівень | Регістр | Дія | Область |
|--------|---------|-----|---------|
| Глобальна база | `FB76h`/`FB77h` (xsiz/ysiz px) | формат тайла шрифту | **весь OSD** |
| Per-window масштаб | `FB32h` (W0), `FB33h/34h` (W1–4) | апаратне розтягування ×1..×4 | **окреме вікно** |
| Субпіксельний | `FB2Bh..FB31h` | по-рядковий/по-піксельний (для >16px символів) | W0 |

> **Різний розмір одночасно = різні вікна з різним per-window scale.** Великий банер → W1 ×2/×3
> через `FB33h`; дрібний статус → W0 ×1. FB76/77 — глобальна база, FB32/33/34 — індивідуальний множник.

---

## 4. Конвеєр кириличного шрифту

ROM-кирилиця прошивко-залежна і **недостатня для української** (бракує `Є І Ї Ґ` + малих літер).
**Надійний шлях:** вантажити власний повний набір у FONT RAM (`1C0h+`) і будувати власну UTF-8→tile-мапу
(точно як це роблять самі заводські прошивки ENGELS/CLEAN через `normal_char_xlat_table`).

### 4.1 Формат тайла 1bpp 16×22 (механізм підтверджено ENGELS; розмір — ціль ModESP)

> ✎ **Виправлено (верифікація):** ENGELS `main_font` сам по собі = **32 байти/гліф** (16 слів, 12×16) — НЕ 44. **44 байти — це НАШ формат для 16×22** (22 ряди × 2 байти), коректно виведений з `INNER.LEN=ysiz` (FB77). Розмір НЕ апаратно-фіксований — задається `FB76/77` (див. 12.14). Підтверджено першоджерелом саме **механізм** (word-addressed FONT RAM; msb→FB04 пишеться першим, lsb→FB03 латчить останнім), а не конкретний розмір main_font.

- Гліф = **22 ряди**, кожен ряд = **одне 16-бітне слово** (бо ширина ≤16) → **44 байти/гліф** (формат-ціль ModESP, не розмір main_font).
- Слово = `[msb : lsb]`: **MSB-байт = пікселі 1–8 (зліва), LSB-байт = пікселі 9–16**.
- Усередині байта: **bit7 = крайній лівий піксель** (MSB-first). ⚠ SPEC §7 каже «bit0=лівий» — **довіряти дизасемблеру (MSB-first)**, звірити на бенчі (12.5).
- Біт=1 → передній план (колір атрибута); біт=0 → прозоро/фон.
- Адреса слова символу з RAM-індексом `N`: `font_addr = INNER.LEN * N`, де `INNER.LEN = ysiz` для xsiz≤16 (= 22 для 16×22).

### 4.2 Запропонована UTF-8 → tile мапа (повний укр. набір, RAM `1C0h+`)

| Діапазон UTF-8 | tile № | К-сть |
|----------------|--------|-------|
| ASCII `0x20`–`0x7E` | `1C0h` + (cp − 0x20) | 95 |
| `°` U+00B0 | `21Fh` | 1 |
| А–я U+0410..U+044F | `220h` + (cp − 0x0410) | 64 |
| Є І Ї Ґ є і ї ґ | `260h`..`267h` | 8 |

Разом 168 гліфів × 44 байти = **7392 байти** — вкладається у 8 КБ FONT RAM. `bitmap_start` (`FB11h`) ставити ≥ `0xA8` (168), щоб усі ці tile-и лишились у 1bpp-зоні.

### 4.3 Протокол ручного завантаження (ENGELS `osd_upload_font_characters_manually`)

```c
// dev = 0x5B; addr — у СЛОВАХ; data: спершу msb (FB04h), потім lsb (FB03h) — lsb латчить обидва
void amt_load_font(const uint16_t* glyphs, uint16_t first_tile, uint8_t count) {
    amt_w(0x5B, 0x76, 16);                 // char_xsiz = 16px
    amt_w(0x5B, 0x77, 22);                 // char_ysiz = 22px → INNER.LEN=22 слів
    amt_w(0x5B, 0x05, 0x00);               // ВИМКНУТИ всі вікна (обов'язково — інакше glitch)
    delay_ms(25);                          // вимкнення діє з наступного кадру (~20 мс @50Гц)
    const uint16_t W = 22;
    for (uint8_t c = 0; c < count; ++c) {
        uint16_t addr = W * (first_tile + c);
        for (uint16_t row = 0; row < W; ++row) {
            uint16_t a = addr + row;
            amt_w(0x5B, 0x0F, (a >> 8) & 0x0F);    // font_addr_msb (4 біти)
            amt_w(0x5B, 0x02,  a & 0xFF);          // font_addr_lsb
            uint16_t w = glyphs[c*W + row];
            amt_w(0x5B, 0x04, (w >> 8) & 0xFF);    // data_msb ← ПЕРШИМ
            amt_w(0x5B, 0x03,  w & 0xFF);          // data_lsb ← латч ОСТАННІМ
        }
    }
    amt_w(0x5B, 0x05, 0x1F);               // УВІМКНУТИ вікна назад
}
```
> **Чому не DMA:** FLASH→FONT DMA потребує `SFR C6h.bit3` (CPU-SFR) і DANGER-регістрів SPI-flash — по I²C
> уникаємо. Glitch-захист = вимкнення вікон (`FB05h=0`), не C6h. Шрифт вантажиться **один раз** при старті.
> ⚠ Авто-інкремент `FB02h` при ручному записі НЕ гарантований — виставляти addr щоразу (бенч: 12.3).

### 4.4 Адаптація `gen_osd_font.py` — режим `--target amt630a`

Поточний генератор пакує 12×18 **2bpp** `.mcm`/`OSD_FONT[]` (54 байти/гліф) для AT7456E NVM —
**несумісний формат**. Потрібен новий цільовий режим:

| Параметр | AT7456E (поточний) | AMT630A (новий) |
|----------|--------------------|-----------------|
| Розмір гліфа | 12×18 px | **16×22 px** |
| Глибина | 2 bpp (00/01/10/11) | **1 bpp** (1=fg, 0=прозоро; колір з атрибута) |
| Байт/гліф | 54 (+10 паддінг) | **44** (22 ряди × 2 байти) |
| Контейнер | `.mcm` + `OSD_FONT[]` | C-масив `uint16_t[]` (22 слова/гліф) |
| Завантаження | character-NVM (SPI) | ручний запис `FB02/0F/03/04` по I²C |

**Переюзати без змін:** UTF-8-декодер (`utf8_decode`, `osd_map_utf8`) — 1:1. Логіку рендера TTF→bool-матриця.
**Адаптувати:** прибрати 2bpp-outline (1bpp не має чорного; ореол робиться кольором фону атрибута);
нова функція пакування у 16-біт слова MSB-first; вивід C-хедера `amt630a_font_data.h`; нова мапа `cp→tile` (§4.2).

```python
# AMT630A: 22 слова/гліф, MSB-first (bit15 = лівий піксель)
CHAR_W, CHAR_H = 16, 22
def pack_glyph_amt630a(rows):       # rows: 22×16 bool
    return [sum((1 << (15 - x)) for x in range(16) if rows[y][x]) for y in range(CHAR_H)]
```

### 4.5 Графіка / іконки / бари (той самий механізм, 1bpp)

- **Лого/силует** (як Fizik `myBitmap` 64×64 SSD1306, MSB-first) → порізати на сітку 16×N тайлів → залити у FONT RAM → розкласти коди у BGMAP вікна. Формат тривіально конвертується (той самий MSB-first).
- **Прогрес-бар / бар рівня** — **8 тайлів-рівнів заповнення** (`1C0h..1C7h`): тайл k заповнений на k/8 ширини (k*2 старших біт=1). Оновлення = запис кодів у BGMAP (атомарно). Найнадійніший патерн (без 4bpp-ризику).
- **Іконка батареї/сигналу** — НЕ покладатися на ROM `137h` (дизасемблером не підтверджено, 12.8); малювати власними тайлами.
- **4bpp 16-кольорова графіка** — лишити на «дослідницький бенч»: регістри відомі (`FB05h.bit7`, `FB35h.bit4`, палітра `FB36h+`), але OEM її не вживає і формат тайла не верифікований.
- Розширити `gen_osd_font.py` режимом «icon» (PNG/растр → 16-біт MSB-first слова) — перевикористати завантажувач FONT RAM.

---

## 5. Мапінг спроможностей на регістри

### (a) Системні сповіщення різними РОЗМІРАМИ та КОЛЬОРАМИ

Механізм: **окреме вікно з власним per-window scale + атрибут кольору** поверх меню.

```c
// «УВАГА» великим червоним поверх усього (вікно 0 — найвищий пріоритет, addr фікс 000h)
amt_w(0x5B, 0x32, 0x05);            // FB32: W0 ScaleX=2(×2), ScaleY=2(×2)  (bit0-1=1, bit2-3=1)
amt_set_color(1, 15, 0, 0);         // color1 = червоний (дефолт уже червоний)
amt_w(0x5B, 0x10, (1 & 7) | (0<<4));// attr: FG=color1(червоний), BG=прозорий = 0x01
osd_print(0x000, "УВАГА", ...);     // у вікно 0 (BGMAP addr 000h)
amt_w(0x5B, 0x05, prev | 0x01);     // увімкнути W0 (bit0); таймаут → зняти bit0
```
- Кольори: перевизначити палітру `FB56h..FB61h` під семантику (червоний=alarm, жовтий=warn, зелений=ok).
- Розмір: `FB32h`/`FB33h`/`FB34h` per-window scale ×1..×4.
- Поверх живого відео: напівпрозорий фон (`FB06h` bit6, рівень `FB0Ch`).
- ⚠ Не копіювати магічний FIZIK-attr `0x09` — явно `(FG&7)|((BG&7)<<4)`, bit3 не чіпати (12.9).

### (b) Керування параметрами екрану з ESP32

| Пункт меню | Регістр(и) | dev | Діапазон |
|-----------|-----------|-----|----------|
| **Підсвітка / яскравість екрана** | `FD28/29` duty, `FD20/21` total, `FD1F`=01 | `0x58` | total≥0x0200; 50%=high `0x0800`/total `0x1000` |
| **Video-яскравість** | `FFD4h` | `0x5A` | `0x66..0xB6` (med `0x8E`) |
| **Контраст** | `FFD3h` | `0x5A` | `0x56..0xA6` (med `0x7E`) |
| **Насиченість** | `FFD6h` | `0x5A` | `0x10..0x60` (med `0x38`) |
| **Tint (NTSC)** | `FFD5h` | `0x5A` | `0x00`; **bit7 НЕ ставити** |
| **Вибір CVBS-входу** | `FED7→FED8→FED7→FEDC` (строгий порядок) | `0x59` | ⚠ значення ревізія-залежні (бенч, 12.2) |
| **No-signal фон** | `FEDCh` bit4/6 (Blue/Snow), bit5 (off) | `0x59` | RMW окремих біт |
| **Статус сигналу (R)** | `FE26`b1, `FE2A`b4/b6, `FE28`b2 | `0x59` | read-only (⚠ чи читається по I²C — бенч) |
| **Меню on/off** | `FB05h` (bit0–4; bit6 hide) | `0x5B` | `0x1F`=всі, `0x00`=off |

> Є **дві різні «яскравості»**: PWM-підсвітка (FD28/29, фізична — як у FIZIK) і video-brightness декодера (FFD4h). «Яскравість екрана» в меню = PWM-підсвітка (перевірений шлях).

### (c) Рендер ПОВНОГО навігаційного меню ModESP

- **Window 0** (або W1 з vramaddr) = повне меню: `size_x=20, size_y=N, char 16×22`, рядки кадру → ряди BGMAP.
- `render(frame)`: для кожного рядка — `osd_print(bgmap_addr, utf8→tile, attr)`, UTF-8→tile через `amt630a_charmap.h` (кирилиця у RAM `1C0h+`).
- **Diff-оновлення:** тримати тіньовий буфер у RAM ESP32 (BGMAP write-only); писати лише змінені рядки/символи (мінімізувати I²C-трафік; `dev.clear()` перед кожним кадром тут шкідливий).
- **Стратегія шарів:** W0 (пріоритет №1, фікс 000h) = OVERLAY/ALARM банер (зазвичай вимкнено); W1 (vramaddr напр. 040h) = повне меню (завжди ON); W2–4 = резерв (бари сигналу/батареї/RSSI).

### (d) Графіка / іконки — див. §4.5 (1bpp-тайли у FONT RAM + BGMAP).

---

## 6. C++ API

> ⚠ **Переглянуто [ADR-002](../display/ADR-002-display-architecture.md):** AMT630A стає **`Amt630aPort : IDisplayPort`** (семантичний шов), а НЕ `AMT630ARenderer : IDisplayRenderer`. `RowAttr`-розширення `DisplayFrame` (§6.3 п.1) **скасоване** — атрибути несе семантичний `MenuView` + `CharGridLayout`. `set_*`/`select_input` → no-op-default методи шва + `IVideoInputs`/`IGraphicRenderer` capability-інтерфейси (`caps()`). Деталі — ADR-002 §7. Решта розділів (карта регістрів, OSD-модель, шрифт, init-послідовність) **лишається чинною**.

Дзеркалить наявний патерн AT7456E (три точки дотику + Kconfig-блок), транспорт — ESP-IDF `i2c_master`.

### 6.1 Новий переносний драйвер `components/modesp_osd/.../amt630a.{h,cpp}`

```cpp
namespace modesp::osd {

struct Amt630aPins { int sda; int scl; uint32_t freq_hz; };  // або готова i2c_master_bus

// I²C 7-біт банки (Rosetta-карта, 2.1)
enum AmtBank : uint8_t {
    AMT_GLOBAL = 0x58,  // FDxx: PWM-підсвітка (тільки whitelisted регістри!)
    AMT_AV     = 0x59,  // FExx: вибір CVBS + детект
    AMT_VIDEO  = 0x5A,  // FFxx: яскравість/контраст/насиченість
    AMT_OSD    = 0x5B,  // FBxx: OSD-рушій
    AMT_INIT   = 0x5F,  // vendor unlock (лише точна послідовність)
};

class Amt630a {
public:
    explicit Amt630a(const Amt630aPins& pins);

    bool init();                                   // bus + (опц.) startup; present()-чек
    bool present();                                // probe ACK на 0x5B

    void    amt_w(uint8_t dev7, uint8_t reg, uint8_t val);   // §2.2 (whitelist-guard у DEBUG)
    uint8_t amt_r(uint8_t dev7, uint8_t reg);

    // ── OSD (dev 0x5B) ──
    void window_setup(uint8_t win, uint8_t w, uint8_t h, uint8_t x, uint8_t y);
    void set_char_size(uint8_t xsiz, uint8_t ysiz);          // FB76/77 (16,22)
    void set_window_scale(uint8_t win, uint8_t sx, uint8_t sy); // FB32/33/34 (тіньова копія — обхід багу 12.6)
    void window_enable(uint8_t mask);                        // FB05h
    void osd_print(uint16_t bgmap_addr, const uint16_t* tiles, size_t n, uint8_t attr); // 10-біт tile → FB0E+FB01
    void set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);   // FB56..FB61, RGB444
    void set_transparency(bool on, uint8_t level);           // FB06/FB0C
    void upload_font(const uint16_t* glyphs, uint16_t first_tile, uint8_t count); // §4.3 (вимикає вікна)

    // ── Параметри екрана ──
    void set_backlight(uint8_t pct);             // dev 0x58: FD20/21 total + FD28/29 duty + FD1F (§11)
    void set_video_brightness(uint8_t pct);      // dev 0x5A: FFD4 (map 0x66..0xB6)
    void set_contrast(uint8_t pct);              // dev 0x5A: FFD3 (map 0x56..0xA6)
    void set_saturation(uint8_t pct);            // dev 0x5A: FFD6 (map 0x10..0x60)
    void select_input(uint8_t in);               // dev 0x59: FED7→FED8→FED7→FEDC (тіньові копії)
    void display_on(bool on);                    // dev 0x5A FFD2 (0x4F/0x54) + 0x58 FD13

    // ── Статус (R; може не читатись по I²C — бенч) ──
    bool have_signal();                          // FE26 bit1
    bool have_cvbs1();  bool have_cvbs3();        // FE2A bit4/bit6
    bool is_ntsc();                              // FE28 bit2

private:
    Amt630aPins pins_;
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t devs_[6] = {};       // по адресі банку
    uint8_t shadow_fed7_, shadow_fed8_, shadow_fedc_;   // тіньові копії для RMW
    uint8_t shadow_scale33_, shadow_scale34_;            // обхід OEM-багу FB33/34
};

} // namespace modesp::osd
```
> **DANGER-guard (ДВОРІВНЕВИЙ — виправлено за верифікацією):** розрізняй два режими запису:
> 1. **Разова init-послідовність** — вшита `const`-таблиця точних FIZIK-значень (вкл. `FD11/FD12/FD13/FD19` PLL, що поза runtime-whitelist і позначені DANGER, але **доведені FIZIK на залізі**) → виконується через окремий `apply_init_table()`, **звільнений** від guard.
> 2. **Runtime** `amt_w` (динамічні `set_*`) — у DEBUG асертить whitelist: банк `0x58` дозволяє лише `0x13/0x1F/0x20/0x21/0x28/0x29/0x42/0xBC..0xC1`; ніколи `FDD0h`, `FDDEh` bit6, `FDE0h`, `FD32h/FD33h`, `FFD5h` bit7.
>
> ⚠ Без цього поділу наївний guard заблокував би доведений init (він пише `FD11/FD12`). Init-таблиця — вичерпний вайт-перелік сама по собі (точні байти Fizik, не імпровізувати).

### 6.2 Рендерер `modules/display/.../amt630a_renderer.{h,cpp}`

Дзеркало `AT7456ERenderer`, увесь TU під `#ifdef CONFIG_MODESP_DISPLAY_AMT630A`:

```cpp
class AMT630ARenderer : public IDisplayRenderer {
public:
    AMT630ARenderer();                            // піни/вхід/яскравість з Kconfig; задати шрифт
    bool init() override;                         // §7 init → present() → upload_font → window_setup → палітра
    void render(const DisplayFrame& frame) override;   // diff проти тіні → osd_print по рядках
    IGraphicRenderer* as_graphic() override { return &graphic_; }   // §6.3
    // overlay-сповіщення:
    void notify(const char* text, uint8_t level); // вікно-банер + scale + колір; таймаут знімає
private:
    osd::Amt630a dev_;
    DisplayFrame  shadow_;                        // BGMAP write-only → тінь для diff
    const uint16_t* font_; size_t font_count_;
    /* GraphicImpl graphic_; */
};
```

### 6.3 Потрібні розширення інтерфейсу (зворотно-сумісні)

`renderer.h` зараз — простий монохромний текст 4×40. Щоб задіяти колір/розмір/графіку/сповіщення:

1. **Per-row атрибути** (опційні, дефолт = поточна поведінка):
   ```cpp
   struct RowAttr { uint8_t fg:3; uint8_t bg:3; uint8_t scale:2; uint8_t flags; };
   struct DisplayFrame {
       static constexpr size_t MAX_ROWS = 4;     // ⚠ збільшити (напр. 16) під повне TFT-меню
       static constexpr size_t MAX_BYTES = 40;
       etl::string<MAX_BYTES> rows[MAX_ROWS];
       RowAttr attrs[MAX_ROWS] = {};              // дефолт {fg=біл,bg=прозор,scale=×1}
       // operator== має враховувати attrs для dirty-логіки
   };
   ```
   Текстові рендерери (Log, AT7456E) `attrs` ігнорують → поведінка не змінюється. AMT630A читає → `FB10h` + scale.
2. **Опційний графічний інтерфейс** (zero-cost, без RTTI):
   ```cpp
   class IGraphicRenderer {
   public:
       virtual void draw_icon(uint8_t win, uint8_t x, uint8_t y, uint16_t tile) = 0;
       virtual void draw_bar (uint8_t win, uint8_t x, uint8_t y, uint8_t pct)  = 0;
   };
   // в IDisplayRenderer: virtual IGraphicRenderer* as_graphic() { return nullptr; }
   ```
3. **Сповіщення** — рішення винесено в **[ADR-001](ADR-001-osd-notifications.md)** (заміняє цей пункт).
   ⚠ **Спростовано попередню тезу «шини немає»:** `DisplayModule` **успадковує `on_message`** від `BaseModule`
   (`base_module.h:43`) і **вже підписаний** на синхронну `etl::message_bus<24>` через catch-all `ModuleAdapter`
   (`module_manager.h:43`) — шина Є, просто хук не перевизначено. Транспорт сповіщень = **подія
   `MsgSystemNotice{level,ttl_ms,text}` на цю шину** (новий `msg_id::SYSTEM_NOTICE` у діапазоні сервісів 50–99,
   поряд із наявними `MsgSafeMode`/`MsgSystemError`). `DisplayModule` перевизначає `on_message` → кладе подію у
   статичну **priority-чергу** банерів (`etl::vector<…,8>`, дроп найнижчого, не нового ALARM); показ/TTL/витіснення
   веде `on_update(dt_ms)`. SharedState (`display.banner`, `display.banner_level`) — **дзеркало** для WebUI/MQTT,
   НЕ транспорт. Деталі, таблиця компромісів і код — в ADR-001.

---

## 7. Доведена init-послідовність (FIZIK)

Це наземна істина — `initDisplay()` + `onDisplay()` з робочого детектора. **Холодний init** поверх уже-працюючої прошивки: unlock → standby (екран off). **onDisplay** → screen on + AV-tune + OSD on.

### 7.1 `initDisplay()` (25 записів + delay 200 мс) — standby-стан

```
# unlock-handshake (vendor-канал 0x5F):
0x5F,0xAF,0x00 ; 0x5F,0xA1,0x55 ; 0x5F,0xA2,0xAA ; 0x5F,0xA3,0x03 ; 0x5F,0xA4,0x50
0x5F,0xA5,0x00 ; 0x5F,0xA6,0x53 ; 0x5F,0xAF,0x11
0x5F,0xC6,0x42 ; 0x5F,0xC6,0x00          # memory_system poke (⚠ DANGER лише поза цим каналом — 12.3)
# PWM/PLL/screen:
0x58,0x42,0x03 ; 0x58,0x1F,0x03          # PIN PWM-режим + PWM0/1 enable
0x58,0x28,0x00 ; 0x58,0x29,0x00          # PWM0 duty = 0 (підсвітка OFF)
0x58,0x11,0x1F ; 0x58,0x12,0x38 ; 0x58,0x13,0x00   # PLL + screen OFF
0x59,0xDC,0x00                            # AV input reset
0x5A,0xD2,0x54 ; 0x59,0xD7,0xFC ; 0x5A,0xB0,0x00   # forced-black, video off, snow off
0x5B,0x05,0x1F                            # усі 5 OSD-вікон ON
0x5F,0xBE,0x55 ; 0x5F,0xBA,0x00 ; 0x5F,0xBE,0xAA   # watchdog unlock/commit
delay(200)
```

### 7.2 `onDisplay()` (~60 записів) — увімкнути відео + OSD

```
0x58,0x11,0xFF ; 0x58,0x12,0xFF ; 0x58,0x13,0xFF   # screen ON
0x59,0x07,0x01 ; 0x59,0x11,0x01 ; 0x59,0xDC,0x20   # AV config + вибір входу
0x5A,0xD2,0x4F                            # forced_blank = показати AV-відео
0x59,0x01,0x06 ; 0x59,0x04,0x80 ; 0x59,0x05,0x30 ; 0x59,0x54,0x40   # AV-decoder tune
0x59,(0x8A..0xE3) ...                     # ~25 регістрів тонкого тюнінгу декодера
0x5A,0xB0,0xA3 ; 0x5A,0xB2..0xB4,0x1C     # snow + LCD-config
0x5A,0xD3,0x80 ; 0x5A,0xD4,0x80 ; 0x5A,0xD6,0x56   # контраст/яскравість/насиченість
0x5A,0xDA,0x6C ; 0x5A,(0xF0..0xFA) ...    # backdrop + GAMMA-таблиця
0x5B,0x05,0x1F                            # OSD усі вікна ON
0x58,0x19,0x08 ; 0x58,0x42,0x03 ; 0x58,0x1F,0x03   # PLL + PWM enable
changeBrightness(preset)                  # FD28/29 duty з пресету
```

### 7.3 Мінімальна послідовність ModESP (Шлях A, поверх OEM)

```
1. Power-on: дочекатись стабілізації OEM (~200 мс).
2. present(): probe ACK на 0x5B. Немає → renderer disabled (система не падає).
3. (опц.) unlock + onDisplay-tune (порт FIZIK) — лише якщо OEM сама не показує відео.
4. Підсвітка: FD20/21 total, FD28/29 high, FD1F enable          (dev 0x58)
5. Вибір CVBS: FED7→FED8→FED7→FEDC (тіньові копії)              (dev 0x59)
6. (раз) Кирилиця у FONT RAM 1C0h+: FB05=0 → upload → FB05=маска (dev 0x5B)
7. window_setup: FB05/07/08/09/0A/0B + FB76/77 + FB32           (dev 0x5B)
8. Палітра FB56..FB61; картинка FFD3/D4/D6                       (dev 0x5B / 0x5A)
9. Цикл: render(frame) diff → osd_print при зміні даних          (dev 0x5B)
```

---

## 8. Поетапний план реалізації

| Етап | Зміст | Файли | Критерій готовності |
|------|-------|-------|---------------------|
| **0. Бенч-bring-up** | I²C-probe AMT630A (ACK 0x5B); порт FIZIK `initDisplay`/`onDisplay`; вивід ROM-цифр у W0 | (скрипт/тест) | відео + один ROM-рядок на TFT |
| **1. Драйвер-кістяк** | `modesp_osd::Amt630a`: `amt_w/amt_r`, `present`, init поверх OEM, whitelist-guard | `components/modesp_osd/include/modesp/osd/amt630a.h` + `src/amt630a.cpp` | компілюється, probe працює |
| **2. Шрифт-конвеєр** | `gen_osd_font.py --target amt630a` (16×22 1bpp, `uint16_t[]`); `amt630a_charmap.h` (UTF-8→tile §4.2); `upload_font` | `tools/gen_osd_font.py`, `components/modesp_osd/.../amt630a_charmap.h`, `amt630a_font_data.h` | кирилиця залита, host-тест мапи |
| **3. Текстовий рендерер** | `AMT630ARenderer : IDisplayRenderer` (дзеркало AT7456E), diff проти тіні; Kconfig (РЕФАКТОР наявного `bool MODESP_DISPLAY_AT7456E` → `choice` рендерів); інстанс + `default_renderer()` гілка; CMake | `modules/display/include/display/amt630a_renderer.h` + `src/amt630a_renderer.cpp`, `modules/display/Kconfig`, `src/display_module.cpp`, `CMakeLists.txt` | повне меню ModESP українською на TFT |
| **4. Параметри екрана** | `set_backlight/contrast/brightness/saturation`, `select_input`, `display_on`; пункти меню налаштувань | `amt630a.cpp` + manifest меню | меню керує яскравістю/входом |
| **5. Колір+розмір** | `RowAttr` у `DisplayFrame` (зворотно-сумісно); per-window scale; палітра під семантику | `renderer.h`, `amt630a_renderer.cpp`, `menu_engine.cpp` | різнокольорові/різнорозмірні рядки |
| **6. Сповіщення** | подія `MsgSystemNotice` на `etl::message_bus` (ADR-001); `DisplayModule::on_message`→priority-черга, TTL у on_update; overlay-банер вікном; дзеркало в `display.banner*` | `display_module.cpp`, `renderer.h` (Banner) | alarm → банер поверх відео |
| **7. Графіка** | `IGraphicRenderer` (icon/bar); 8 тайлів-рівнів; (опц.) лого; режим «icon» генератора | `renderer.h`, `amt630a_renderer.cpp`, `gen_osd_font.py` | бари сигналу/батареї, іконки |
| **8. (опц.) 4bpp** | дослідницький: `FB05h.bit7`, `FB35h.bit4`, палітра `FB36h+` — лише після бенч-верифікації | — | кольорове лого (за потреби) |

Етапи 0–3 — критичний шлях (повне меню). 4–7 — інкрементальні, зворотно-сумісні. 8 — опційний/ризиковий.

---

## 9. Чеклист бенч-перевірки

**Транспорт/init:**
- [ ] I²C ACK на `0x5B` (probe present); шина 100 кГц стабільна (вище — перевірити).
- [ ] Чи OEM-прошивка сама показує відео без unlock — чи потрібен порт `initDisplay`/`onDisplay`.
- [ ] Чи OEM-прошивка **перезаписує** наші OSD-вікна / FFD3-D6 / PWM (конфлікт; у неї є `apply_settings_to_IO_ports`).

**OSD/шрифт:**
- [ ] Порядок lsb/msb у FONT-data (`FB03h`/`FB04h`) — гліфи не дзеркальні по байтах (12.5).
- [ ] Біт-порядок у слові тайла: MSB=лівий (дизасемблер) vs SPEC «bit0=лівий».
- [ ] Авто-інкремент `FB02h` при ручному записі — чи можна виставляти addr раз на гліф (×4 швидше) (12.3).
- [ ] `char_xsiz=16/char_ysiz=22` дає коректний 1-симв/слово рендер (INNER.LEN=22).
- [ ] Палітра `FB56h..FB61h` реально приймає запис по I²C (Korth позначив «fixed»; ENGELS-код пише) (12.1).
- [ ] Per-window scale `FB33h/FB34h` прямим записом повного байта (обхід OEM-багу `and 0F0h`) (12.6).
- [ ] Реальний колір attr `0x09` FIZIK (12.9) — не використовувати, явний `(FG&7)|((BG&7)<<4)`.
- [ ] Рівень напівпрозорості `FB0Ch` bit0–2 без «flimmer».

**Параметри/вхід:**
- [ ] **Полярність PWM-підсвітки** (high=яскравіше чи темніше? FIZIK натякає на інверсію) (12.10).
- [ ] **Точні bit-значення CVBS-входів** на ревізії прошивки KOZHAN (перебрати FED8 bit6-7∈{0,2,3}, FEDC bit4-5) (12.2).
- [ ] Чи `FD18h.bit7` (ForceMaxBacklight)=0 — інакше PWM-dimming вимкнено.
- [ ] Чи читаються `FE26/28/2A` по I²C (детект сигналу ззовні); якщо ні — no-signal по ADC1 ESP32.
- [ ] PWM total: безпечний `≥0x0200` (нижче ~0x0100 не світить).

**Відкриті апаратні питання:**
- Точна семантика vendor-каналу `0x5F` (магія 55/AA/53/03/50) — не в Korth-реверсі, ревізія-залежна.
- Чи `0x5F` дає READ-доступ до SFR (зокрема vsync `91h`) — FIZIK лише пише.
- 27 vs 26 МГц PWM-clock (для абсолютної частоти/EMI; для duty% неважливо — беремо 27, ENGELS).

---

## 10. Безпека: DANGER-регістри

**Whitelist банків для Шляху A:** OSD `0x5B`, AV `0x59`, video `0x5A`, **PWM-частина** `0x58`
(`0x13/0x1F/0x20/0x21/0x28/0x29/0x42/0xBC..0xC1`), `0x5F` **лише** точними FIZIK-послідовностями.

| Регістр | dev | Небезпека |
|---------|-----|-----------|
| `FDD0h` | `0x58`/`0xD0` | SPI transfer mode — flash erase/DMA, зламає прошивку |
| `FDDEh` bit6 | `0x58`/`0xDE` | hang CPU; SPI start/stop/reset |
| `FDE0h` | `0x58`/`0xE0` | ENGELS: «DANGER!» crashes CPU |
| `FDE5h–FDE7h`, `FDF1h` | `0x58` | unused/DANGER; code-base (bit0–3) |
| `FD32h/FD33h` | `0x58` | SPI-flash піни — фізично ламає flash-доступ |
| `FDB0h/B2h/B4h/B5h` | `0x58` | ADC config — окремі DANGER-біти |
| `FD01h/FD11h/FD12h/FD17h` | `0x58` | PLL: bit0 hang CPU / OSD-error / hang ADC. ⚠ FIZIK пише конкретні значення — **копіювати точно, не імпровізувати** |
| `FD40h/FD41h` | `0x58` | KillTftUpdating / StopDotClk — чорний/завислий екран |
| `FCxx` (`0x5C`) тайминг | `0x5C` | завеликі значення → freeze/white screen |
| `FFD5h.bit7` | `0x5A`/`0xD5` | ламає декодування PAL-кольору (BUG) |
| `SFR C6h/D8h/PCON` | — | CPU-SFR: reboot/hang/halt (⚠ C6h досяжний через `0x5F` — лише в unlock-послідовності) |

> **Закодувати в драйвер** як whitelist-guard на адресу регістра (DEBUG-assert у `amt_w`).

---

## 11. Енергетика: PWM-підсвітка

OEM-прошивка може тримати PWM0 (підсвітка) на максимумі → марна витрата. Перехід на нижчий duty:

| Duty | Уся система | Лише підсвітка |
|------|-------------|----------------|
| 100% | 290 мА (1.45 Вт) | 234 мА |
| **50%** | **65 мА (0.33 Вт)** | **9 мА** |
| 0% | 56 мА | 0 мА |

```c
// 50% (SPEC); base clock 27 МГц (ENGELS, не 26 — 12.10). total ≥0x0200.
amt_w(0x58, 0x20, 0x00); amt_w(0x58, 0x21, 0x10);   // total = 0x1000
amt_w(0x58, 0x28, 0x00); amt_w(0x58, 0x29, 0x08);   // high  = 0x0800
amt_w(0x58, 0x1F, 0x01);                             // PWM0 enable
```
> ⚠ FIZIK варіює лише `high` (0x0014..0x0546) при заводському total, і його шкала виглядає **інвертованою**
> (max яскравість = малий duty) — **перевірити полярність на бенчі** перед лінеаризацією 0–100% (12.10).
> Підсвітка не світить при надто малому total і вимкнена при `FD18h.bit7=1` (ForceMaxBacklight).

---

## 12. Конфлікти джерел

| # | Питання | Як вирішено |
|---|---------|-------------|
| **12.1 Палітра програмована?** | SPEC неоднозначно («fixed??»). | **Програмована.** ENGELS-код `osd_init_six_colors` (@ABE8h) реально пише `FB56h..FB61h`. Дефолти = таблиця 3.5. Бенч: чи приймає запис ззовні (Korth позначив «fixed»). |
| **12.2 CVBS-входи AV1/AV3** | SPEC і ENGELS-new-fw дають **переставлені** bit6-7/bit4-5. | Незмінне: порядок D7→D8→D7→DC і FED7 0→3. Точні bit-значення **ревізія-залежні → бенч** (перебрати FED8∈{0,2,3}). Тіньові копії в RAM ESP32, писати абсолютні значення. |
| **12.3 `SFR C6h` по I²C** | SPEC: «недоступний по I²C, DANGER». FIZIK: успішно пише `0x5F,0xC6,0x42`. | **`0x5F` — окремий vendor-канал** до SFR. C6h-poke безпечний **лише** в точній unlock-послідовності FIZIK. **Не екстраполювати** на довільний запис C6. DMA FLASH→FONT теоретично можливий, але DANGER — не робимо (ручний font-upload). |
| **12.4 ROM-коди символів** | SPEC+FIZIK: space=0, 0-9=1..A, A-Z=B..24. ENGELS/CLEAN: **жодного ROM на низьких кодах** — самі вантажать RAM-шрифт `1C0h+` через xlat. | **Прошивко-залежні.** На платі KOZHAN коди можуть працювати (як у FIZIK), АЛЕ покладатися небезпечно. **Надійно: власний RAM-шрифт `1C0h+` + власна UTF-8 мапа** (§4) — знімає залежність. |
| **12.5 Біт-порядок тайла** | SPEC §7: «bit0=лівий піксель». ENGELS дані `main_font`: **MSB(bit15/bit7)=лівий**. | **Довіряти ENGELS (MSB-first)** — дані гліфів однозначні. Бенч: якщо дзеркальні — інвертувати. Порядок запису слова (msb→FB04 ПЕРШИМ, lsb→FB03 ОСТАННІМ латчить) — з виконавчого коду, не з коментаря (коментарі ENGELS lsb↔msb переплутані). |
| **12.6 OEM-баг FB33/FB34** | OEM `osd_set_text_scale` робить `and 0F0h` замість `0Fh` → затирає сусіднє вікно пари. | При I²C-керуванні **писати весь байт сам** (тіньова копія обох напівбайтів) — баг обходиться. |
| **12.7 `xyloc_msb` біти** | Реверс дав лише `bit7=vramaddr-msb`; розкладка старших біт xloc/yloc не задокументована. | **Встановити емпірично** на бенчі. |
| **12.8 ROM-іконки `137h`** | SPEC: бари батареї/сигналу `137h–13Bh`. ENGELS: **не знайдено**; OEM малює завантаженими тайлами. | **Не покладатися на ROM.** Малювати власними тайлами `1C0h+` (формат доведений). |
| **12.9 Attr FIZIK `0x09`** | FIZIK коментує «FG=білий», але `0x09`=FG=color1(червоний)+bit3. | **Не копіювати магічне `0x09`.** Явно `(FG&7)|((BG&7)<<4)`, bit3 не чіпати; колір — через перевизначення палітри. Бенч: який реальний колір. |
| **12.10 PWM clock + полярність** | SPEC: 26 МГц. ENGELS: **27 МГц** (`019BFCC0h`). FIZIK duty виглядає інвертованим. | Clock = **27 МГц** (firmware-authoritative; для duty% неважливо). **Полярність — бенч** перед лінеаризацією шкали. |
| **12.11 «Яскравість» (дві різні)** | FIZIK «яскравість» (FD28/29 PWM) ≠ SPEC «яскравість» (FFD4h video). | **Обидві валідні, різна природа.** Меню «яскравість екрана» = PWM-підсвітка (фізична, перевірена). Video-brightness FFD4h — окремо. |
| **12.12 Сповіщення в ModESP** | ТЗ припускає наявність; grep `modules/display/` = 0 збігів. | **Спроєктувати з нуля** (§6.3 п.3): SharedState-ключі + overlay-банер, узгодити з alarm-модулем. |
| **12.13 4bpp / `FB05h.bit7`** | SPEC: «bit7=4bpp вікна». ENGELS: «bit7=BITMAP ON/OFF» глоб. шар, функція UNUSED. | bit7 = глобальний bitmap-шар, не per-window. **4bpp слабо доведено** (OEM не вживає) → дослідницький бенч (етап 8). |
| **12.14 Розмір тайла** | SPEC: 16×22. ENGELS/CLEAN: 12×16 (бо ysiz=16 у тих прошивках). | Розмір **не фіксований апаратно** = INNER.LEN(FB76/77). ModESP: 16×22 (як FIZIK). |
| **12.15 `FE2Ah` біти** | SPEC: bit4=вибраний/bit6=невибраний. ENGELS: bit4=HaveCVBS1/bit6=HaveCVBS3 (фізичні). | **Брати ENGELS** (per-фізичний-вхід). Бенч. |

---

**Джерела:** Arkmicro AMT630A Specification V1.1; Martin Korth no$x51 reverse (`ENGELS.A22` OEM 4.3″,
`AMT630A.A22` 3.5″, problemkaputt.de/x51specs.htm); Fizik `AMT630.h` (DETECTOR_FPV 1.36.9.5, підтверджений
I²C на залізі); ModESP `modules/display/`, `components/modesp_osd/`, `tools/gen_osd_font.py`.
Зведено зі звітів `docs/amt630a/research/01..08` + `AMT630A_user_spec.md`.
