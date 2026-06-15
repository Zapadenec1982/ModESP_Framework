# 07 — Fizik Reference: доведена на залізі правда про AMT630A (I²C, Шлях A)

> **Статус:** наземна істина. Усе нижче взято з робочої прошивки детектора Fizik
> `DETECTOR_FPV_1.36.9.5` (PlatformIO, ESP32 `esp32dev`, Arduino framework) і **перехресно
> звірено** з OEM-дизасемблером `ENGELS.A22` (Martin Korth / no$x51, абсолютні адреси +
> імена `IO_*`-регістрів). Це те, що **точно працює** на платі з AMT630A.
>
> **Джерела:**
> - `AMT630.h` (драйвер OSD/TFT по I²C) — повний розбір нижче
> - `display_1306.h` (17 KB, окремий SSD1306-OLED + усе меню/UI) — НЕ той самий дисплей
> - `main.cpp`, `config.h`, `PCF8574.h`, `key.h`, `platformio.ini`
> - `ENGELS.A22` — OEM-дизасемблер для анотації кожного регістра

---

## 0. КЛЮЧОВЕ ЗАСТЕРЕЖЕННЯ: два РІЗНІ дисплеї в одній прошивці

У детекторі Fizik **два незалежні дисплеї**, і їх легко сплутати:

| Об'єкт C++ | Файл | Залізо | Шина | Призначення |
|------------|------|--------|------|-------------|
| `AMT630 display_TFT` | `AMT630.h` | **AMT630A** video-SoC + LCD 4.3″ | I²C `0x58–0x5F` | показує **живе FPV-відео** з 3 CVBS-входів + OSD-оверлей |
| `Display D1306(50)` | `display_1306.h` | **SSD1306/SH1106 OLED 128×64** | I²C `0x3C` | усе **меню/статус/RSSI-бари/батарея** детектора |

**display_1306.h — це НЕ драйвер AMT630A.** Це Adafruit-GFX обгортка над OLED-екраном
(монохром 128×64). Уся «багаторядковість, меню, іконки, розкладка статусу», на яку
сподівались — реалізована для **OLED**, а не для AMT630A. Для AMT630A в Fizik є лише
`AMT630.h` з мінімальним OSD (`osdPrint`, цифри+латиниця 0x01–0x24). Це важливий факт:
**доведений OSD-функціонал AMT630A у Fizik — мінімальний** (вмикання відео + яскравість +
один рядок тексту). Решта UI зроблена на окремому OLED.

---

## 1. Налаштування I²C-шини (доведене)

**Це найважливіший і трохи несподіваний факт:** Fizik **ніде не задає піни чи частоту I²C**.

| Параметр | Значення | Звідки |
|----------|----------|--------|
| Ініціалізація шини | `Wire.begin()` **без аргументів** | `PCF8574.h:9` — **єдиний** виклик `Wire.begin()` у всьому проєкті |
| SDA / SCL | **дефолтні ESP32: GPIO21 (SDA) / GPIO22 (SCL)** | наслідок `Wire.begin()` без пінів |
| Частота | **дефолтна 100 кГц** | немає жодного `Wire.setClock()` у проєкті (grep підтверджено) |
| Тайм-аут | дефолтний | немає `Wire.setTimeout()` |

Grep по всьому дереву (`Wire.(begin|setClock|setTimeout)`):
- `PCF8574.h:9` → `Wire.begin();` (без аргументів)
- решта — лише `Wire.beginTransmission(...)` (транзакції, не конфіг шини)

**Висновок для ModESP:** AMT630A, OLED SSD1306 (0x3C), PCF8574-клавіатура (0x20–0x27)
і TA8804-тюнер — **усі висять на ОДНІЙ I²C-шині GPIO21/22 @ 100 кГц**. AMT630A надійно
працює на 100 кГц зі стандартного ESP32-I²C. Підвищення швидкості Fizik не перевіряв.

### Тайминги/затримки запису

Кожен запис регістра AMT630A завершується **`delay(10)`** (10 мс!):

```cpp
// AMT630.h:15
static void writeCommand(uint8_t address, uint8_t reg, uint8_t data) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
  delay(10);                 // <-- 10 мс після КОЖНОГО регістра
}
```

- `initDisplay()` — 24 записи → ~240 мс + фінальний `delay(200)` ≈ **0.44 с**.
- `onDisplay()` — ~70 записів → **~0.9 с** блокуючого часу (!).

Це доведено-працює, але дуже консервативно. `display_1306.h` має ідентичний приватний
`writeCommand` з тим самим `delay(10)` (рудимент — там він не використовується активно).

---

## 2. Карта device-адрес у Fizik (звірка з OEM-банками)

`AMT630.h` визначає три іменовані адреси + використовує ще три «голі»:

```cpp
#define TFT_LCD_REG 0x58   // насправді FDxx = Global/PWM/PLL  (НЕ LCD!)
#define TFT_AV_REG  0x59   // FExx = AV-декодер / вибір CVBS-входу
#define TFT_OSD_REG 0x5B   // FBxx = OSD-рушій
// також у коді напряму: 0x5A (FFxx = Video/яскравість), 0x5F (init-unlock)
```

| Fizik 7-біт | OEM MCU-банк | OEM-підсистема (`ENGELS.A22`) | Fizik-назва | Вірно? |
|-------------|--------------|-------------------------------|-------------|--------|
| `0x58` | `FDxx` | Global, **PWM-підсвітка**, PLL, ADC, SPI-flash | `TFT_LCD_REG` | **НІ** — це не LCD |
| `0x59` | `FExx` | AV-декодер, вибір CVBS, детект сигналу | `TFT_AV_REG` | **так** |
| `0x5A` | `FFxx` | Video-process, GAMMA, **контраст/яскравість/насиченість**, snow | (без define) | так |
| `0x5B` | `FBxx` | **OSD** (вікна, BGMAP, FONT, палітра) | `TFT_OSD_REG` | так |
| `0x5F` | — | **немає у XRAM-мапі** — vendor init/unlock | (без define) | спец. |

> **КОНФЛІКТ #1 (підтверджено й розв'язано):** канонічна спека (`AMT630A_user_spec.md`,
> розд. 2) вже зазначала, що Fizik **помилково** назвав `0x58` як «LCD». Дизасемблер це
> **підтверджує на 100%**: регістри, які Fizik пише в `0x58` (`0x28/0x29/0x42/0x11/0x12/0x13`),
> у OEM — це `FD28/FD29` (**PWM0 backlight duty**), `FD42` (**PIN P35/P36 PWM, backlight**),
> `FD11/FD12/FD13` (**PLL / screen on-off**). Тобто Fizik-«яскравість» = **PWM-підсвітка**,
> а не LCD-контраст. Реальний LCD-контраст/яскравість живе в `0x5A`/`FFD3/FFD4`.

---

## 3. ПОВНА init-послідовність `initDisplay()` (регістр-за-регістром)

Викликається двічі: у `D1306.begin()`-гілці немає, але прямо в `setup()` (`main.cpp:123`)
та з `onDisplay()`. Це «холодна» ініціалізація поверх уже-працюючої прошивки AMT630A.

| # | Fizik dev,reg,val | OEM-адреса | OEM-назва / що робить |
|---|-------------------|------------|------------------------|
| 1 | `0x5F, 0xAF, 0x00` | — | **vendor unlock-handshake** (поза XRAM-мапою) |
| 2 | `0x5F, 0xA1, 0x55` | — | magic `0x55` (той самий патерн, що `xram_sett_testburn 55h/AAh`) |
| 3 | `0x5F, 0xA2, 0xAA` | — | magic `0xAA` |
| 4 | `0x5F, 0xA3, 0x03` | — | unlock-параметр |
| 5 | `0x5F, 0xA4, 0x50` | — | unlock-параметр |
| 6 | `0x5F, 0xA5, 0x00` | — | unlock-параметр |
| 7 | `0x5F, 0xA6, 0x53` | — | unlock-параметр |
| 8 | `0x5F, 0xAF, 0x11` | — | завершення unlock-фази (AF: 00→11) |
| 9 | `0x5F, 0xC6, 0x42` | `FDC6` | **memory_system** (= DANGER `SFR C6h` через I²C!) — set 0x42 |
| 10 | `0x5F, 0xC6, 0x00` | `FDC6` | memory_system → 0x00 (скидання) |
| 11 | `0x58, 0x42, 0x03` | `FD42` | **IO_PIN_P35_P36_pwm** — bit0-2 = ScreenBlack(backlight). 0x03 |
| 12 | `0x58, 0x1F, 0x03` | `FD1F` | **IO_PWM_enable_flags** — bit0-3 = PWM0..3 enable. 0x03 = PWM0+PWM1 on |
| 13 | `0x58, 0x28, 0x00` | `FD28` | **IO_PWM0_duty_high_lsb** = 0 (підсвітка вимкнена) |
| 14 | `0x58, 0x29, 0x00` | `FD29` | **IO_PWM0_duty_high_msb** = 0 |
| 15 | `0x58, 0x11, 0x1F` | `FD11` | **IO_PLL_11h** (bit4:OSD_BG_ONLY, bit5:scanlinefreeze) = 0x1F |
| 16 | `0x58, 0x12, 0x38` | `FD12` | **IO_PLL_12h** (DANGER: hang ADC?) = 0x38 |
| 17 | `0x58, 0x13, 0x00` | `FD13` | **IO_PLL_13h** — screen on/off (00=off, FF=on). 0x00 = екран **OFF** |
| 18 | `0x59, 0xDC, 0x00` | `FEDC` | **IO_AV_input_select_reg_1** = 0 |
| 19 | `0x5A, 0xD2, 0x54` | `FFD2` | **IO_LCD_forced_blank_color** = **0x54 = форс-чорний** (відео приховано) |
| 20 | `0x59, 0xD7, 0xFC` | `FED7` | **IO_AV_video_on_off** — bit3-4=0 (відео off). 0xFC |
| 21 | `0x5A, 0xB0, 0x00` | `FFB0` | **IO_LCD_snow_enable_and_misc** = 0x00 (snow off) |
| 22 | `0x5B, 0x05, 0x1F` | `FB05` | **IO_OSD_window_enable_bits** = 0x1F = **усі 5 вікон ON** |
| 23 | `0x5F, 0xBE, 0x55` | — | vendor magic `0x55` |
| 24 | `0x5F, 0xBA, 0x00` | — | vendor (BA: у OEM «unused, value 00h») |
| 25 | `0x5F, 0xBE, 0xAA` | — | vendor magic `0xAA` (фінальний commit unlock) |
| — | `delay(200)` | | стабілізація |

**Сенс initDisplay:** unlock-handshake (`0x5F`) → memory_system poke → підсвітка **OFF**
(PWM duty=0) → PLL-конфіг → screen **OFF** (FD13=0, FFD2=форс-чорний) → AV-вхід скинуто →
усі OSD-вікна увімкнено → фінальний vendor-commit. Тобто **холодний стан = чорний екран,
відео off, OSD-вікна готові, підсвітка off**. Це «idle/standby»-стан.

> **КОНФЛІКТ #2:** Fizik пише `0x5F, 0xC6` (= `FDC6` = memory_system). Спека (розд. 12)
> класифікує `SFR C6h` як **DANGER (CPU-only, reboot/hang)** і каже «недоступний по I²C».
> Але Fizik **успішно пише його по I²C через device 0x5F** (а не 0x58/FDxx). Це означає,
> що `0x5F` — окремий vendor-канал, де C6 поводиться інакше (init-magic), і він **доведено
> безпечний у цій конкретній unlock-послідовності**. Не екстраполюй на довільний запис C6.

---

## 4. ПОВНА `onDisplay()` — увімкнення відображення відео + OSD

`onDisplay()` (`AMT630.h:52`) спрацьовує **один раз** при `_state_video == false` (латч).
Спершу повторно викликає `initDisplay()`, потім ~60 записів:

**4.1. Screen ON (скасування OFF з init):**
| Fizik | OEM | Дія |
|-------|-----|-----|
| `0x58,0x11,0xFF` | FD11 | PLL_11 = 0xFF |
| `0x58,0x12,0xFF` | FD12 | PLL_12 = 0xFF |
| `0x58,0x13,0xFF` | FD13 | **screen ON** (00→FF) |

**4.2. AV-декодер → увімкнути CVBS-вхід (банк 0x59 / FExx):**
| Fizik | OEM | Дія |
|-------|-----|-----|
| `0x59,0x07,0x01` | FE07 | AV-config |
| `0x59,0x11,0x01` | FE11 | IO_AV (bit2,4..7 впливають на color-roll) |
| `0x59,0xDC,0x20` | FEDC | input_select_1 = 0x20 (вибір входу) |
| `0x5A,0xD2,0x4F` | FFD2 | **forced_blank = 0x4F = ПОКАЗАТИ AV-відео** (знято форс-чорний!) |
| `0x59,0xCD,0x00` | FECD | AV |
| `0x59,0x01,0x06` | FE01 | IO_AV_ctrl (bit1,bit4) |
| `0x59,0x04,0x80` | FE04 | AV-config (OEM fixed=0x30, Fizik=0x80) |
| `0x59,0x05,0x30` | FE05 | AV-config (OEM fixed=0x40, Fizik=0x30) |
| `0x59,0x54,0x40` | FE54 | IO_AV_ctrl_whatever_2 |
| `0x59, 0x8A..0xE3` | FE8A..FEE3 | ~25 регістрів тонкого тюнінгу декодера (8A,8B,A4,A7,A8,A9,AB,AC,AD,AF,B0,B1,B4,CB,D7,D8,DD,DE,E0,E1,E3) |

**4.3. Video-process / GAMMA (банк 0x5A / FFxx):**
| Fizik | OEM | Дія |
|-------|-----|-----|
| `0x5A,0xB0,0xA3` | FFB0 | snow_enable_and_misc = 0xA3 |
| `0x5A,0xB2,0x1C` ×3 (B2,B3,B4) | FFB2-B4 | LCD-config (OEM fixed 0x20→Fizik 0x1C) |
| `0x5A,0xD3,0x80` | FFD3 | **контраст = 0x80** |
| `0x5A,0xD4,0x80` | FFD4 | **яскравість = 0x80** |
| `0x5A,0xD6,0x56` | FFD6 | **насиченість = 0x56** |
| `0x5A,0xDA,0x6C` | FFDA | backdrop/snow level (OEM fixed 0x6C ✓) |
| `0x5A, 0xF0..0xFA` | FFF0..FFFA | GAMMA-таблиця (F0=0x02,F1=0xF1,F2=0x13,F3=0xDB,F4=0xCD,F5=0x19,F6=0x1B,F7=0xEA,F8=0x0F,F9=0x31,FA=0x19) |

**4.4. OSD ON + фінал:**
| Fizik | OEM | Дія |
|-------|-----|-----|
| `0x5B,0x05,0x1F` | FB05 | OSD: усі 5 вікон ON (повторно) |
| `0x58,0x19,0x08` | FD19 | IO_PLL_19h (bit0:NoSignal-control) = 0x08 |
| `0x58,0x42,0x03` | FD42 | PIN P35/P36 PWM = 0x03 |
| `0x58,0x1F,0x03` | FD1F | PWM enable = PWM0+1 |
| `changeBrightness(EEPROM.read(0x3C))` | — | **застосувати яскравість-пресет з EEPROM addr 60** |

> Зверни увагу: `0x5A,0xD3/D4/D6` (FFD3/D4/D6 = контраст/яскравість/насиченість) **встановлюються
> жорстко в `0x80/0x80/0x56`** і **не міняються** пресетами яскравості. Реальна «яскравість» у
> Fizik-меню керує **PWM-підсвіткою** (FD28/29), а не FFD4. Це збігається з KONFLIKT #1.

---

## 5. Пресети яскравості `changeBrightness(int)` — доведені значення

Працює **лише** коли `_state_video == true`. Пише у `TFT_LCD_REG` (= `0x58`/FDxx),
регістри `0x28/0x29` = **FD28/FD29 = PWM0 backlight duty (high lsb/msb)**:

| Пресет | `0x28`(FD28 lsb) | `0x29`(FD29 msb) | duty(high) 16-біт | Сенс |
|--------|------------------|------------------|-------------------|------|
| 0 | `0x14` | `0x00` | `0x0014` | найтьмяніше (дуже малий PWM-high) |
| 1 | `0xA4` | `0x00` | `0x00A4` | темно |
| 2 | `0x46` | `0x05` | `0x0546` | яскраво |
| 3 (default) | `0x01` | `0x00` | `0x0001` | (парадокс: майже 0 — «Very Low») |

```cpp
// AMT630.h:138 — повністю
void changeBrightness(int newBrightness) {
  _brightness = newBrightness;
  if (_state_video) {
    switch (_brightness) {
      case 0: writeCommand(0x58,0x28,0x14); writeCommand(0x58,0x29,0x00); break;
      case 1: writeCommand(0x58,0x28,0xA4); writeCommand(0x58,0x29,0x00); break;
      case 2: writeCommand(0x58,0x28,0x46); writeCommand(0x58,0x29,0x05); break;
      case 3:
      default:writeCommand(0x58,0x28,0x01); writeCommand(0x58,0x29,0x00); break;
    }
  }
}
```

> Спостереження: це **duty-high лише**, без зміни total-періоду (FD20/FD21 Fizik не чіпає).
> Спека (розд. 8) пропонує total=0x1000/high=0x0800 для 50%. Fizik використовує МАЛІ значення
> duty-high (0x0014…0x0546) — отже total теж малий (заводський дефолт). Збіг із спекою на
> рівні **«FD28/FD29 = PWM-підсвітка»**, але конкретні числа інші → перевірити на бенчі.
> Значення зберігається в **EEPROM addr 60** (`CONFIG_AMT_ADDR_BRIGHTNESS`), 0–3.

---

## 6. `offDisplay()` — teardown (доведений)

Латч `_state_video == true → false`. Повертає чіп у idle (≈ хвіст initDisplay):

```cpp
// AMT630.h:120
void offDisplay() {
  if (_state_video == true) {
    _state_video = false;
    writeCommand(0x58, 0x42, 0x03);   // FD42 PIN-PWM
    writeCommand(0x58, 0x1F, 0x03);   // FD1F PWM enable
    writeCommand(0x58, 0x28, 0x00);   // FD28 PWM0 duty lsb = 0  → підсвітка OFF
    writeCommand(0x58, 0x29, 0x00);   // FD29 PWM0 duty msb = 0
    writeCommand(0x58, 0x11, 0x1F);   // FD11 PLL
    writeCommand(0x58, 0x12, 0x38);   // FD12 PLL
    writeCommand(0x58, 0x13, 0x00);   // FD13 screen OFF
    writeCommand(0x59, 0xDC, 0x00);   // FEDC AV input reset
    writeCommand(0x5A, 0xD2, 0x54);   // FFD2 forced_blank = 0x54 (чорний)
    writeCommand(0x59, 0xD7, 0xFC);   // FED7 AV video off
    writeCommand(0x5A, 0xB0, 0x00);   // FFB0 snow off
    writeCommand(0x5B, 0x05, 0x00);   // FB05 = 0 → УСІ OSD-вікна OFF
  }
}
```

**Ключове:** teardown = підсвітка off (FD28/29=0) + screen off (FD13=0) + forced-black
(FFD2=0x54) + **OSD-вікна off (FB05=0)**. FB05=0 збігається з порадою спеки (розд. 7):
вимикати вікна перед операціями зі шрифтом, щоб уникнути glitch.

---

## 7. OSD-вивід тексту (`osdPrint` + `osdFontCode`) — доведений мінімум

`AMT630.h:174`. Пише в банк OSD `0x5B`/FBxx. **Тільки цифри 0-9 + великі A-Z + пробіл**
(вбудований ROM-шрифт, кирилиці немає):

```cpp
static uint8_t osdFontCode(char c) {
  if (c>='0'&&c<='9') return (c-'0')+0x01;   // 0x01..0x0A
  if (c>='A'&&c<='Z') return (c-'A')+0x0B;   // 0x0B..0x24
  if (c==' ') return 0x00;                    // пробіл
  return 0x00;
}
```

| Fizik dev,reg,val | OEM (FBxx) | Призначення |
|-------------------|------------|-------------|
| `0x5B,0x05,0x01` | FB05 | enable **Window0** (bit0) |
| `0x5B,0x07,len`  | FB07 | ширина вікна0 у символах |
| `0x5B,0x08,0x01` | FB08 | висота вікна0 = 1 символ |
| `0x5B,0x76,16`   | FB76 | **char_xsiz = 16 px** |
| `0x5B,0x77,22`   | FB77 | **char_ysiz = 22 px** |
| `0x5B,0x09,0x00` | FB09 | MSB координат |
| `0x5B,0x0A,x`    | FB0A | X-позиція (символи) |
| `0x5B,0x0B,y`    | FB0B | Y-позиція (символи) |
| `0x5B,0x0D,0x00` | FB0D | BGMAP-addr msb |
| `0x5B,0x00,0x00` | FB00 | BGMAP-addr lsb (старт) |
| **цикл по символах:** | | |
| `0x5B,0x0E,0x00` | FB0E | data msb = 0 (коди <256) |
| `0x5B,0x01,code` | FB01 | **data lsb → у VRAM, addr++** |
| `0x5B,0x10,0x09` | FB10 | атрибут: FG=білий(bit0-2), BG=прозорий |

> Це **точно** алгоритм osd_print зі спеки (розд. 5), і **char 16×22 px** збігається з
> ROM-шрифтом 16×22 з даташита. `osdPrint` у фінальній прошивці **закоментований** у
> `onDisplay()` (`// osdPrint(50,30,"12")`) — тобто на AMT630A Fizik **OSD-текст у релізі НЕ
> виводить**, лише живе відео. Увесь видимий UD/статус — на OLED. Але код osdPrint
> доведено-коректний (звірено з дизасемблером) і є робочим прикладом.

---

## 8. Логіка вмикання/вимикання AMT630A в `main.cpp` (стани)

`D1306.display_av` (EEPROM addr 55, `CONFIG_AMT_ADDR_STATUS`) керує режимом:

| display_av | Поведінка |
|------------|-----------|
| 0 = AUTO | дисплей AMT630A вмикається лише коли є відео (`onDisplay` при сигналі >1с, `offDisplay` при втраті >1с) |
| 1 = ON   | завжди `onDisplay()` |
| 2 = OFF  | завжди `offDisplay()` |

- CAN-команда `0x100` примусово ставить `display_av=2` + `offDisplay()` (вимкнути дисплей дистанційно).
- `initDisplay()` викликається **один раз** у `setup()` (рядок 123), безумовно.
- `changeBrightness` викликається в loop при зміні `display_brightness` (рядок 581).

---

## 9. Що `display_1306.h` робить ПОНАД `AMT630.h` (це все для OLED!)

`display_1306.h` (542 рядки) — повноцінний UI на OLED 128×64 через Adafruit-GFX. До AMT630A
**прямого стосунку не має** (лише читає ті ж EEPROM-комірки статусу/яскравості). Але як
референс **структури меню/UI** — цінний:

- **Boot-splash:** bitmap-логотип 64×64 (`myBitmap`, тризуб) + рамки + «SLAVA UKRAINE / Powered by Fizik / Sera Luciferchik», `delay(1000)` кожне.
- **10 екранів меню** (`loops(menu1, menu2)` → switch 0–9):
  0=home (RSSI 1.2/5.8/3.3, частоти, FPV-індикатори), 1=band_12 scanner, 2=band_58 scanner,
  3=buzzer, 4=battery, 5=wifi, 6=min-rssi, 7=display(AUTO/ON/OFF), 8=brightness(Low/Med/High/VeryLow),
  9=about (версія, RX-моделі).
- **Багаторядковість:** `viewText(str, x, y, size)` — обгортка над `setCursor/setTextSize/println`.
  Рядки укладаються вручну за y-координатою (10/20/30/40-px кроки).
- **RSSI-бари:** малюються «точками» (`viewText(".", x, y)`) у циклах — спектр-сканер 0–128 px по X.
- **Іконки:** лише bitmap-тризуб на сплеші; решта — текст.
- **Пресети яскравості (закоментовані!):** у `brightness_screen()` є **закоментовані**
  `writeCommand(0x58,0x28,...)` з ІНШИМИ значеннями ніж у AMT630.h:
  - Low: `0x28=0x14, 0x29=0x00`
  - Medium: `0x28=0xA4, 0x29=0x00`
  - High: `0x28=0xC0, 0x29=0x05`  ← **High тут 0xC0/0x05, а в AMT630.h case 2 = 0x46/0x05**
  - VeryLow: `0x28=0x01, 0x29=0x00`

> **КОНФЛІКТ #3 (внутрішній у Fizik):** «High» PWM-duty відрізняється між `display_1306.h`
> (закоментований `0xC0/0x05` = duty-high 0x05C0) і активним `AMT630.h` case 2 (`0x46/0x05`
> = 0x0546). Активний у релізі — **AMT630.h (0x0546)**. Закоментована версія з OLED-файлу —
> стара/інша калібровка. Бери значення з **AMT630.h** (розд. 5).
> Аналогічно `display_screen()`/`about_screen()` містять закоментовані `writeCommand(0x58,0x28/0x29)`
> — мертвий код, ігнорувати.

`display_1306.h` приватний `writeCommand` теж пише в `0x58` (FDxx) — підтверджує, що автор
свідомо керував PWM-підсвіткою AMT630A з OLED-меню (потім переніс у AMT630.h).

---

## 10. Зведені факти для ModESP-драйвера AMT630A

1. **Шина:** один `Wire.begin()` (GPIO21/22, 100 кГц), без setClock. AMT630A доведено працює на 100 кГц.
2. **Затримка:** `delay(10)` після кожного запису регістра — консервативно, але доведено.
3. **Device-адреси:** 0x58=Global/PWM/PLL, 0x59=AV, 0x5A=Video/контраст-яскравість-насиченість, 0x5B=OSD, **0x5F=vendor unlock**.
4. **«Яскравість» Fizik = PWM-підсвітка** (FD28/FD29 duty-high), НЕ FFD4. Зберігається EEPROM addr 60 (0–3).
5. **Init = unlock(0x5F magic 55/AA) → memory_system poke (FDC6) → standby (екран off, відео off, OSD-вікна on).**
6. **onDisplay = screen on (FD13=FF) + AV-decoder tune (~25 рег) + GAMMA/контраст/яскравість(FFxx) + OSD on + PWM brightness.**
7. **offDisplay = PWM off + screen off + forced-black (FFD2=0x54) + OSD off (FB05=0).**
8. **OSD-текст** (osdPrint): char 16×22 px, Window0, FB05/07/08/76/77/09/0A/0B/00/0D + цикл FB0E/01/10, attr 0x09. **У релізі закоментований** — реальний UI на OLED.
9. **forced_blank FFD2:** 0x4F = показати відео, 0x54 = чорний екран. Ефективний «м'який» on/off.

---

## 11. Перелік підтверджених/конфліктних пунктів

**Підтверджено дизасемблером (ground truth):**
- Rosetta-мапа банків спеки правильна; Fizik-«TFT_LCD_REG 0x58» = насправді FDxx (Global/PWM).
- CVBS input-select FED7→FED8→FEDC точно як у спеці (`ENGELS.A22:22395`).
- FFD3/D4/D6 = контраст/яскравість/насиченість; FFD2 = forced_blank_color (on/off).
- FB05/07/08/76/77/0E/10 = OSD-регістри точно як у спеці.

**Невирішені / для бенчу:**
- **#1** Fizik-«яскравість» керує PWM-підсвіткою (FD28/29), не FFD4 → дві різні «яскравості»; узгодити в драйвері.
- **#2** Fizik пише `0x5F,0xC6` (FDC6/memory_system — спекою позначено DANGER) — працює лише в vendor unlock-каналі 0x5F; не екстраполювати.
- **#3** значення PWM-duty «High» різняться між AMT630.h (0x0546, активне) і display_1306.h (0x05C0, закоментоване). Брати AMT630.h.
- **Невідомо:** точна семантика device `0x5F` (поза XRAM-мапою OEM) — це vendor init/unlock, не документований у Korth-реверсі. Магія 55/AA/53/03/50 потребує бенч-перевірки на конкретній ревізії.
- **Total PWM-період** (FD20/FD21) Fizik не задає → покладається на заводський дефолт прошивки; для керування duty потрібно знати/виставити total.
