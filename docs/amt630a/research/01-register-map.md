# AMT630A — авторитетна карта регістрів для керування по I²C

> **Дослідницький звіт #01.** Зведення чотирьох джерел:
> 1. **SPEC** — `docs/amt630a/AMT630A_user_spec.md` (канонічна спека користувача: Rosetta-карта банк↔I²C, формати, DANGER).
> 2. **ENGELS** — `ENGELS.A22` (nocash/Martin Korth дизасемблер OEM-прошивки `GK_SK_630A_43D_DX_3KEY_170911`, 4.3″/480×272). **Найбагатше джерело: абсолютні адреси + імена `IO_*` + бітові коментарі.**
> 3. **CLEAN** — `AMT630A.A22` (nocash дизасемблер прошивки 3.5″ `MT_630A_35D`, 320×240). Підтверджує ті самі `IO_*`-імена + дає коментовані функції (`apply_backlight`).
> 4. **FIZIK** — `AMT630.h` (доведений робочий I²C-драйвер на ESP32/Arduino). **Єдине джерело, що ПІДТВЕРДЖЕНЕ на реальному залізі.**
>
> Усі імена `IO_*` нижче — дослівно з дизасемблера nocash. Бітові примітки — реверс Korth (часто «?», бо отримані експериментами).

---

## 0. TL;DR — головні висновки

1. **Rosetta-карта SPEC підтверджена дизасемблером.** MCU XRAM-банк `Fxxh` ↔ I²C-адреса; низький байт MCU-адреси = № регістра. Відповідність:

   | MCU-банк | I²C 7-біт | I²C 8-біт (W) | Підсистема | Перевірено Fizik |
   |----------|-----------|---------------|------------|------------------|
   | `FBxx` | **`0x5B`** | `0xB6` | **OSD** (вікна, BGMAP, FONT, палітра) | ✅ так |
   | `FCxx` | `0x5C` | `0xB8` | LCD-color-swap + Video/Tcon-тайминг (50/60 Гц) | ⚠️ не вживано Fizik напряму |
   | `FDxx` | **`0x58`** | `0xB0` | Global: **PWM (підсвітка)**, ADC, SPI-flash, PLL, PIN-mode | ✅ так |
   | `FExx` | **`0x59`** | `0xB2` | **AV-декодер / вибір CVBS** | ✅ так |
   | `FFxx` | **`0x5A`** | `0xB4` | LCD/Video-process: gamma, **яскравість/контраст**, backdrop, snow | ✅ так |
   | (SFR/init) | **`0x5F`** | `0xBE` | **init/system** sub-протокол → CPU SFR (watchdog, memory_system) | ✅ так |

2. **РОЗВ'ЯЗАНО конфлікт іменування `0x58`/`0x5C`.** SPEC мала рацію, Fizik помилився в назвах (не в адресах):
   - Fizik `#define TFT_LCD_REG 0x58` — **назва оманлива**. `0x58` фізично = банк **FDxx (Global/PWM/PLL)**, НЕ LCD-тайминг. Доказ нижче (розділ 8) — усі записи Fizik у `0x58` (рег. `0x11/0x12/0x13/0x28/0x29/0x1F/0x42/0x19`) збігаються з `FD11/12/13` (PLL/screen-on-off), `FD28/29` (PWM0 backlight duty), `FD1F` (PWM enable). Тобто Fizik «яскравість» = duty підсвітки PWM0.
   - Справжній **LCD/Tcon-тайминг = банк FCxx = I²C `0x5C`**, який Fizik напряму не чіпає (працює поверх вже ініціалізованого OEM).
   - Спека теж не ідеальна: вона називає FFxx «video/яскравість» (вірно), але приписує «яскравість/контраст FFD3/D4» — це підтверджено; а Fizik «яскравість» (PWM) — це інша яскравість (підсвітка, FDxx). **Є дві різні «яскравості»** (див. розділ 14).

3. **ЧАСТКОВО СПРОСТОВАНО твердження SPEC, що `SFR C6h` недоступний по I²C.** Fizik робить `writeCommand(0x5F, 0xC6, 0x42)` і `0x00` на реальному залізі (`initDisplay()`). Тобто **банк `0x5F` дає доступ запису у CPU-SFR-простір** (включно з `C6h memory_system`, `BEh watchdog_unlock`, `AFh`, `A1h–A6h`). Це **не** XRAM-`FBxx–FFxx`-доступ — це окремий init/system-канал. Наслідок для нас: vsync-`SFR 91h` теоретично теж може бути читабельним через `0x5F` (не перевірено), але DMA-FONT через `C6h.bit3` лишається ризикованим (DANGER, розділ 12).

4. **Clock conflict:** SPEC каже PWM total «у циклах 26 МГц». Дизасемблер CLEAN прямо пише **27 МГц** (`27MHz/4000 = 6.75kHz`). Беремо 27 МГц.

---

## 1. Формат I²C-транзакції (з SPEC, підтверджено Fizik)

```c
// Fizik AMT630.h, рядки 15–21 — ДОВЕДЕНО на залізі:
static void writeCommand(uint8_t address, uint8_t reg, uint8_t data) {
    Wire.beginTransmission(address);   // address = 7-біт банку (0x58..0x5C, 0x5F)
    Wire.write(reg);                   // reg = низький байт MCU-адреси (nn з Fxxnn)
    Wire.write(data);                  // 1 байт даних
    Wire.endTransmission();
    delay(10);                         // Fizik ставить 10 мс між записами
}
```
Приклад: `FB05h = 0x1F` → `writeCommand(0x5B, 0x05, 0x1F)` (увімкнути всі 5 OSD-вікон).
**Читання** статусних регістрів (детект сигналу `FE2Ah` тощо): write reg-pointer → repeated-start → read 1 байт. Fizik читання не використовує (лише пише).

---

## 2. БАНК `FBxx` — OSD (I²C `0x5B`) ✅ підтверджено Fizik

OSD-рушій: BGMAP RAM (200h записів «екран»), FONT RAM (1000h слів ≈8 КБ), FONT ROM (418 симв. 16×22 1bpp). 5 вікон, вікно 0 — найвищий пріоритет (BGMAP fixed 000h).

### 2.1 Доступ до VRAM/FONT/BGMAP

| adr | Ім'я (ENGELS) | Призначення | Біти / нюанси | R/W | I²C-безпека |
|-----|---------------|-------------|---------------|-----|-------------|
| `FB00h` | `IO_OSD_bgmap_addr_lsb` | BGMAP-addr lsb | **авто-інкремент при записі `FB01h`** | RW | ✅ |
| `FB01h` | `IO_OSD_bgmap_data_lsb` | data lsb (№ символу low) | **запис → у VRAM** (латчить і msb) | RW | ✅ |
| `FB02h` | `IO_OSD_font_addr_lsb` | FONT-addr lsb (для ручного аплоаду) | 12-біт адреса слова | RW | ✅ |
| `FB03h` | `IO_OSD_font_data_lsb` | FONT-data **msb*** | *(nocash: коментар «DATA_MSB», тобто порядок lsb/msb тут переплутаний у назві) | RW | ✅ |
| `FB04h` | `IO_OSD_font_data_msb` | FONT-data **lsb*** | запис latch → у VRAM | RW | ✅ |
| `FB05h` | `IO_OSD_window_enable_bits` | **enable вікон** | bit0–4=Window0–4 on; bit6=TEXT on/off (hides); bit7=BITMAP(4bpp) on/off | RW | ✅ (Fizik: `0x1F`=всі вікна, `0x01`=лише W0, `0x00`=off) |
| `FB0Dh` | `IO_OSD_bgmap_addr_msb` | BGMAP-addr msb | bit1–7 NOT R/W (лише bit0); **ручний інкремент при переносі lsb** | part | ✅ |
| `FB0Eh` | `IO_OSD_bgmap_data_msb` | data msb (№ символу high) | bit2–7 NOT R/W (лише 2 біти) | part | ✅ |
| `FB0Fh` | `IO_OSD_font_addr_msb` | FONT-addr msb | bit4–7 NOT R/W | part | ✅ |
| `FB10h` | `IO_OSD_bgmap_data_attr` | атрибут кольору | bit0–2=FG (0=прозорий,1–6=палітра,7=чорний), bit4–6=BG; bit7 NOT R/W | part | ✅ (Fizik: `0x09`=FG білий,BG прозорий) |

> ⚠️ **Нюанс font_data lsb/msb:** ENGELS дослівно називає `FB03h`=«font_data_lsb ;DATA_MSB», `FB04h`=«font_data_msb ;DATA_LSB» — тобто навіть реверсер позначив порядок як неоднозначний. SPEC (розділ 7) каже: «`FB04h`=data_msb; `FB03h`=data_lsb (data_lsb латчить обидва)». **Перевірити на бенчі фактичний порядок пікселів** при завантаженні кирилиці.

### 2.2 Вікно 0 (геометрія)

| adr | Ім'я | Призначення | Біти | R/W |
|-----|------|-------------|------|-----|
| `FB07h` | `IO_OSD_window_0_size_x` | ширина W0 у символах | bit7 NOT R/W (1–127) | part |
| `FB08h` | `IO_OSD_window_0_size_y` | висота W0 у символах | bit6–7 NOT R/W (1–63) | part |
| `FB09h` | `IO_OSD_window_0_xyloc_msb` | msb X/Y + BGMAP-addr | bit7 NOT R/W; 2×11-біт | part |
| `FB0Ah` | `IO_OSD_window_0_xloc_lsb` | X-позиція lsb | (11-біт) | RW |
| `FB0Bh` | `IO_OSD_window_0_yloc_lsb` | Y-позиція lsb | (11-біт) | RW |
| `FB76h` | `IO_OSD_char_xsiz` | розмір символу X (px) | lower5bit (max ~24px); bit5–7 NOT R/W | part |
| `FB77h` | `IO_OSD_char_ysiz` | розмір символу Y (px) | lower6bit; bit6–7 NOT R/W | part |
| `FB32h` | `IO_OSD_window_0_scale` | масштаб W0 | bit4–7 NOT R/W | part |
| `FB33h` | `IO_OSD_window_1_and_2_scale` | масштаб W1+W2 | 2×2біт (lsb=ScaleX, msb=ScaleY) | RW |
| `FB34h` | `IO_OSD_window_3_and_4_scale` | масштаб W3+W4 | — | RW |
| `FB78h` | `IO_OSD_xyflip` | флип тайлів/мапи | bit4=TileXflip, bit5=MapXflip, bit6=TileYflip, bit7=MapYflip | RW |

Вікна 1–4: блоки `FB12h–FB17h` (W1), `FB18h–FB1Dh` (W2), `FB1Eh–FB23h` (W3), `FB24h–FB29h` (W4) — кожне: size_x, size_y, xyloc_msb, xloc_lsb, yloc_lsb, vramaddr_lsb. Vscale W0: `FB2Bh–FB2Eh`; Hscale W0: `FB2Fh–FB31h`.

### 2.3 Палітра (6 програмованих кольорів)

Формат **4:4:4 RGB** (по даних ENGELS): msb-байт = Blue(bit0–3), lsb-байт = Green(bit4–7)+Red(bit0–3). SPEC каже значення 0–10 (11–15 = той самий max).

| adr пара | Колір | Фабричне (ENGELS) |
|----------|-------|-------------------|
| `FB56h`/`FB57h` | `IO_OSD_color_1` | 00h,0Fh = **red** |
| `FB58h`/`FB59h` | `IO_OSD_color_2` | 00h,F0h = **green** |
| `FB5Ah`/`FB5Bh` | `IO_OSD_color_3` | 0Fh,00h = **blue** |
| `FB5Ch`/`FB5Dh` | `IO_OSD_color_4` | 00h,FFh = **yellow** (r+g) |
| `FB5Eh`/`FB5Fh` | `IO_OSD_color_5` | 0Fh,F0h = **cyan** (g+b) |
| `FB60h`/`FB61h` | `IO_OSD_color_6` | 0Fh,FFh = **white** (r+g+b) |

Колір 0 = прозорий (відео), колір 7 = чорний (фіксовані). Напівпрозорість: `FB06h` (`IO_OSD_misc_transp_enable`, upper2bit), рівень — `FB0Ch` (`IO_OSD_bright_transp_level`, upper3bit=brightness, lower3bit=transparency).

> ⚠️ `FBC6h`/`FBC7h` = `IO_OSD_bugged_color_lsb/msb` — реверс позначає «bugged?». Не використовувати.

---

## 3. БАНК `FCxx` — LCD color-swap + Video/Tcon-тайминг (I²C `0x5C`) ⚠️ НЕ чіпає Fizik напряму

Це **справжній** «LCD/Tcon» банк (а не Fizik-овий `0x58`). У робочій схемі OEM-прошивка вже все ініціалізувала — для Шляху A (поверх прошивки) сюди майже не лізуть. Майже всі регістри `fixed`/`unused`/`NOT R/W` — критичний тайминг, легко зламати картинку.

| adr | Ім'я | Призначення | Нюанс | I²C-безпека |
|-----|------|-------------|-------|-------------|
| `FC00h` | `IO_LCD_color_swap` | swap кольорів AV+OSD | fixed 05h; bit0–3 = swap (LCD-databus mode) | ⚠️ обережно |
| `FC02h` | (unused) | — | bit0–2 **kills AV image**; bit3–7 NOT R/W | ⚠️ |
| `FC90h` | `IO_VIDEO_control` | control PAL50/60 | fixed 02h; bit0=VerticalScale, bit5=Blue-instead-Red | ⚠️ |
| `FC91h–FC93h` | `IO_60HZ_control_*` | 60 Гц control | bit3=AV_OFF/BLACK | ⚠️ |
| `FC98h/FC99h` | `IO_60HZ_15khz` | 60 Гц H-rate (03C8h) | тайминг | ⚠️ |
| `FC9Ch…FCB0h` | `IO_60HZ_xloc/yloc/crop` | позиція/кроп AV+OSD 60 Гц | купа word-пар | ⚠️ |
| `FCBDh–FCBFh` | `IO_50HZ_control_*` | 50 Гц control | bit3=AV_BLACK/OFF | ⚠️ |
| `FCC4h/FCC5h` | `IO_50HZ_15khz` | 50 Гц H-rate (0466h) | **завеликі → екран фрізиться/біліє** | ⚠️ DANGER-ish |
| `FCC8h…FCDCh` | `IO_50HZ_xloc/yloc/crop` | позиція/кроп AV+OSD 50 Гц | «too large: screen freezes/goes white» | ⚠️ DANGER-ish |
| `FCD0h/FCD1h` | `IO_50HZ_yloc_av_osd` | V-позиція 50 Гц | **nonzero msb → freeze (повтор скан-лінії)** | ⚠️ DANGER-ish |
| `FCE4h` | `IO_VIDEO_something_4` | AV h-position (used!) | fixed 45h, ORed 40h | ⚠️ |
| `FCEAh` | `IO_VIDEO_something_5` | used, можливо write-only | read завжди FFh | ⚠️ |

**Висновок по FCxx:** для Шляху A не чіпати. Зміни тут масштабу/кропу легко вішають картинку (freeze/white). Якщо колись треба зсунути OSD по екрану — лише після бенч-тестів.

---

## 4. БАНК `FDxx` — Global: PWM / ADC / SPI-flash / PLL / PIN (I²C `0x58`) ✅ підтверджено Fizik

**Це Fizik-овий `TFT_LCD_REG=0x58` (назва помилкова — насправді Global-банк).**

### 4.1 PLL / screen on-off (Fizik пише сюди в `initDisplay`/`onDisplay`!)

| adr | Ім'я | Призначення | Біти | I²C-безпека |
|-----|------|-------------|------|-------------|
| `FD01h` | `IO_PLL_01h_cpu` | PLL | **bit0: hangs CPU** | ⚠️ DANGER |
| `FD0Bh` | `IO_PLL_0Bh_used` | force color/snow | bit0=forceNTSCcolor, bit1,3–7=BLUE/SNOW | ⚠️ |
| `FD0Eh` | `IO_PLL_0Eh_used` | scanline freeze | set 20h/2Ch | ⚠️ |
| `FD11h` | `IO_PLL_11h_used` | PLL/OSD | bit0?**hangs CPU**, bit1–2 OSD-error+hang, bit4=OSD_BG_ONLY, bit5=scanlinefreeze | ⚠️ DANGER (Fizik пише `0x1F`!) |
| `FD12h` | `IO_PLL_12h_used` | PLL | **DANGER: hang ADC?** (Fizik пише `0x38`) | ⚠️ DANGER |
| `FD13h` | `IO_PLL_13h_used` | **screen on/off** | 00h=off, FFh=on | ✅ (Fizik: `0xFF`=on у `onDisplay`, `0x00`=off у `offDisplay`) |
| `FD17h` | `IO_PLL_adc_clk_divider` | ADC clk | bit7=DANGER | ⚠️ |
| `FD18h` | `IO_PLL_18h_pwm` | force backlight | **bit7: ForceMaxBacklight → PWM-dimming OFF** | ⚠️ важливо для PWM-яскравості |
| `FD19h` | `IO_PLL_19h_used` | signal/darker | bit0=NoSignal, bit6=Darker, bit7=NoSignal | ✅ (Fizik: `0x08`) |

> 🔑 **Конфлікт із SPEC (важливо):** SPEC класифікує `FD11/FD12` як небезпечні-ish, і це справді так у реверсі. Проте Fizik **успішно пише в них на реальному залізі** в `initDisplay`: `0x58,0x11,0x1F`/`0xFF`, `0x58,0x12,0x38`/`0xFF`, `0x58,0x13,0x00`/`0xFF`, `0x58,0x42,0x03`, `0x58,0x1F,0x03`, `0x58,0x19,0x08`, `0x58,0x28/0x29`. Тобто конкретні значення Fizik безпечні (це послідовність ON/OFF дисплея), хоча довільні біти в `FD11/FD12` — небезпечні. **Висновок: копіювати точні значення Fizik, не імпровізувати в FD11/FD12.**

### 4.2 PWM — підсвітка (економія енергії) ✅ Fizik «яскравість»

| adr | Ім'я | Призначення | I²C-безпека |
|-----|------|-------------|-------------|
| `FD1Fh` | `IO_PWM_enable_flags` | enable: bit0–3 = PWM0–3 on | ✅ (Fizik: `0x03`) |
| `FD20h`/`FD21h` | `IO_PWM0_duty_total_lsb/msb` | PWM0 **total** (період, цикли 27 МГц) | ✅ |
| `FD28h`/`FD29h` | `IO_PWM0_duty_high_lsb/msb` | PWM0 **high** (тривалість HIGH) | ✅ (Fizik `changeBrightness` пише сюди 4 рівні!) |
| `FD22h–FD2Fh` | `IO_PWM1/2/3_*` | PWM1=SPI.DTA, PWM2=SPI.CLK/RST, PWM3=SPI./CS | ⚠️ це SPI-flash піни — не чіпати |

**Fizik `changeBrightness()` (доведено на залізі):**
```cpp
case 0: writeCommand(0x58,0x28,0x14); writeCommand(0x58,0x29,0x00); // high=0x0014
case 1: writeCommand(0x58,0x28,0xA4); writeCommand(0x58,0x29,0x00); // high=0x00A4
case 2: writeCommand(0x58,0x28,0x46); writeCommand(0x58,0x29,0x05); // high=0x0546
case 3: writeCommand(0x58,0x28,0x01); writeCommand(0x58,0x29,0x00); // high=0x0001 (default)
```
> ⚠️ **Конфлікт значень SPEC vs Fizik vs CLEAN:** SPEC дає «50% = total 0x1000, high 0x0800». CLEAN-функція `apply_backlight` пише: підсвітка ON лише коли `high ≥ ~600`(dec), а short high (<600) ігнорується; рекомендує `total=4000 @50%` або `total=1200 @85%`. Малі `total` (напр. 0x0100) → backlight off. Fizik використовує fixed total (з `initDisplay`) і варіює лише `high`. **Беремо логіку CLEAN: high має бути ≥ ~600 dec; SPEC-ове 0x0800(2048) це задовольняє.**
> ⚠️ `FD18h.bit7` (ForceMaxBacklight) має бути **0**, інакше PWM-dimming вимкнено і duty ігнорується. Перевірити стан цього біта на бенчі.

### 4.3 PIN-mode (DANGER — SPI-flash піни)

| adr | Ім'я | Небезпека |
|-----|------|-----------|
| `FD32h` | `IO_PIN_P10_P11_spi_flash` | **DANGER — SPI flash піни** |
| `FD33h` | `IO_PIN_P12_P13_spi_flash` | **DANGER — SPI flash піни** |
| `FD42h` | `IO_PIN_P35_P36_pwm` | PWM0/PWM1; **bit0–2 = ScreenBlack(backlight)** |
| `FD40h/FD41h` | `IO_PIN_*_lcd` | bit0=StopDotClk / KillTftUpdating | ⚠️ |

### 4.4 ADC (keypad) — `FDBxh`

| adr | Ім'я | Призначення | I²C-безпека |
|-----|------|-------------|-------------|
| `FDB0h`/`FDB1h` | `IO_ADC_ctrl_lsb/msb` | ADC control | ⚠️ **bit1–2 DANGER** (lsb), bit0 NOT R/W |
| `FDB2h` | `IO_ADC_config_1` | fixed 20h | ⚠️ bit1 DANGER |
| `FDB4h` | `IO_ADC_config_2` | fixed 22h | ⚠️ bit7 DANGER |
| `FDB5h` | `IO_ADC_config_3` | fixed 37h | ⚠️ bit0–1 DANGER |
| `FDB8h`/`FDB9h` | `IO_ADC_status_lsb/msb` | write 0 = ack | R(ack) |
| `FDBCh`/`FDBDh` | `IO_ADC_input_0_lsb/msb` | **analog 0 (keypad)** | R only (STAT) ✅ читати |
| `FDBEh–FDC1h` | `IO_ADC_input_1/2` | analog 1/2 | R only |

Keypad-пороги (SPEC, 3.5″): ~0x0564=+(Up/Right), ~0x0804=Menu, ~0x0B38=−(Down/Left), ~0x0FF5=нічого. На 4.3″ + і − поміняні.

### 4.5 SPI-flash — **DANGER, НЕ чіпати по I²C** (Шлях B, дамп прошивки)

`FDD0h` `IO_SPI_transfer_mode` (DANGER), `FDDEh` `IO_SPI_kick_stop_reset`, `FDE0h` (unused, DANGER!), `FDE4h` `IO_SPI_command_write`, `FDF1h` `IO_SPI_upper_32k_code_base` (bit0–3 DANGER). Команди FDD0h: 01h=read JEDEC ID, 04h=enable read/erase, 08h=ERASE, 10h=read flash→cpu, **40h=DMA flash→vram**, **80h=DMA ram→flash**. Для Шляху A — не торкатися.

---

## 5. БАНК `FExx` — AV-декодер / вибір CVBS (I²C `0x59`) ✅ підтверджено Fizik

3 фізичні CVBS-входи з внутрішнім мукером. Fizik пише сюди ~30 регістрів у `onDisplay()` (AV-калібрування). Найважливіше:

### 5.1 Вибір входу / video on-off

| adr | Ім'я | Призначення | Біти / SPEC-послідовність |
|-----|------|-------------|---------------------------|
| `FED7h` | `IO_AV_video_on_off` | enable/disable + disable-snow | bit0+1+6+7 = freeze backdrop/snow. **SPEC: bit3,4 = vid on(3)/off(0)**. Fizik: `0xFC` (init), пізніше `0xE7` |
| `FED8h` | `IO_AV_input_select_reg_0` | вибір входу #0 | **SPEC: bit6,7** (CVBS1=2, CVBS3=0). Fizik: `0xA3` (у `onDisplay`) |
| `FEDCh` | `IO_AV_input_select_reg_1` | вибір входу #1 | **SPEC: bit4,5** (CVBS1=0, CVBS3=2). Fizik: `0x00`(off)→`0x20`(on) |
| `FED9h` | `IO_AV_config_FED9h_bits` | used (bit4–5 clr, bit6 set) | — |
| `FEDBh` | `IO_AV_config_FEDBh_bit` | used (bit7 clr) | — |

**SPEC-послідовність перемикання входу** (строго): `FED7h` bit3,4→0 → `FED8h` bit6,7 → `FED7h` bit3,4→3 → `FEDCh` bit4,5.

| Вхід | `FED7h` bit3-4 | `FED8h` bit6-7 | `FEDCh` bit4-5 |
|------|---------------|----------------|----------------|
| CVBS1 (AV1) | 0 → 3 | 2 | 0 |
| CVBS3 (AV3) | 0 → 3 | 0 | 2 |

> ⚠️ **Конфлікт значень:** SPEC дає чисті bit-маски (`FED8h` bit6,7). Fizik пише цілі байти (`FED8h=0xA3`, `FED7h=0xE7`) — вочевидь специфічні для ревізії OEM-прошивки 1.36.x. **На цільовій платі KOZHAN перевірити точні значення на бенчі** (SPEC сам це радить, п.14).

### 5.2 Детект сигналу (читання)

| adr | Ім'я | Біти | R/W |
|-----|------|------|-----|
| `FE26h` | `IO_AV_stat_detect_0` | OFTEN used; video DETECT (SPEC: bit1,2) | R (STAT) |
| `FE27h` | `IO_AV_stat_detect_1` | bit0 tested | R |
| `FE28h` | `IO_AV_stat_framerate_flag` | **bit2 = PAL/NTSC (50/60 Гц)** | R |
| `FE2Ah` | `IO_AV_stat_signal_detect` | **bit4=HaveCVBS1, bit6=HaveCVBS3**, bit0–3=ErrorFlags | R |
| `FED0h` | `IO_AV_stat_detect_2` | used status | R |
| `FEAAh`/`FEABh` | `IO_AV_stat_sensitivity_msb/lsb` | 16-біт рівень сигналу (NoSignal=0FFFh) | R |

### 5.3 AV-калібрування (Fizik пише, але це OEM-tuning)

`FE01h` `IO_AV_ctrl_whatever_1`, `FE15h` `IO_AV_ctrl_sensitivity_0` (00h=max,05h=med,09h=low), `FE54h` `IO_AV_ctrl_whatever_2`, `FEA0h` `IO_AV_force_resync` (bit0 pulse — обережно: NoSignal/freeze!), `FECBh` `IO_AV_ctrl_artifacts` (bit0=LessPalArtifacts), `FED5h` `IO_AV_ctrl_sensitivity_1`. Fizik у `onDisplay` шле `0x59,0x01,0x06`, `0x59,0x04,0x80`, `0x59,0x05,0x30`, `0x59,0x54,0x40`, `0x59,0x8A/0x8B`, `0x59,0xA4..0xE3` — це AV-decoder tuning, копіювати дослівно якщо потрібен такий самий decode.
`FE42h` `IO_AV_secret_control` — unused прошивкою, але має цікаві ефекти (експериментальний).

---

## 6. БАНК `FFxx` — LCD/Video-process: gamma / яскравість / контраст / backdrop (I²C `0x5A`) ✅ підтверджено Fizik

**Це Fizik-овий `0x5A` (у Fizik без `#define`, лише сирий `0x5A`).**

### 6.1 Яскравість / контраст / насиченість / tint

| adr | Ім'я | Параметр | Medium (SPEC) | I²C-безпека |
|-----|------|----------|---------------|-------------|
| `FFD3h` | `IO_LCD_basic_contrast` | контраст | 0x7E | ✅ |
| `FFD4h` | `IO_LCD_basic_brightness` | **яскравість картинки** (не підсвітка!) | 0x8E | ✅ |
| `FFD5h` | `IO_LCD_basic_tint` | tint (лише NTSC) | 0x00 | ⚠️ **bit7 ламає PAL-колір** (BUG, не чіпати) |
| `FFD6h` | `IO_LCD_basic_saturation` | насиченість | 0x38 | ✅ |

### 6.2 Backdrop / display on-off / snow (Fizik активно пише)

| adr | Ім'я | Призначення | I²C-безпека |
|-----|------|-------------|-------------|
| `FFD2h` | `IO_LCD_forced_blank_color` | **display on/off** (4Fh=показ AV/backdrop, 5xh=blank fixed color) | ✅ (Fizik: `0x54`=blank у `offDisplay`, `0x4F`=показ у `onDisplay`) |
| `FFB0h` | `IO_LCD_snow_enable_and_misc` | snow/backdrop ctrl | ✅ (Fizik: `0x00`=off, `0xA3`=on) bit7=snow on/off, bit5=disable picture |
| `FFB1h` | `IO_LCD_sharpness_or_so` | різкість AV | ✅ |
| `FFB2h–FFB4h` | `IO_LCD_config_FFB2h..B4h` | fixed 20h | Fizik пише `0x1C` ×3 |
| `FFCEh/CF/D0h` | `IO_LCD_backdrop_color_Y/Cb/Cr` | колір backdrop (YCbCr) | BLUE=13h,DDh,72h; BLACK=00h,80h,80h ✅ |
| `FFDAh` | `IO_backdrop_snow_level` | кількість snow-пікселів | fixed 6Ch |

### 6.3 Gamma + decode config

`FF00h` `IO_LCD_config_FF00h` (gamma mode, init 03h). Gamma ramps: `FF01h–FF1Fh` red, `FF20h–FF3Eh` green, `FF3Fh–FF5Dh` blue (впливають і на OSD-кольори, крім яскравих). `FFF0h–FFFBh` `IO_LCD_config_FFFxh` — AV color-decoding (fixed, не чіпати). Fizik у `onDisplay` шле `0x5A,0xD3,0x80`,`0xD4,0x80`,`0xD6,0x56`,`0xDA,0x6C`,`0xF0..0xFA` — decode/backdrop tuning, копіювати дослівно.

---

## 7. БАНК `0x5F` — init/system (CPU SFR sub-протокол) ✅ підтверджено Fizik

**Це НЕ XRAM-банк `Fxxh`.** Адреса I²C `0x5F` (8-біт W = `0xBE`) — окремий канал, що пише у **CPU SFR-простір 80h–FFh** (зазвичай недоступний ззовні). Fizik використовує його у `initDisplay()` для розблокування system/memory та watchdog.

| reg (через `0x5F`) | SFR-ім'я (ENGELS) | Призначення | Fizik-значення |
|--------------------|-------------------|-------------|----------------|
| `0xAF` | (ctl3, unused-у-SFR) | system gate/unlock? | `0x00` потім `0x11` |
| `0xA1–0xA6` | (A1h..A6h unused-у-SFR) | unlock-послідовність (магічні) | `0x55,0xAA,0x03,0x50,0x00,0x53` |
| `0xC6` | `SFR_IO_memory_system` | **DANGER**: spi-flash/OSD memory system; **bit3 = pause FONT render під час DMA** | `0x42` потім `0x00` |
| `0xBE` | `SFR_IO_watchdog_unlock` | unlock/lock watchdog (55h=unlock, AAh=lock) | `0x55` потім `0xAA` |
| `0xBA` | `SFR_IO_watchdog_enable` | watchdog on/off | `0x00` |

**ENGELS-підтвердження C6h.bit3 = FONT-render-pause:**
```asm
9ED3  or   [SFR_IO_memory_system],08h ;bit3=1  ;-pause FONT rendering during DMA upload
9F26  and  [SFR_IO_memory_system],0F7h ;bit3=0 ;-resume FONT rendering after DMA upload
```
**ENGELS-підтвердження watchdog unlock-протоколу:**
```asm
0951  mov  [SFR_IO_watchdog_unlock],55h   ;-unlock
095D  mov  [SFR_IO_watchdog_unlock],0AAh  ;-lock
```

> 🔑 **Це частково спростовує SPEC** (розділи 1, 7, 9), яка стверджує: «`SFR C6h`/`SFR 91h` — тільки внутрішній MCU, недоступні по I²C». Fizik доводить, що **запис у `C6h` по I²C через банк `0x5F` працює** на реальному залізі. Отже:
> - DMA FLASH→FONT (потребує `C6h.bit3`) **теоретично можливий по I²C** через `0x5F` — але це **DANGER** (memory_system), і Fizik його не робить (вантажить шрифт інакше). Не покладатися без бенч-перевірки.
> - vsync-`SFR 91h` читання через `0x5F` — **не перевірено** (Fizik не читає). SPEC-теза «vsync недоступний» можливо хибна, але непідтверджена.

---

## 8. РОЗВ'ЯЗАННЯ конфлікту іменування `0x58`/`0x5C` (SPEC vs Fizik)

**Питання:** Fizik `#define TFT_LCD_REG 0x58` — це LCD-банк (як натякає назва) чи Global-банк (як каже SPEC Rosetta)?

**Відповідь: Global-банк (FDxx). SPEC має рацію щодо адрес; Fizik помилився лише в НАЗВІ define.**

Доказ — кожен запис Fizik у `0x58` зіставлено з ENGELS `FDxx`:

| Fizik запис | = MCU adr | ENGELS ім'я | Що це насправді |
|-------------|-----------|-------------|-----------------|
| `0x58,0x42,0x03` | `FD42h` | `IO_PIN_P35_P36_pwm` | PWM0/1 pin-mode (bit0–2 ScreenBlack) |
| `0x58,0x1F,0x03` | `FD1Fh` | `IO_PWM_enable_flags` | enable PWM0+PWM1 |
| `0x58,0x28/0x29` | `FD28/29h` | `IO_PWM0_duty_high` | **підсвітка PWM duty** (= Fizik «яскравість») |
| `0x58,0x11,0xFF` | `FD11h` | `IO_PLL_11h_used` | PLL/OSD enable |
| `0x58,0x12,0xFF` | `FD12h` | `IO_PLL_12h_used` | PLL |
| `0x58,0x13,0xFF/0x00` | `FD13h` | `IO_PLL_13h_used` | **screen on/off** |
| `0x58,0x19,0x08` | `FD19h` | `IO_PLL_19h_used` | signal/darker ctrl |

Жоден з цих регістрів не належить LCD-тайминг-банку FCxx. Тому:
- **`0x58` = FDxx Global/PWM/PLL** (підтверджено).
- **`0x5C` = FCxx справжній LCD/Tcon-тайминг** (Fizik його не чіпає взагалі — працює поверх готової OEM-ініціалізації).
- Fizik «яскравість» (`changeBrightness`) ≠ SPEC «яскравість» (`FFD4h`). Fizik регулює **підсвітку** (FD28/29 PWM duty); SPEC `FFD4h` регулює **video brightness** картинки. Дві різні речі — обидві валідні.

---

## 9. DANGER-регістри (вішають/перезавантажують/ламають чіп)

### 9.1 CPU-SFR (доступні через банк `0x5F`, не через XRAM-`Fxx`)

| reg | Ім'я | Небезпека |
|-----|------|-----------|
| `C6h` | `SFR_IO_memory_system` | **DANGER**: spi-flash/OSD memory system; зміна не-bit3 бітів ризикована (reboot/hang). Fizik пише лише `0x42`/`0x00` у відомій послідовності |
| `87h` | `pcon` (PCON) | bit0–1 = **halt/idle** (DANGER) |
| `BAh/BBh/BEh` | watchdog enable/reload/unlock | неправильна послідовність → reset |
| `D8h` (s1con) | — | SPEC: bit5 → CPU у 21× повільніший |

### 9.2 XRAM I/O — DANGER по I²C (підтверджено ENGELS-коментарями)

| adr | Ім'я | Чому DANGER |
|-----|------|-------------|
| `FD01h` | `IO_PLL_01h_cpu` | **bit0: hangs CPU** |
| `FD11h` | `IO_PLL_11h_used` | bit0?: hangs CPU; bit1–2: OSD-error + hang |
| `FD12h` | `IO_PLL_12h_used` | DANGER: hang ADC |
| `FD17h` | `IO_PLL_adc_clk_divider` | bit7 = DANGER |
| `FDB0h` | `IO_ADC_ctrl_lsb` | bit1–2 DANGER |
| `FDB2h/B4h/B5h` | `IO_ADC_config_*` | bit1 / bit7 / bit0–1 DANGER |
| `FD32h/FD33h` | `IO_PIN_*_spi_flash` | **SPI-flash піни — фізично ламає flash-доступ** |
| `FDD0h` | `IO_SPI_transfer_mode` | flash-операції (erase/DMA) |
| `FDDEh` | `IO_SPI_kick_stop_reset` | bit6: hang CPU (SPEC); flash start/stop/reset |
| `FDE0h` | (unused) | ENGELS: **«DANGER!»** crashes CPU |
| `FDF1h` | `IO_SPI_upper_32k_code_base` | bit0–3 DANGER (code-base!) |
| `FCC4h/C5h, FCC8h…FCDCh, FCD0h/D1h` | `IO_50HZ_*` тайминг | завеликі значення → **freeze / екран біліє** |
| `FFD5h.bit7` | `IO_LCD_basic_tint` | **ламає декодування PAL-кольору** (BUG) |

**Правило Шляху A:** працювати в банках **OSD `0x5B`**, **AV `0x59`**, **video/яскравість `0x5A`**, безпечні частини **`0x58`** (PWM `FD1Fh–FD29h`, screen-on/off `FD13h`, читання ADC `FDBCh+`), і **`0x5F` лише точними послідовностями Fizik**. **НЕ лізти** в SPI-flash область `0x58/FDD0h+` і LCD-тайминг `0x5C/FCxx`.

---

## 10. Таблиця підтвердження джерел

| Підсистема | SPEC | ENGELS | CLEAN | FIZIK (залізо) | Статус |
|------------|:----:|:------:|:-----:|:--------------:|--------|
| `0x5B` OSD enable/print (FB05/00/01/0E/10) | ✅ | ✅ | ✅ | ✅ | **підтверджено** |
| `0x5B` font upload (FB02/03/04/0F) | ✅ | ✅ | ✅ | ✖ (Fizik не вантажить) | імена ✅, порядок lsb/msb **перевірити** |
| `0x5B` палітра FB56–61 | ✅ | ✅ | ✅ | ✖ | **підтверджено (ROM-значення)** |
| `0x58`=FDxx Global/PWM | ✅(rosetta) | ✅ | ✅ | ✅ | **підтверджено** |
| PWM backlight FD20/21/28/29 | ✅(26МГц?) | ✅(27МГц) | ✅(27МГц) | ✅(duty) | clock=**27 МГц** |
| `0x59`=FExx AV select FED7/D8/DC | ✅(bit-маски) | ✅(імена) | ✅ | ✅(інші байти) | адреси ✅, значення **звірити на бенчі** |
| AV detect FE26/28/2A | ✅ | ✅ | ✅ | ✖ | **підтверджено (імена/біти)** |
| `0x5A`=FFxx brightness FFD3/D4/D6 | ✅ | ✅ | ✅ | ✖(пише backdrop) | **підтверджено** |
| `0x5A` display on/off FFD2, snow FFB0 | ✖ | ✅ | ✅ | ✅ | **підтверджено** |
| `0x5C`=FCxx LCD/Tcon тайминг | ✅(rosetta) | ✅ | ✅ | ✖(не чіпає) | **підтверджено (не для Шляху A)** |
| `0x5F` init→SFR C6/BE/watchdog | частково ✖ | ✅(SFR) | ✅ | ✅ | **SPEC-теза «C6 недоступний по I²C» спростовано** |

---

## 11. Невирішені питання (для бенч-тесту на платі KOZHAN)

1. **Точні байти вибору CVBS** (`FED7/FED8/FEDCh`) — SPEC bit-маски vs Fizik цілі байти (`0xE7/0xA3/0x20`). Залежить від ревізії OEM-прошивки. Звірити.
2. **Порядок lsb/msb у FONT-data** (`FB03h`/`FB04h`) — навіть nocash позначив неоднозначно. Перевірити при першому завантаженні кирилиці.
3. **Стан `FD18h.bit7` (ForceMaxBacklight)** — якщо =1, PWM-dimming вимкнено і `FD28/29` не діятимуть. Перевірити перед регулюванням яскравості.
4. **Чи `0x5F` дає READ доступ до SFR** (зокрема vsync `91h`) — Fizik лише пише. SPEC каже «не можна», але вже помилилася щодо C6h-запису. Не покладатися.
5. **Чи OEM-прошивка перезаписує OSD-вікна** (конфлікт із нашими записами по I²C) — SPEC п.14 застереження.
6. **Clock підсвітки** — SPEC «26 МГц» vs дизасемблер «27 МГц». Беремо 27 МГц (firmware-authoritative), але для duty% це не критично (відношення high/total).

---

## 12. Рекомендована безпечна послідовність (Шлях A, поверх OEM-прошивки)

```
1. Power-on: дочекатись стабілізації OEM-прошивки (~200 мс).
2. (опц.) init/system unlock — ТІЛЬКИ якщо потрібно (Fizik робить у initDisplay через 0x5F).
3. Підсвітка: FD20/21=total, FD28/29=high(≥600dec), FD1F=enable PWM0   (dev 0x58)
4. Вибір CVBS: послідовність FED7→FED8→FED7→FEDC                       (dev 0x59)
5. (раз) Кирилиця у FONT RAM 1C0h+: FB05=0 → FB02/0F+FB03/04 → FB05=enable (dev 0x5B)
6. Вікно 0: FB05/07/08/09/0A/0B + FB76/77 + FB32                       (dev 0x5B)
7. Палітра: FB56..FB61                                                  (dev 0x5B)
8. Картинка: FFD3 контраст, FFD4 яскравість, FFD6 насиченість          (dev 0x5A)
9. Цикл: osd_print() через FB00/01/0E/10 при зміні даних               (dev 0x5B)
```

**Джерела:** Arkmicro AMT630A Spec V1.1 (через user-spec); Martin Korth no$x51 reverse (`ENGELS.A22` OEM 4.3″, `AMT630A.A22` 3.5″); Fizik `AMT630.h` (DETECTOR_FPV 1.36.9.5, підтверджений I²C на залізі).
