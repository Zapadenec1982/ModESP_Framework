# AMT630A — Параметри екрана з ESP32 (керування по I²C, Шлях A)

> Дослідницький звіт. Ціль: повний **безпечний** набір параметрів, які ESP32 виставляє
> у «меню налаштувань екрана»: **яскравість, контраст, насиченість, підсвітка (PWM),
> вибір CVBS-входу, no-signal-режим** — з регістрами, діапазонами і застереженнями.
>
> **Джерела (перехресно звірені):**
> - `docs/amt630a/AMT630A_user_spec.md` — канонічна user-спека (розділи 3, 8, 10, 12).
> - `ENGELS.A22` — OEM-дизасемблер Martin Korth (НАЙБАГАТШИЙ: абсолютні адреси,
>   імена `IO_*`-регістрів, біт-коментарі, реальні init-значення). Цитати = `ENGELS:рядок`.
> - Fizik `AMT630.h` — ДОВЕДЕНИЙ робочий I²C-драйвер (Arduino/Wire).
>
> **Підхід:** Шлях A — керування ззовні по I²C поверх працюючої прошивки. Лише XRAM
> I/O-порти `FBxx–FFxx`; CPU-SFR (`80h–FFh`) недоступні по I²C.

---

## 0. Rosetta-карта банків (нагадування) + важлива поправка до Fizik

| MCU-банк | I²C 7-біт | Підсистема |
|----------|-----------|-----------|
| `FBxx` | **`0x5B`** | OSD (вікна, BGMAP, FONT, палітра) |
| `FCxx` | `0x5C` | Tcon / Scaler / LCD-тайминг |
| `FDxx` | **`0x58`** | Global, **PWM (підсвітка)**, ADC, SPI-flash, PLL, PIN |
| `FExx` | **`0x59`** | AV-декодер / вибір CVBS-входу + детект сигналу |
| `FFxx` | **`0x5A`** | Video-process, GAMMA, **яскравість/контраст/насиченість** |

I²C-транзакція запису: `device7=банк`, `reg=низький байт MCU-адреси`, `data=значення`.
Приклад: `FFD4h = 0x8E` → пишемо у `0x5A`, регістр `0xD4`, значення `0x8E`.

### ⚠ КЛЮЧОВЕ ВІДКРИТТЯ — «яскравість» Fizik це насправді PWM-підсвітка

Fizik у `changeBrightness()` пише у `TFT_LCD_REG` (= `0x58`) регістри `0x28`/`0x29`:
```cpp
#define TFT_LCD_REG 0x58
writeCommand(TFT_LCD_REG, 0x28, 0x14);  // case 0 (найтемніше)
writeCommand(TFT_LCD_REG, 0x29, 0x00);
...
writeCommand(TFT_LCD_REG, 0x28, 0x01);  // case 3/default (найяскравіше)
writeCommand(TFT_LCD_REG, 0x29, 0x00);
```
Device `0x58` = банк **FDxx**. Отже `0x28/0x29` на `0x58` = **FD28h/FD29h = `IO_PWM0_duty_high`**
(`ENGELS:604–605`). Тобто **Fizik регулює яскравість саме через PWM0-підсвітку**, а не через
FFxx-video-brightness. Це **узгоджує** user-спеку (розділ 8: підсвітка = FD28/FD29) з реальним
робочим кодом і **доводить**, що PWM0-підсвітка керована по I²C на цьому залізі.

> Наслідок для меню ESP32: «яскравість екрана» доцільно реалізувати **через PWM0-підсвітку
> (FD28/FD29)** — це і є перевірений Fizik-шлях. Окремо FFD4h (`IO_LCD_basic_brightness`) керує
> video-яскравістю декодера (інша природа). Можна виставити обидва, але «фізичну» яскравість
> екрана дає підсвітка.

---

## 1. Підсвітка PWM — FD-банк / I²C `0x58` (ПЕРЕВІРЕНО Fizik)

### 1.1 Регістри (`ENGELS:587, 595–605, 631`)

| Регістр | I²C (`0x58`) | Ім'я ENGELS | Призначення |
|---------|-------------|-------------|-------------|
| `FD1Fh` | `0x1F` | `IO_PWM_enable_flags` | bit0 = PWM0 (підсвітка) enable; bit0..3 = PWM0..3 |
| `FD20h` | `0x20` | `IO_PWM0_duty_total_lsb` | період (total), молодший байт |
| `FD21h` | `0x21` | `IO_PWM0_duty_total_msb` | період (total), старший байт |
| `FD28h` | `0x28` | `IO_PWM0_duty_high_lsb` | HIGH-тривалість (duty), молодший |
| `FD29h` | `0x29` | `IO_PWM0_duty_high_msb` | HIGH-тривалість (duty), старший |
| `FD42h` | `0x42` | `IO_PIN_P35_P36_pwm` | bit0-1 → пін у PWM-режим; bit0-2 «ScreenBlack(backlight)» |
| `FD18h` | `0x18` | `IO_PLL_18h_pwm` | **bit7 = ForceMaxBacklight → PWM-dimming OFF** (не вмикати!) |

### 1.2 Базовий тактовий генератор — ПОПРАВКА до user-спеки

User-спека (розділ 8) каже «у циклах 26 МГц». **OEM-firmware каже 27 МГц.**
`pwm_set_duty_pwm0` (`ENGELS:17857`) явно завантажує константу:
```asm
8506 7F C0   mov r7,0C0h ; \
8508 7E FC   mov r6,0FCh ; 019BFCC0h (27000000 aka 27MHz)
850A 7D 9B   mov r5,9Bh  ;
850C 7C 01   mov r4,01h  ;/
```
і частоту PWM `0A28h = 2600` Гц:
```asm
84FE AB 07   mov r3,r7  ; \ 00000A28h (2600 Hz)
8500 AA 06   mov r2,r6  ; / C=27MHz/2.6kHz
```
Тобто заводський період `total = 27e6 / 2600 ≈ 10384 ≈ 0x2890`, а duty = `C*duty%/100`.
**⚠ КОНФЛІКТ:** спека пише «26 МГц», firmware — «27 МГц». Для **відносного** керування
(duty% від total) точна частота неважлива; для абсолютної частоти бери **27 МГц** (ENGELS — першоджерело).

Перед записом duty OEM ставить пін у PWM-режим (`ENGELS:17943`):
```asm
8573 90 FD 42  mov dptr,IO_PIN_P35_P36_pwm
8576 E0        movx a,[dptr]
8577 44 03     or  a,03h            ; bit0-1 = PWM output
8579 F0        movx [dptr],a
```

### 1.3 Дві стратегії яскравості/підсвітки

**A. Fixed-total + варіація duty_high (як Fizik).** Залиш заводський `total`,
міняй лише `FD28/FD29`. Fizik використовує 4 рівні (значення duty_high lsb):

| Fizik case | FD28h (lsb) | FD29h (msb) | Семантика |
|-----------|-------------|-------------|-----------|
| 0 | `0x14` | `0x00` | найтемніше |
| 1 | `0xA4` | `0x00` | темно |
| 2 | `0x46` | `0x05` | середньо (`0x0546`) |
| 3/def | `0x01` | `0x00` | найяскравіше (фактично майже off-HIGH → max?) |

> Зверни увагу: у Fizik «найяскравіше» = найменший duty (`0x0001`), а «темно» = більший.
> Це означає, що на його платі **підсвітка інвертована** (HIGH = вимкнено), або duty рахується
> як LOW-фаза. **Перевір полярність на бенчі**, перш ніж лінеаризувати шкалу 0–100%.

**B. Spec 50%-економія.** User-спека (розділ 8) дає драматичну економію 290→65 мА:
```c
amt_w(0x58, 0x20, 0x00); amt_w(0x58, 0x21, 0x10);  // total = 0x1000
amt_w(0x58, 0x28, 0x00); amt_w(0x58, 0x29, 0x08);  // high  = 0x0800 (50%)
amt_w(0x58, 0x1F, 0x01);                            // PWM0 enable
```
Застереження спеки: «не працює при надто малих total (напр. 0x0100)»; підсвітка регулюється,
лише **якщо пін PWM0 вже в PWM-режимі**. ENGELS показує, що OEM сам ставить пін у PWM (`FD42h |= 3`),
тож на KOZHAN-платі з тим самим firmware дімінг має бути доступним.

### 1.4 Рекомендований API меню (підсвітка/яскравість)

```c
// 0..100% яскравості підсвітки; total фіксований; полярність уточнити на бенчі
void screen_set_backlight(uint8_t pct) {           // dev 0x58
    if (pct > 100) pct = 100;
    uint16_t total = 0x1000;                        // безпечний total (≥0x0100)
    uint16_t high  = (uint32_t)total * pct / 100;   // лінійно
    amt_w(0x58, 0x20, total & 0xFF); amt_w(0x58, 0x21, total >> 8);
    amt_w(0x58, 0x28, high  & 0xFF); amt_w(0x58, 0x29, high  >> 8);
    amt_w(0x58, 0x1F, 0x01);                        // enable PWM0
}
```
**Діапазон:** total `0x0200..0xFFFF` (нижче ~`0x0100` не світить), high `0..total`.

---

## 2. Вибір CVBS-входу — FE-банк / I²C `0x59` (ПОВНІСТЮ підтверджено ENGELS)

### 2.1 Послідовність (`apply_av_input_r7`, `ENGELS:22390`)

Порядок **строгий** (read-modify-write кожного регістра):
```
1. FED7h: clear bit3,4         (a &= 0xE7)         ; вимкнути video
2. FED8h: виставити bit6,7     (input_select_0)
3. FED7h: set bit3,4           (a |= 0x18)         ; увімкнути video назад
4. FEDCh: виставити bit4,5     (input_select_1)
```

### 2.2 Таблиця значень — ДВІ РЕВІЗІЇ ПРОШИВКИ (`ENGELS:22395–22404`)

```
;Input    FED7h.bit3-4  FED8h.bit6-7  FEDCh.bit4-5
;--- СТАРА прошивка (<11sep2017):
;AV1      0-then-3      2             0   ;<-- CVBS1
;AV2      0-then-3      2             3   ;<-- ловить дещо (смуги якщо CVBS3 має сигнал)
;AV3      0-then-3      0             2   ;<-- CVBS3
;Invalid  0-then-3      3             3
;--- НОВА прошивка (>=ver11sep2017):
;AV1      0-then-3      0 !           2 ! ;<--
;AV2      0-then-3      0 !           3
;AV3      0-then-3      2 !           0 ! ;<--
;Invalid  0-then-3      3             3
```

> **⚠ КРИТИЧНИЙ КОНФЛІКТ із user-спекою.** Спека (розділ 3) дає лише «новішу» таблицю,
> але **переставлені AV1↔AV3 vs ENGELS-new**: спека пише AV1=`FED8 bit6-7=2, FEDC bit4-5=0`,
> а ENGELS-new пише AV1=`FED8=0, FEDC=2` (тобто спекові «AV1» збігаються з ENGELS-**old**-AV1,
> а спекові цифри для нової — навпаки). Практичний висновок: **точні bit6-7/bit4-5 ЗАЛЕЖАТЬ
> від ревізії прошивки на твоїй платі — обов'язково ВИЗНАЧИТИ на бенчі** (перебрати 0/2/3 для
> bit-полів і подивитись, який вхід ловиться). Незмінне у всіх ревізіях: **FED7 завжди 0→3
> (bit3,4), і завжди в порядку D7→D8→D7→DC**.

### 2.3 Точні маски бітів (з asm, для надійного RMW)

| Крок | Регістр | Операція asm | Маска |
|------|---------|-------------|-------|
| вимкнути video | `FED7` | `and 0xE7` | clear bit3,4 |
| увімкнути video | `FED7` | `or 0x18` | set bit3,4 |
| input_0 bit6,7=00 | `FED8` | `and 0x3F` | (new-fw AV1/AV2) |
| input_0 bit7=1,bit6=0 | `FED8` | `or 0x80; and 0xBF` | (old-fw AV1/AV2) |
| input_0 bit6,7=11 | `FED8` | `or 0xC0` | invalid |
| input_1 bit5=1 | `FEDC` | `or 0x20` | |
| input_1 bit5=0 | `FEDC` | `and 0xDF` | |
| input_1 bit4=0 | `FEDC` | `and 0xEF` | (фінальний крок у більшості гілок) |
| input_1 bit4=1 | `FEDC` | `or 0x10` | (invalid) |

### 2.4 Псевдокод під меню (новіша прошивка, ПЕРЕВІРИТИ на бенчі)

```c
// in = 1=AV1(CVBS1), 3=AV3(CVBS3). RMW зберігає інші біти.
void screen_select_input(uint8_t in) {              // dev 0x59
    uint8_t v;
    v = amt_r(0x59,0xD7); amt_w(0x59,0xD7, v & 0xE7);   // FED7 clear bit3,4
    v = amt_r(0x59,0xD8);
    if (in==1) v &= 0x3F;            // AV1 new-fw: bit6,7=0
    else       { v |= 0x80; v &= 0xBF; } // AV3 new-fw: bit7=1,bit6=0
    amt_w(0x59,0xD8, v);
    v = amt_r(0x59,0xD7); amt_w(0x59,0xD7, v | 0x18);   // FED7 set bit3,4
    v = amt_r(0x59,0xDC);
    if (in==1) v |= 0x20; else v &= 0xDF;              // bit5
    v &= 0xEF;                                          // bit4=0
    amt_w(0x59,0xDC, v);
}
```
> Якщо `amt_r` ненадійний по I²C (XRAM-порти можуть не читатись як очікувано) —
> тримай тіньові копії FED7/FED8/FEDC у RAM ESP32 і пиши абсолютні значення.

---

## 3. Детект наявності сигналу — читання, I²C `0x59` (FE-банк)

### 3.1 Регістри (`ENGELS:725, 727, 729`)

| Регістр | I²C (`0x59`) | Ім'я ENGELS | Біти |
|---------|-------------|-------------|------|
| `FE26h` | `0x26` | `IO_AV_stat_detect_0` | **bit1** = video DETECT (часто вживаний прошивкою); NOT R/W |
| `FE28h` | `0x28` | `IO_AV_stat_framerate_flag` | **bit2** = PAL/NTSC (50/60 Гц); NOT R/W |
| `FE2Ah` | `0x2A` | `IO_AV_stat_signal_detect` | **bit4=HaveCVBS1, bit6=HaveCVBS3, bit0-3=ErrorFlags**; NOT R/W |
| `FED0h` | `0xD0` | `IO_AV_stat_detect_2` | додатковий статус; NOT R/W |

> **Поправка до спеки:** спека пише `FE26h bit1,2` і `FE2Ah bit4=сигнал на вибраному,
> bit6=на невибраному». ENGELS уточнює конкретніше: `FE2Ah bit4 = HaveCVBS1`,
> `bit6 = HaveCVBS3` (фізичні входи, не «вибраний/невибраний»), `bit0-3 = коди помилок`.
> `FE26h bit1` — головний DETECT-прапор, який сама прошивка тестує в IRQ.

### 3.2 Як firmware це читає (`ENGELS:1344` — IRQ-обробник)

```asm
0072 90 FE 26  mov dptr,IO_AV_stat_detect_0   ; \
0075 E0        movx a,[dptr]                  ;
0076 54 02     and  a,02h     ;bit1           ;  тест video DETECT
0078 C3        clr c ; 0079 13 rcr a          ;
007A 70 03     jnz a,@@dont_enter_coarse      ; / якщо немає сигналу → COARSE mode
```
і `FE2Ah` для артефактів/станів (`ENGELS:0079–0098`): читає bit0-3, XOR зі старим
значенням `xram_old_AV_stat_signal_detect`, реагує на зміну.

### 3.3 API меню (статус сигналу)
```c
bool screen_have_signal(void)  { return amt_r(0x59,0x26) & 0x02; }  // FE26 bit1
bool screen_have_cvbs1(void)   { return amt_r(0x59,0x2A) & 0x10; }  // FE2A bit4
bool screen_have_cvbs3(void)   { return amt_r(0x59,0x2A) & 0x40; }  // FE2A bit6
bool screen_is_ntsc(void)      { return amt_r(0x59,0x28) & 0x04; }  // FE28 bit2
```
> Це апаратний детект **для відображення**, НЕ для детекції слабких дронів (там — ADC1-тракт ESP32).

---

## 4. Яскравість / контраст / насиченість / tint — FF-банк / I²C `0x5A`

### 4.1 Регістри (`ENGELS:926–929`, ПІДТВЕРДЖЕНО `apply_settings_to_IO_ports`)

| Регістр | I²C (`0x5A`) | Ім'я ENGELS | Параметр | Medium (спека) |
|---------|-------------|-------------|----------|----------------|
| `FFD3h` | `0xD3` | `IO_LCD_basic_contrast` | контраст | `0x7E` |
| `FFD4h` | `0xD4` | `IO_LCD_basic_brightness` | video-яскравість | `0x8E` |
| `FFD5h` | `0xD5` | `IO_LCD_basic_tint` | tint (лише NTSC) | `0x00` |
| `FFD6h` | `0xD6` | `IO_LCD_basic_saturation` | насиченість | `0x38` |

OEM пише саме сюди (`ENGELS:15851, 15873, 15895, 15903`):
```asm
7646 90 FF D4  mov dptr,IO_LCD_basic_brightness  ; <- brightness
7671 90 FF D3  mov dptr,IO_LCD_basic_contrast     ; <- contrast
769C 90 FF D6  mov dptr,IO_LCD_basic_saturation   ; <- saturation
76A9 90 FF D5  mov dptr,IO_LCD_basic_tint          ; <- tint
```

### 4.2 Діапазон значень — модель MEDIUM ± 0x28 (`ENGELS:13504 xlat_...`)

OEM конвертує USER 0..100 → HEX навколо MEDIUM:
```
MIN = MEDIUM - 0x28  (clamp 0x00; для saturation MIN завжди 0x00)
MAX = MEDIUM + 0x28  (clamp 0xFF)
USER 50 (центр) → MEDIUM
```
Отже **безпечні діапазони для меню** (повний registr-діапазон 0x00..0xFF дозволений,
але «розумний» = MEDIUM±0x28):

| Параметр | Min (0%) | Med (50%) | Max (100%) | Регістр |
|----------|---------|-----------|-----------|---------|
| Контраст | `0x56` | `0x7E` | `0xA6` | `FFD3h` |
| Яскравість (video) | `0x66` | `0x8E` | `0xB6` | `FFD4h` |
| Насиченість | `0x10` | `0x38` | `0x60` | `FFD6h` |
| Tint (NTSC) | `0x00` | `0x00` | — | `FFD5h` (див. BUG) |

> Можна писати й абсолютні 0x00..0xFF — чіп прийме, але крайні значення дають
> переекспонований/чорний кадр. Модель MEDIUM±0x28 — те, що показує заводське меню.

### 4.3 ⚠ BUG tint (`ENGELS` + спека розділ 10)
`FFD5h bit7` ламає декодування кольору PAL, хоча tint має діяти лише на NTSC.
**Не чіпати bit7 tint.** На PAL-платах tint краще лишити `0x00` і не показувати у меню.

### 4.4 API меню
```c
void screen_set_contrast(uint8_t pct)   { amt_w(0x5A,0xD3, map(pct,0x56,0xA6)); }
void screen_set_brightness(uint8_t pct) { amt_w(0x5A,0xD4, map(pct,0x66,0xB6)); }
void screen_set_saturation(uint8_t pct) { amt_w(0x5A,0xD6, map(pct,0x10,0x60)); }
// tint: лише NTSC, bit7 НЕ ставити; зазвичай 0x00
```

---

## 5. No-signal режим (заставка коли немає відео)

Прошивка має кілька джерел «no-signal» картинки. Для меню ESP32 корисні два керовані:

### 5.1 SNOW vs Backdrop (`ENGELS:835, 840–845`)

| Регістр | I²C | Біт | Ефект |
|---------|-----|-----|-------|
| `FED7h` | `0x59`/`0xD7` | bit0,1,6,7 | **disable SNOW** (freeze backdrop/snow-color; обережно — може заморозити OSD-scanline) |
| `FEDCh` | `0x59`/`0xDC` | bit4 | toggle = Blue backdrop (без снігу); untoggle = коротко no-signal(зі снігом)→норм. картинка |
| `FEDCh` | `0x59`/`0xDC` | bit5 | NoSignal (порожній екран) |
| `FEDCh` | `0x59`/`0xDC` | bit6 | Blue backdrop (без снігу, кілька випадкових пікселів) |
| `FD19h` | `0x58`/`0x19` | bit0 | NoSignal (з невеликим vsync-roll); bit7 NoSignal |
| `FE31h` | `0x59`/`0x31` | bit7 | NoSignal |

> **Рекомендація для меню:** показати користувачу опцію «фон при відсутності сигналу»:
> `Синій екран` (FEDCh bit6=1) або `Сніг` (FEDCh bit4=0, bit6=0). Логіку триггерити
> по `screen_have_signal()` (розділ 3). НЕ чіпати FED7 bit0/1/6/7 у нормальному режимі —
> вони можуть заморозити OSD-рядок (саме той, де твій статус-текст).

### 5.2 Чутливість декодера (опц., `ENGELS:723, 830`)
`FE15h` (`0x59`/`0x15`) `IO_AV_ctrl_sensitivity_0`: `00h`=max, `05h`=med, `09h`=low.
Впливає на те, з якого рівня сигналу чіп вважає, що «є відео». Для слабких/шумних
CVBS можна підняти чутливість (`00h`), але це **не** заміняє ADC-детекцію на ESP32.

---

## 6. OSD-вікно (контекст для «увімкнути/вимкнути екранне меню») — `0x5B`

| Регістр | I²C (`0x5B`) | Ім'я ENGELS | Призначення |
|---------|-------------|-------------|-------------|
| `FB05h` | `0x05` | `IO_OSD_window_enable_bits` | bit0..4 = Window0..4 on/off; **bit6 = hide windows (TEXT on/off)**; bit7 = bitmap on/off |
| `FB06h` | `0x06` | `IO_OSD_misc_transp_enable` | напівпрозорість (upper2bit) |
| `FB0Ch` | `0x0C` | `IO_OSD_bright_transp_level` | upper3bit=яскравість OSD, lower3bit=прозорість |

Fizik вмикає OSD: `writeCommand(0x5B,0x05,0x1F)` (усі 5 вікон), вимикає: `0x5B,0x05,0x00`.
**bit6** дає швидке «приховати все меню без втрати даних» — зручно для toggle меню з ESP32.

---

## 7. ⛔ DANGER-регістри — НЕ ЧІПАТИ по I²C

Звірено user-спека (розділ 12) ↔ ENGELS (іменовані `DANGER`). Тримайся ЛИШЕ банків
`0x59` (AV/вхід/детект), `0x5A` (FFxx яскравість), `0x5B` (OSD), і **PWM-частини**
`0x58` (`FD1Fh`, `FD20/21`, `FD28/29`, `FD42h`). Усе інше в `0x58`/FDxx — небезпечне.

| Регістр | I²C | Небезпека | Джерело |
|---------|-----|-----------|---------|
| `FDD0h` | `0x58`/`0xD0` | SPI transfer mode — flash-операції, зламає прошивку | spec+`ENGELS:675` |
| `FDDEh` | `0x58`/`0xDE` | SPI kick/stop/reset (write 80h); bit6 hang CPU | spec+`ENGELS:697` |
| `FDE0h` | `0x58`/`0xE0` | crashes CPU (unused, DANGER!) | spec+`ENGELS:699` |
| `FDE5h–FDE7h` | `0x58` | unused/DANGER | `ENGELS:702` |
| `FDF1h` | `0x58`/`0xF1` | upper-32k code base; bit0-3 DANGER | `ENGELS:706` |
| `FD32h/FD33h` | `0x58`/`0x32,0x33` | SPI-flash піни (`IO_PIN_P1x_spi_flash`) | spec+`ENGELS:615–616` |
| `FDB0h` | `0x58`/`0xB0` | ADC ctrl: bit1-2 DANGER; bit0 NOT R/W | `ENGELS:647` |
| `FDB2h,FDB4h,FDB5h` | `0x58` | ADC config: окремі DANGER-біти | `ENGELS:649–652` |
| `FD12h` | `0x58`/`0x12` | hang ADC? | `ENGELS:581` |
| `FD17h` | `0x58`/`0x17` | ADC clk divider; bit7 DANGER | `ENGELS:586` |
| `FD11h` | `0x58`/`0x11` | bit0 hangs CPU; bit1-2 OSD char error+hang | `ENGELS:580` |
| `FD18h` bit7 | `0x58`/`0x18` | ForceMaxBacklight → вимикає dimming (не «крах», але псує економію) | `ENGELS:587` |
| `FD41h`/`FD40h` | `0x58` | KillTftUpdating / StopDotClk — чорний/завислий екран | `ENGELS:629–630` |
| `SFR C6h`, `SFR D8h`, `PCON` | — | CPU-SFR: reboot/hang/halt | spec — **і так недоступні по I²C** |

> **Особлива увага:** `0x58` (FDxx) містить і безпечний PWM, і смертельний SPI-flash/ADC.
> Пиши в FDxx **лише** конкретні регістри `0x1F/0x20/0x21/0x28/0x29/0x42`. Жодних
> «init-петель усього банку».

---

## 8. Перехресні конфлікти між джерелами (ПІДСУМОК)

1. **PWM clock 26 vs 27 МГц.** Спека: 26 МГц. ENGELS (першоджерело): **27 МГц**
   (`019BFCC0h`). Для відносного duty% — неважливо; для абсолютної частоти бери 27.
2. **Таблиця CVBS-входів AV1/AV3 переставлена** між спекою (розділ 3) і ENGELS-new-fw.
   Незмінні: порядок D7→D8→D7→DC і FED7 0→3. Точні bit6-7/bit4-5 — **визначити на бенчі**
   за ревізією прошивки.
3. **«Яскравість» Fizik = PWM0-підсвітка (FD28/29), не FFD4.** Узгоджує спеку (розд.8)
   з робочим кодом; доводить керованість PWM по I²C. Полярність duty в Fizik виглядає
   **інвертованою** (max яскравість = малий duty) — перевірити.
4. **FE2Ah біти.** Спека: bit4=вибраний/bit6=невибраний. ENGELS точніше:
   **bit4=HaveCVBS1, bit6=HaveCVBS3** (фізичні), bit0-3=error. Брати ENGELS.
5. **Читання XRAM-портів по I²C.** Статус-регістри (`FE26/28/2A`) позначені `NOT R/W`
   у сенсі «не записувані», але читаються прошивкою через `movx`. Чи віддає їх I²C-slave
   назад — **перевірити на бенчі**; якщо ні, детект сигналу по I²C недоступний (тоді
   no-signal-логіку вести по ADC1 ESP32).

---

## 9. Підсумкова таблиця «меню налаштувань екрана» для ESP32

| Пункт меню | Регістр(и) | I²C dev | Безпечний діапазон | Джерело |
|-----------|-----------|---------|--------------------|---------|
| **Підсвітка / яскравість екрана** | `FD28/FD29` (duty), `FD20/FD21` (total), `FD1F`=01 | `0x58` | total ≥0x0200; duty 0..total; 50%=high `0x0800`/total `0x1000` | Fizik+спека+`ENGELS:587–605` |
| **Video-яскравість** | `FFD4h` | `0x5A` | `0x66..0xB6` (med `0x8E`) | спека+`ENGELS:927` |
| **Контраст** | `FFD3h` | `0x5A` | `0x56..0xA6` (med `0x7E`) | спека+`ENGELS:926` |
| **Насиченість** | `FFD6h` | `0x5A` | `0x10..0x60` (med `0x38`) | спека+`ENGELS:929` |
| **Tint (лише NTSC)** | `FFD5h` | `0x5A` | `0x00`; **bit7 НЕ ставити** | спека+`ENGELS:928` |
| **Вибір входу** | `FED7→FED8→FED7→FEDC` | `0x59` | див. розд.2 (бенч!) | спека+`ENGELS:22390` |
| **No-signal фон** | `FEDCh` bit4/6 (Blue/Snow), bit5 (off) | `0x59` | RMW окремих біт | `ENGELS:840–845` |
| **Статус сигналу (R)** | `FE26`b1, `FE2A`b4/b6, `FE28`b2 | `0x59` | read-only | спека+`ENGELS:725–729` |
| **OSD меню on/off** | `FB05h` (bit0-4 / bit6 hide) | `0x5B` | `0x1F`=усі вікна, `0x00`=off | Fizik+`ENGELS:318` |
| **Чутливість декодера (опц.)** | `FE15h` | `0x59` | `00h`/`05h`/`09h` | `ENGELS:723` |

---

## 10. Що ОБОВ'ЯЗКОВО перевірити на бенчі

1. **Полярність PWM-підсвітки** (high=яскравіше чи темніше?) — Fizik натякає на інверсію.
2. **Точні bit-значення CVBS-входів** на ревізії прошивки KOZHAN (перебрати FED8 bit6-7 ∈ {0,2,3}, FEDC bit4-5).
3. **Чи читаються FE26/28/2A по I²C** (детект сигналу ззовні).
4. **Чи прошивка AMT630A не перезаписує** твої FFD3-D6 / PWM при власних подіях
   (у неї є `apply_settings_to_IO_ports`, яка викликається з меню/hotkey — можливий конфлікт).
5. **27 vs 26 МГц** для абсолютної частоти підсвітки (якщо критично для EMI/мерехтіння).
