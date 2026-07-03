# AMT630A — Master I2C Control Reference (ESP32 over running OEM firmware)

> **Status:** Synthesized from OEM-firmware disassembly (ENGELS.A22 / AMT630A.A22), the ARKMICRO
> *AMT630A Video Display Controller Product Specification V1.1* (condensed preview), and the existing
> ModESP driver. Every register value is opcode-backed (cited `FILE:line`) unless explicitly marked
> **INFERRED** or **datasheet-stated**. Sources by trust: **ENGELS** = firmware disasm with absolute
> addresses + opcode bytes (PRIMARY, closest to the user's KOZHAN 4.3" board family);
> **CLEAN** = second disasm with best behavioral comments; **datasheet_fitz.txt** = vendor spec text.

## Overview

The AMT630A (Arkmicro) is a 3-input CVBS video decoder + TFT scaler + hardware-OSD SoC with an
embedded 8051 (80C52) core that runs OEM firmware from external SPI-flash. We do **not** replace that
firmware — we drive the chip **externally over I2C from an ESP32, on top of the running OEM firmware**.
The 8051's memory-mapped I/O registers (XRAM pages `FBxx`..`FFxx`) are reachable both internally
(8051 `MOVX`) and externally via the I2C slave port. So every firmware register write
`MOVX FExx = v` has an exact I2C equivalent: a 3-byte transaction `[dev7, reg_low, value]`.

### Rosetta bank map — MCU XRAM page ↔ I2C device address (VENDOR-CONFIRMED)

The mapping is now confirmed both by reverse engineering **and** by the datasheet TOC
(`datasheet_fitz.txt:107-116`), which states each block's I2C 8-bit address and XRAM page explicitly.

| MCU page | I2C 7-bit | I2C 8-bit (W) | Subsystem | Vendor block (datasheet) |
|---|---|---|---|---|
| `FBxx` | `0x5B` | `0xB6` | OSD (windows, BGMAP, FONT, palette) | §6.8 OSD |
| `FCxx` | `0x5C` | `0xB8` | LCD/Tcon timing + scaler color-swap **(DO NOT TOUCH)** | §6.2 Tcon / §6.7 Scaler |
| `FDxx` | `0x58` | `0xB0` | Global: PWM backlight, ADC, SPI-flash, PLL, pin-mode | §6.1 Global / §6.9 SPI / §6.10 ADC |
| `FExx` | `0x59` | `0xB2` | AV decoder / CVBS input select + signal detect | §6.3 Decoder |
| `FFxx` | `0x5A` | `0xB4` | Video-process: gamma, brightness/contrast/saturation, backdrop, snow | §6.4 Video-Process / §6.5 GAMMA |
| (SFR) | `0x5F` | `0xBE` | vendor unlock/init sub-protocol → CPU SFR (NOT in datasheet TOC; INFERRED) | — |

Conversion: `I2C7 = I2C8 >> 1`. Write transaction = `[dev7, reg_low_byte, value]`. So MCU write
`FEDC = 0x20` == `amt_w(0x59, 0xDC, 0x20)`. `amt_w(dev7,reg,val)` is used throughout this document
for an I2C write; `amt_r(dev7,reg,&out)` for a read; `amt_rmw(dev7,reg,and_mask,or_mask)` for a
read-modify-write.

> **datasheet caveat:** §6.6 RCRT registers are **"MCU access only"** (`datasheet_fitz.txt:112`) and may
> silently no-op over external I2C. Our backdrop/snow registers (`FFB0/D2/CE-D0/DA`) are §6.4/6.5 by
> address range (I2C-OK), but **bench-verify any NEW FFxx register** before relying on it.

---

## 0. Bench reconciliation (stand_s3 board — proven hardware facts)

This doc is derived from the OEM firmware disassembly (authoritative for *register semantics*). The
following are bench-proven facts on the real `stand_s3` board (ESP32-S3, I2C SDA=GPIO6/SCL=GPIO5,
panel ZCD-630A-4.3D). Where they refine the firmware analysis, **the bench wins** — trust it.

1. **Path-A init is mandatory — never run the full FIZIK `apply_init_table()`.** The AMT630A's own
   8051 firmware already shows the camera on power-up. Running the full init does an off→on of video
   banks `0x58/0x59/0x5A` and **destroys the working video unrecoverably** (needs a power-cycle; the
   chip keeps state across ESP reflash). Use `apply_osd_init()` only = vendor-unlock (bank `0x5F`:
   AF / A1-A6 / C6) + `FB05=0x1F` (OSD windows on). Then OSD overlays the live video. (§4 init.)

2. **The input-switch blanking is the off→on bracket + wrong-revision mux.** Bench: ANY input switch
   blanked video and did not recover. Root cause (this doc): the select sequence brackets with FED7
   video off→on, and if the FED8/FEDC mux values are for the *wrong firmware revision* (§1b OLD vs
   NEW), the "switch back" never re-selects the live camera → permanent blank. **Fixes to try on the
   bench, in order:** (a) use the correct revision's values (try OLD first, then NEW); (b) after
   switching, confirm lock via `FE2A` (AV1=bit4, AV3=bit6) and auto-fall-back to the other mapping if
   not locked; (c) if off→on itself is what hangs, try RMW-ing only FED8/FEDC mux bits **without** the
   FED7 off→on bracket (video stays on). Also confirm whether a camera is physically wired to the
   target input at all (switching to an empty input *correctly* shows no-signal).

3. **No-signal: `FFD2` (reversible black) + `FFB0/FFCE-D0` (blue/snow).** `FEDC=0x40`=black is
   bench-real but sticky and collides with input-mux → abandoned for `FFD2`. `FFD2` 4F/54 only does
   show/black; for BLUE and SNOW use the `FFB0/FFCE-D0/FFDA` path (§1a, §6) — reversible and
   collision-free. Re-assert on `FE26.bit1` edges (OEM firmware may repaint).

4. **Fonts: bench uses `FB77(ysiz)=20`, `FB76(xsiz)=16`, 1 word/row.** 193 glyphs × 20 words = 3860
   fits the FONT RAM; 22 rows (4246) does NOT. Critically, on the silicon `xsiz=16` behaves as
   **INNER.LEN = ysiz (1 word/row)** — the ENGELS "xsiz≥16 → 1.5× branch" (§8) did **not** manifest on
   this chip, so `xsiz=16` is safe (the §8 caution to use ≤15 is superseded by bench). `bitmap_start`
   (FB11/FB70) is RELATIVE to 0x1C0 = `first_tile+count`; an absolute value (e.g. 0x281) leaves glyphs
   in the 4bpp zone → **colored squares** (the long-standing bring-up bug, now understood). (§8.)

5. **`osd_print`: write the attr `FB10` AFTER each cell's tile (FIZIK order), not once per row** —
   attr-once leaves column 0 with a stale 1-cell-lagged attribute (looked like colored 4bpp blocks).

6. **Proven-working image controls (video+OSD mode):** PWM backlight (needs `FD42=0x03` pin-mux +
   FD20/21/28/29 + FD1F), display on/off (backlight 0), video brightness/contrast/saturation
   (FFD4/FFD3/FFD6 — safe decoder params on the live video). The driver `Amt630a` lives in
   `components/modesp_osd` + `modules/display` (`Amt630aPort`, `CONFIG_MODESP_DISPLAY_AMT630A`).

---

## 1. EXECUTIVE ANSWERS — the two stuck problems, solved up front

### (a) No-signal control: snow / blue / black / standby

**RESOLVED — the firmware's backdrop path is bank `FFxx` (I2C `0x5A`):** `FFB0` (snow on/off),
`FFCE/CF/D0` (backdrop YCbCr), `FFDA` (snow level), with `FFD2` left at `4Fh` (show path).
Citations: ENGELS:5642-5686, ENGELS:26557-26580, CLEAN:3044-3081.

> **Bench note (stand_s3, 2026-06-15/16):** `FEDC` (bank `0x59`) ALSO changes the backdrop on this board
> — bench-confirmed `FEDC=0x40` (bit6) = BLACK, `0x20` (bit5) = snow (CLEAN flags bit4/6=backdrop,
> bit5=no-signal; bit→color is revision-dependent — bench got BLACK from bit6, not the design-doc's
> "blue"). So the `FEDC` route is NOT wrong — it works. BUT it proved **sticky** (couldn't restore live
> video → the driver moved to `FFD2`), and `FEDC` bit4,5 doubles as the input-mux (so `0x20`/snow also
> selects AV3). The `FFB0/FFCE-D0` path is **collision-free and reversible** and is the ONLY way to get
> BLUE or arbitrary backdrop color (which `FFD2` 4F/54 cannot) — prefer it for new code. See §0.

First, **once at init**, suppress the chip's autonomous gray-snow so YOU control the mode:
```text
amt_w(0x59,0xD5,0xB1)             # FED5 sensitivity = B1h  (OEM default; 0x00 re-enables HW auto-snow)
```
Then select the mode (all values opcode-proven):
```text
# (1) LIVE AV VIDEO
amt_rmw(0x5A,0xB0,0x7F,0x00)      # FFB0 snow off (RMW, keep bit5)
amt_w(0x5A,0xD2,0x4F)             # show AV/backdrop/snow
amt_rmw(0x59,0xD7,0xFF,0x18)      # FED7 AV video enable bit3,4

# (2) BLUE  (ENGELS:5642-5658)
amt_w(0x5A,0xB0,0x20)             # snow off, bit5=1
amt_w(0x5A,0xCE,0x13); amt_w(0x5A,0xCF,0xDD); amt_w(0x5A,0xD0,0x72)   # YCbCr = blue
amt_w(0x5A,0xD2,0x4F)

# (3) BLUE + SNOW  (apply_backdrop combo, CLEAN:3044-3081)
amt_w(0x5A,0xB0,0x20)             # snow off first
amt_w(0x5A,0xCE,0x13); amt_w(0x5A,0xCF,0xDD); amt_w(0x5A,0xD0,0x72)
amt_w(0x5A,0xDA,0x6C)             # snow level
amt_w(0x5A,0xB0,0xA0)             # SNOW ON (bit7|bit5)
amt_w(0x5A,0xD2,0x4F)

# (4) BLACK  (ENGELS:5672-5686)
amt_w(0x5A,0xB0,0x20)
amt_w(0x5A,0xCE,0x00); amt_w(0x5A,0xCF,0x80); amt_w(0x5A,0xD0,0x80)   # YCbCr = black
amt_w(0x5A,0xD2,0x4F)

# (5) BLACK + SNOW  (ENGELS lcd_display_snow exactly, ENGELS:26557-26580)
amt_w(0x5A,0xB0,0x20)
amt_w(0x5A,0xCE,0x00); amt_w(0x5A,0xCF,0x80); amt_w(0x5A,0xD0,0x80)
amt_w(0x5A,0xB0,0xA0)             # SNOW ON
amt_w(0x5A,0xDA,0x6C)
amt_w(0x5A,0xD2,0x4F)

# (6) STANDBY / off  (safe external — no PLL / no panel-SPI)
amt_w(0x5A,0xD2,0x54)             # flat panel black (firmware xlat r7=06h -> 54h)
amt_w(0x58,0x1F,0x00)             # PWM0 backlight off (FD1F)
# optional: amt_rmw(0x59,0xD7,0xE7,0x00)   # AV video off (clear bit3,4)
# wake: amt_w(0x58,0x1F,0x01); amt_w(0x5A,0xD2,0x4F); amt_rmw(0x59,0xD7,0xFF,0x18)
```

**Key facts** — in modes 2-5, `FFD2` stays `4Fh` (show path); the backdrop color/snow regs do the
work, NOT `FFD2 5xh`. `FFD2` does **not** affect the OSD layer (ENGELS:925), so `54h` = "black screen,
OSD/menu still visible". `FFB0` init = `22h`; firmware always preserves the low bits via RMW
(`(old&0x7F)|0x20` snow-off, `old|0xA0` snow-on). BLUE backdrop YCbCr = `13h,DDh,72h` and BLACK/SNOW =
`00h,80h,80h` are **opcode-proven** (ENGELS:5651-5657 / 5680-5686 / 26565-26571), not just datasheet.

> **Re-assert caveat:** the OEM firmware keeps running and may re-apply its own configured no-signal
> mode on the next `FE26.bit1` signal edge (via its IRQ/input_selector). To stay deterministic, the
> ESP32 should poll `FE26.bit1` and re-assert these registers on transitions.

### (b) Video-input switching (CVBS AV1 / AV2 / AV3)

**RESOLVED for the KOZHAN family (OLD-firmware branch).** Only **AV1→CVBS1** and **AV3→CVBS3** are
real usable inputs; **AV2** is a junk arm that only ghosts CVBS3. The select control is bank `FExx`
(`0x59`): `FED7` (video on/off), `FED8` (bits6,7 = mux hi), `FEDC` (bits4,5 = mux lo). The proven
write order is **FED7(off) → FED8 → FED7(on) → FEDC**. Source: ENGELS:22406-22517.

| Input | FED8 bits6,7 | FEDC bits4,5 | Physical | Lock bit |
|---|---|---|---|---|
| **AV1** | `2` (10b) | `0` (00b) | **CVBS1** | `FE2A` bit4 (HaveCVBS1) |
| **AV3** | `0` (00b) | `2` (10b) | **CVBS3** | `FE2A` bit6 (HaveCVBS3) |
| AV2 | `2` | `3` (11b) | ghosts CVBS3 (junk) | — |

Copy-paste select **AV1 / CVBS1** (shadow-RMW, opcode-proven OLD-fw):
```text
amt_rmw(0x59,0xD7,0xE7,0x00)      # FED7 video OFF (clear bit3,4)             ENGELS:22417
amt_rmw(0x59,0xD8,0xFF,0x80)      # FED8 set bit7                             ENGELS:22422
amt_rmw(0x59,0xD8,0xBF,0x00)      # FED8 clear bit6  => bits6,7 = 2           ENGELS:22425
amt_rmw(0x59,0xD7,0xFF,0x18)      # FED7 video ON (set bit3,4)                ENGELS:22432
amt_rmw(0x59,0xDC,0xDF,0x00)      # FEDC clear bit5                           ENGELS:22437
amt_rmw(0x59,0xDC,0xEF,0x00)      # FEDC clear bit4  => bits4,5 = 0           ENGELS:22490
delay_ms(10); amt_r(0x59,0x2A,&s); locked = (s & 0x10)    # bit4 = HaveCVBS1
```
Select **AV3 / CVBS3** (note FED8 is a single 1-step clear of bits6,7; verify bit6 of FE2A):
```text
amt_rmw(0x59,0xD7,0xE7,0x00)      # FED7 video OFF
amt_rmw(0x59,0xD8,0x3F,0x00)      # FED8 clear bits6,7 together (=0)          ENGELS:9D1A
amt_rmw(0x59,0xD7,0xFF,0x18)      # FED7 video ON
amt_rmw(0x59,0xDC,0xFF,0x20)      # FEDC set bit5
amt_rmw(0x59,0xDC,0xEF,0x00)      # FEDC clear bit4  => bits4,5 = 2
delay_ms(10); amt_r(0x59,0x2A,&s); locked = (s & 0x40)    # bit6 = HaveCVBS3
```

> **FIRMWARE-VERSION RISK:** the values above are PROVEN only for the OLD-firmware arm (the only arm
> ENGELS assembled with opcodes). Post-2017 firmware **SWAPS AV1↔AV3** (CLEAN:3638/3640). The KOZHAN
> firmware version is unconfirmed — if AV1/AV3 come out swapped on the bench, exchange the AV1 and
> AV3 register values.

### Top-5 actionable fixes for the two stuck problems

1. **`set_backdrop()` is fundamentally wrong** — it writes only `FFD2` (4F/54), which can produce
   neither BLUE nor SNOW and gets clobbered by the firmware's `xlat_r7_to_forced_blank_color` on
   every display on/off + signal transition. Replace it with the bank-`0x5A` `FFB0/FFCE-D0/FFDA`
   method above (5 modes), and re-assert on `FE26.bit1` edges.
2. **`select_input()` mislabels AV2/AV3 and uses absolute writes** — its `n=1` actually encodes
   AV3/CVBS3 (not "AV2"), and its absolute `FED7` writes (`0xE4`/`0xFC`) stomp the SNOW-disable
   bits 0,1,6,7. Use the shadow-RMW `&0xE7`/`|0x18` sequence above (the FED8 path already RMWs).
3. **`set_backdrop` uses the wrong register entirely** for snow/blue — `FEDC` is the input-select
   register, not the no-signal backdrop. No-signal backdrop = bank `0x5A` `FFB0/FFCE-D0/FFDA`.
4. **`set_backlight()` is missing `FD42 |= 0x03` pin-mux** (P35→PWM0) — without it the backlight
   may not respond at all; and `FD1F` should be RMW-OR (`|= 0x01`), not an absolute `0x03` write.
5. **`set_saturation()` uses min `0x10`; firmware-fixed min is `0x00`** — change the map to
   `map_pct(pct, 0x00, 0x60)`.

---

## 2. Consolidated register map (all banks)

`amt_w(dev7, reg, val)`. R = readable status, W = write control, R/W = both.

### FDxx — Global (I2C `0x58`)
| MCU | reg | name | role | dir | proof |
|---|---|---|---|---|---|
| FD00 | 0x00 | RSTN_REG | `5Ah` = soft reset | W | datasheet_fitz.txt:688-694 |
| FD01 | 0x01 | chip_en | bit0: 0=power-down, 1=normal | R/W | datasheet_fitz.txt:695-700; **DANGER** PLL |
| FD18 | 0x18 | PLL_18h | **bit7 = ForceMaxBacklight (DANGER, keep 0)** | R/W | ENGELS:587 |
| FD1F | 0x1F | PWM_enable | bit0 = PWM0 (backlight), bit1 = PWM1 (volume) | R/W | ENGELS:595, CLEAN:3195/3251 |
| FD20/21 | 0x20/21 | PWM0 total LSB/MSB | period | W | ENGELS:596-597 |
| FD28/29 | 0x28/29 | PWM0 high LSB/MSB | on-time = duty | W | ENGELS:604-605 |
| FD22/23 | 0x22/23 | PWM1 total | volume period | W | ENGELS:598-599 |
| FD2A/2B | 0x2A/2B | PWM1 high | volume duty | W | ENGELS:606-607 |
| FD42 | 0x42 | PIN_P35_P36_pwm | OR 0x03 → P35/PWM0; OR 0x30 → P36/PWM1 | R/W | ENGELS:631 |
| FD11/12/13 | 0x11/12/13 | PLL regs | **DANGER** (hang/freeze) | R/W | ENGELS:11146; CLEAN:3737-3793 |
| FD0E/19/1A | 0x0E/19/1A | PLL regs | **DANGER** | W | ENGELS:11146-11155 |
| FD32/33 | 0x32/33 | SPI-flash pins | **DANGER** | W | ENGELS:11393-11394 |
| FDD0/DE/E0 | 0xD0/DE/E0 | SPI transfer/DMA | **DANGER** | W | ENGELS:28622 |
| FDDF | 0xDF | SPI status | bit7 = busy (R) | R | ENGELS:28622 |
| FD40/41 | 0x40/41 | KillTft/StopDotClk | **DANGER** | W | sec-E |

### FExx — AV Decoder (I2C `0x59`)
| MCU | reg | name | role | dir | proof |
|---|---|---|---|---|---|
| FE01 | 0x01 | AV_ctrl | bit0 = ForcePALcolors | R/W | ENGELS:711 |
| FE15 | 0x15 | ctrl_sensitivity_0 | 00=max,05=med,09=low | W | ENGELS:723 |
| FE26 | 0x26 | stat_detect_0 | bit1=signal (loose), (FE26&0x06)==0x06 = strict valid | R | ENGELS:725, 1985 |
| FE27 | 0x27 | stat_detect_1 | bit0 = secondary detect hint | R | ENGELS:726, 1579 |
| FE28 | 0x28 | stat_framerate_flag | bit2: 0=NTSC/60, 1=PAL/50 | R | ENGELS:727, 1995 |
| FE2A | 0x2A | stat_signal_detect | bit4=HaveCVBS1, bit6=HaveCVBS3 | R | ENGELS:729, 2330 |
| FEAA/AB | 0xAA/AB | stat_sensitivity MSB/LSB | 0xFFF=none, ~0x287=locked | R | ENGELS:796, 1507 |
| FED5 | 0xD5 | ctrl_sensitivity_1 | init `B1h` (suppress auto-snow); 00h = HW gray-snow | W | ENGELS:830, 11159 |
| FED7 | 0xD7 | AV_video_on_off | **bit3,4 = video enable** (mask 0x18); bit0,1,6,7 = SNOW-disable (preserve) | R/W | ENGELS:835 |
| FED8 | 0xD8 | input_select_reg_0 | **bit6,7 = mux hi** | R/W | ENGELS:836 |
| FEDC | 0xDC | input_select_reg_1 | **bit4,5 = mux lo** | R/W | ENGELS:840 |

### FFxx — Video-Process / GAMMA (I2C `0x5A`)
| MCU | reg | name | role | dir | proof |
|---|---|---|---|---|---|
| FFB0 | 0xB0 | snow_enable_and_misc | **bit7 = SNOW on/off**, bit5 forced 1; init `22h` | R/W | ENGELS:11164; CLEAN:700 |
| FFCE/CF/D0 | 0xCE/CF/D0 | backdrop Y/Cb/Cr | backdrop color | R/W | CLEAN:713-715; ENGELS:5650-5686 |
| FFD2 | 0xD2 | forced_blank_color | `4Fh`=show; `50h..55h`=flat blank; does NOT touch OSD | R/W | CLEAN:717; ENGELS:925 |
| FFD3 | 0xD3 | contrast | med `0x7E`, min `0x56`, max `0xA6` | R/W | ENGELS:926, 11373 |
| FFD4 | 0xD4 | brightness | med `0x8E`, min `0x66`, max `0xB6` | R/W | ENGELS:927, 11373 |
| FFD5 | 0xD5 | tint | NTSC only; **bit7 smashes PAL** (force 0 on PAL) | R/W | ENGELS:928; CLEAN:2806 |
| FFD6 | 0xD6 | saturation | med `0x38`, **min `0x00`**, max `0x60` | R/W | ENGELS:929, 12617 |
| FFDA | 0xDA | backdrop_snow_level | snow density, `6Ch` | R/W | CLEAN:725; ENGELS:B48C |
| FF01..1F | 0x01..1F | gamma R (31 bytes) | gamma ramp | W | ENGELS:853 |
| FF20..3E | 0x20..3E | gamma G (31 bytes) | gamma ramp | W | ENGELS:854 |
| FF3F..5D | 0x3F..5D | gamma B (31 bytes) | gamma ramp | W | ENGELS:855 |
| FFF0..FB | 0xF0..FB | YUV color-decode matrix (12 bytes) | color matrix | W | ENGELS:938-949 |

### FBxx — OSD (I2C `0x5B`)
| MCU | reg | name | role | proof |
|---|---|---|---|---|
| FB00 | 0x00 | bgmap_addr_lsb | cell-index LSB, **auto-inc** on data write | ENGELS:313 |
| FB01 | 0x01 | bgmap_data_lsb | tile-index LSB (commits cell) | ENGELS:314 |
| FB02 | 0x02 | font_addr_lsb | font-RAM word addr LSB | ENGELS:315 |
| FB03 | 0x03 | font_data LOW | data LOW byte (written **LAST = LATCH**) | ENGELS:316 + code 78F1 |
| FB04 | 0x04 | font_data HIGH | data HIGH byte (written **FIRST**) | ENGELS:317 + code 78DC |
| FB05 | 0x05 | window_enable_bits | bit0-4 enable win0-4; bit6 hide-text; bit7 bitmap-on | ENGELS:318 |
| FB06 | 0x06 | misc_transp_enable | 0xC0 = semi-transp on, 0x00 = opaque | CLEAN:4918 |
| FB07/08 | 0x07/08 | win0 size_x (cells) / size_y (rows) | — | — |
| FB09 | 0x09 | win0_xyloc_msb | bit0-2 xloc.msb, bit4-6 yloc.msb, bit7 not R/W | CLEAN:4704/4752 |
| FB0A/0B | 0x0A/0B | win0 xloc_lsb / yloc_lsb (pixels) | — | — |
| FB0C | 0x0C | bright_transp_level | upper3 brightness, lower3 transparency (init 0x80) | CLEAN:3896 |
| FB0D | 0x0D | bgmap_addr_msb | **does NOT auto-carry** (manual on wrap); bit1-7 not R/W | ENGELS:329 |
| FB0E | 0x0E | bgmap_data_msb | tile-index MSB (only bit0-1 R/W → 10-bit index) | ENGELS:330 |
| FB0F | 0x0F | font_addr_msb | bit4-7 not R/W (mask 0x0F) | ENGELS:331 |
| FB10 | 0x10 | bgmap_data_attr | per-cell FG/BG attribute; bit7 not R/W | ENGELS:332 |
| FB11 | 0x11 | bitmap_start_lsb | TEXT/BITMAP boundary LSB (relative to 0x1C0) | ENGELS:333 |
| FB12-17 | | win1 (size_x/y, xyloc_msb, xloc, yloc, vramaddr_lsb=FB17) | — | — |
| FB18-1D | | win2 (vramaddr_lsb=FB1D, its MSB = FB1A.bit7) | — | — |
| FB1E-23 | | win3 (vramaddr_lsb=FB23) | — | — |
| FB24-29 | | win4 (vramaddr_lsb=FB29) | — | — |
| FB32 | 0x32 | win0_scale | 2×2bit ScaleX(lo2)/ScaleY(hi2); bit4-7 not R/W | ENGELS:8E2B |
| FB33 | 0x33 | win1_and_2_scale | lo-nibble win1, hi-nibble win2 (**OEM bug, see §06.8**) | ENGELS:8E87 |
| FB34 | 0x34 | win3_and_4_scale | lo-nibble win3, hi-nibble win4 (**OEM bug**) | ENGELS:8EBC |
| FB35 | 0x35 | bitmap_transp_misc | bit3/5 nudge, bit4 bitmap color0 transp | ENGELS:11266 |
| FB56-61 | | 6-color text palette color1..6 RGB444 (msb,lsb pairs) | — | ENGELS:377-388 |
| FB70 | 0x70 | bitmap_start_msb | boundary MSB (bit2-7 not R/W → mask 0x03) | ENGELS:404 |
| FB76/77 | 0x76/77 | char_xsiz / char_ysiz (pixels) | glyph size (OEM 12×16) | ENGELS:410-411 |
| FB78 | 0x78 | xyflip | b4 TileXflip, b5 MapXflip, b6 TileYflip, b7 MapYflip | ENGELS:412 |
| FB89 | 0x89 | screen_position | global OSD shift (fixed 0) | — |

### FCxx — Tcon / Scaler (I2C `0x5C`) — **DO NOT TOUCH** (whole bank panel-critical)
| MCU | reg | role | proof |
|---|---|---|---|
| FC90 | 0x90 | VIDEO_control bit0 = use 60Hz vertical timing (init-time ratio only) | ENGELS:464 |
| FC96/97, FCC2/C3 | | aspect-ratio scale factors (init-time only) | ENGELS:11353-61 |
| (all others) | | Tcon timing — never write | ENGELS:11187-11264 |

---

## 3. Section 08 — Datasheet cross-reference & contradiction audit

**Datasheet:** ARKMICRO *AMT630A Video Display Controller — Product Specification* V1.1, 2014.10
(CONFIDENTIAL; `datasheet_fitz.txt:21-22,50-52` — page footers misprint "V1.0").

> **SCOPE:** our copy is a **9-page condensed preview**, not the full spec. The TOC
> (`datasheet_fitz.txt:107-116`) advertises full register tables in §6.1-6.10 (pages 9-79), but the
> register body is **absent** — content stops inside §6.1 Global at register `0x02`
> (`datasheet_fitz.txt:676-711`). The datasheet contributes the **address map, pinout, block diagram,
> and feature list**; for every bit-field the **disassembly is authoritative** and nothing in the
> datasheet contradicts it.

### (a) Vendor's own Rosetta (datasheet_fitz.txt:107-116)
The TOC headings state, per block, **both** the I2C 8-bit write address and the MCU XRAM page:

| § | Vendor block | I2C(8-bit) | MCU page | cite | Rosetta match |
|---|---|---|---|---|---|
| 6.1 | Global Register | 0xB0 | 0xFDXX | datasheet_fitz.txt:107 | FDxx=0x58/0xB0 ✓ |
| 6.2 | Tcon Register | 0xB8 | 0xFCXX | datasheet_fitz.txt:108 | FCxx=0x5C/0xB8 ✓ |
| 6.3 | Decoder Register | 0xB2 | 0xFEXX | datasheet_fitz.txt:109 | FExx=0x59/0xB2 ✓ |
| 6.4 | Video Process Register | 0xB4 | 0xFFXX | datasheet_fitz.txt:110 | FFxx=0x5A/0xB4 ✓ |
| 6.5 | GAMMA Register | 0xB4 | 0xFFXX | datasheet_fitz.txt:111 | shares FFxx/0xB4 ✓ |
| 6.6 | RCRT Register | **MCU only** | 0xFFXX | datasheet_fitz.txt:112 | NOT I2C-reachable ✓ |
| 6.7 | Scaler Register | 0xB8 | 0xFCXX | datasheet_fitz.txt:113 | shares FCxx/0xB8 ✓ |
| 6.8 | OSD (设备地址B6, 0XFBXX) | 0xB6 | 0xFBXX | datasheet_fitz.txt:114 | FBxx=0x5B/0xB6 ✓ |
| 6.9 | SPI Register | 0xB0 | 0xFDXX | datasheet_fitz.txt:115 | shares FDxx/0xB0 ✓ |
| 6.10 | 12-bit ADC Register | 0xB0 | 0xFDXX | datasheet_fitz.txt:116 | shares FDxx/0xB0 ✓ |

PROVEN: the five video pages and their I2C addresses are vendor-stated; our Rosetta map is correct.
The **0xBE/0x5F SFR-unlock channel is NOT in the TOC** (disassembly-only / INFERRED). §6.6 RCRT is
"MCU access only" — do not expect those FFxx registers to work over I2C.

### (b) The only registers actually defined in the body — §6.1 Global (datasheet_fitz.txt:682-711)
| MCU | Reset | Bits | Vendor name | Description | I2C form |
|---|---|---|---|---|---|
| FD00 | 00h | [7:0] | **RSTN_REG** | `5Ah` = Soft reset; else no action | `amt_w(0x58,0x00,0x5A)` |
| FD01 | 01h | [0] | **chip_en** | `0`=power-down BK/ADC + gate CLK; `1`=normal | `amt_w(0x58,0x01,v|1)` |
| FD01 | 01h | [7:1] | reserved | usable as scratch variable reg | — |

### (c) Feature/capability constants (datasheet_fitz.txt:148-214)
- **3 CVBS analog inputs** (153); **9-bit 1-ch video ADC** (155); **27 MHz** single crystal (156).
- **OSD:** 512-char font ROM; dynamic font RAM **4096×16 bytes**; glyphs up to **24×32**;
  **16-colour palette**; **5 OSD windows**; 16-colour bitmap; blending/blink/highlight (180-191).
  Confirms FB-bank model (5 windows = `FB05` bit0-4, ENGELS:318).
- **4× 16-bit PWM** (204); separate **built-in 12-bit ADC** (201, distinct from 9-bit video ADC);
  LDO 1.2V core; 3.3V only; LQFP-64; ITU/CCIR-656 8/10-bit out.

### (d) No hard contradictions — gaps + trust recommendation
Trust the disassembly for bit-fields/values; datasheet for naming + address map + pinout.
1. TOC typo: two sections numbered "6.7" (Scaler + OSD); OSD is logically §6.8. No technical impact.
2. FED7/FED8/FEDC bits: datasheet §6.3 body missing; disasm fully defines them. Trust disasm.
3. FFB0/FFD2/FFCE-D0/FFDA bits: §6.4 body missing; disasm: FFB0 init=22h (ENGELS:11164,908),
   FFD2 4Fh=show/5xh=blank (925), FFCE-D0 BLUE=13/DD/72 & BLACK=00/80/80 (921-923), FFDA 6Ch (933).
4. 0xBE/0x5F SFR-unlock: not in TOC; keep INFERRED-from-firmware.
5. FB-bank OSD detail: datasheet gives only capacities; addresses/bits only in disasm.
6. FFxx I2C reachability (datasheet ADDS info): §6.4/6.5 = I2C-OK, but §6.6 RCRT = MCU-only —
   bench-verify any NEW FFxx register.
7. FCxx (0xB8) do-not-touch: datasheet puts BOTH Tcon (§6.2) and Scaler (§6.7) on 0xB8/FCXX —
   explains why that page is panel-critical. Keep the rule.

### (e) Pinout / CVBS pin mapping (datasheet_fitz.txt:402-674, LQFP-64)
| Pin | PAD | Function | cite |
|---|---|---|---|
| 1 | **CVBS1** | Composite video in 1 | 407-409 |
| 2 | **CVBS2** | Composite video in 2 | 411-412 |
| 3 | **CVBS3** | Composite video in 3 | 413-415 |
| 14/15 | XTAL_OUT/IN | 27 MHz crystal | 450-455 |
| 17-20 | SPI_CS/SI/SO/CLK | SPI-flash | 459-474 |
| 55-58 | DC_PWM0..3 (P35/36/37/07) | 4× PWM (backlight) | 631-646 |
| 60 | **P04/TXD/SDA** | **I2C SDA** | 652-656 |
| 61 | **P05/RXD/SCL** | **I2C SCL** | 657-661 |
| 63 | RESET | chip reset | 666-670 |

Block diagram: CVBS1/2/3 → Video Mux → Video Decoder → Scaler → Video-Process → YCbCr_to_RGB → OSD
→ GAMMA → TCON. Firmware `apply_av_input_r7` labels AV1=CVBS1, AV3=CVBS3 (ENGELS:22396,22398); the
KOZHAN board assembled the OLD branch (`tech_version=ver07mar2016 < ver11sep2017`, ENGELS:31,33,48).
Soft reset: `amt_w(0x58,0x00,0x5A)`. chip_en normal: ensure `amt_w(0x58,0x01,…|0x01)`.

---

## 4. Section 03 — Initialization & boot-over-OEM sequence

> Banks: FB=0x5B(OSD), FC=0x5C(LCD/Tcon DANGER), FD=0x58(Global/PWM/PLL/SPI), FE=0x59(AV),
> FF=0x5A(video-proc), SFR-window=0x5F.

### A. What the OEM already does at power-on (do NOT redo)
**Boot chain (ENGELS):** `0000h jmp reset_entrypoint` (ENGELS:1235) → `reset_entrypoint @98CAh`
(zerofill IRAM, set SP, RAM preset) → `jmp main_function @9A65h` (ENGELS:21599) → `call main_init
@BC18h` (ENGELS:21996). CLEAN variant additionally zerofills XRAM 0000h..07FFh (CLEAN:3532-3538),
snapshots `xram_initial_hw_regs` over FB00h..FFFFh (CLEAN:3553-3564), forces backlight+volume OFF
(CLEAN:3565-3566) before main_init.

**main_init = 5 parts** (ENGELS:28013-28018):
- part_1 (ENGELS:28488): `init_SFR_IO_memory_system` r7=00h → `or [C6h],80h` set bit7
  (ENGELS:1253), then `FD0E=20h` (ENGELS:28492-28494).
- part_2 (@005Eh): watchdog_disable → init_lcd_pins_and_force_display_off → timers → 57600 baud →
  watchdog_enable.
- part_3 (@B956h): detect_flash_chiptype, load settings from SPI-flash.
- part_4 (ENGELS:17106): **the big one** → output extra+huge io-lists → OSD VRAM allocation →
  `init_OSD_hardware` (17109) → init_ADC_analog_hardware (17113) → `init_AV_stuff` (17115) →
  no-signal handling + apply_settings_to_IO_ports_except_backlight.
- part_5 (ENGELS:28018): display_spi_reset/detect/init = **physical LCD panel SPI bring-up
  (NV3035C/HX8238). External I2C cannot and must not redo this.**

**Master register dump = `huge_fixed_io_list`** (ENGELS:11048; KOZHAN takes OLD branch, ENGELS:11049).
OEM already loads: PLL FD0A=2Bh/FD0B=40h/FD0D=F0h/FD0F=03h/FD10=04h/FD14=03h/FD15=02h/FD16=03h/
FD1A=08h/FD19=83h (opcodes ENGELS:11146-11155); AV FE54=00h/FE83=FEh/FED5=B1h (ENGELS:11157-11159);
video FFB0=22h/FFB1=0Fh/FFB2-B4=20h/FFB7=90h/FFCB=2Ah/FFF0..FB YUV/FF00=03h + RGB gamma ramps
FF01..1F/FF20..3E/FF3F..5D (ENGELS:11164-11307); all FCxx Tcon timing (ENGELS:11187-11264);
OSD FB35=00h/FB89=00h (ENGELS:11266-11267).

**`init_AV_stuff`** (ENGELS:25703): RMW FED7|=1Ch (ENGELS:25706), FED8|=1Bh then &=0DBh
(25710/25713), FED9&=0CFh|=40h (25717/25720), FEDB&=7Fh (25724), FE01|=10h (25728), FE04=30h
(25731), FE05=40h (25734).

**`init_OSD_hardware`** (ENGELS:23483): disable all 5 windows (FB05&=0E0h, 28613-28617),
zerofill VRAM, `osd_init_six_colors` (FB56..FB61 palette, ENGELS:24809-24845 — **PROVES palette
writable**), char size FB76=0Ch/FB77=10h (28595-28602), bitmap start FB11=28h/FB70=00h
(28527-28536), brightness=07h (23497, comment warns 7 too bright), then font upload.

### B. Minimal Path-A init over running OEM
We do NOT call init_OSD_hardware (OEM did). The existing `apply_osd_init()` (amt630a.cpp:124) is
CORRECT minimal Path-A entry. Sequence:
```
1. present(): probe 0x5B ACK; no ACK -> renderer off, system survives.
2. (optional) 0x5F unlock — insurance only (toggles OEM watchdog); NOT needed for OSD.
3. amt_w(0x5B,0x05,0x1F)        ; OSD windows 0..4 on (FB05 bit0-4, RMW preserves bit5-7)
4. (once) font: 0x5B,0x05,0x00 -> upload FONT RAM 1C0h+ -> 0x5B,0x05,mask
5. amt_w(0x5B,0x76,xsiz); amt_w(0x5B,0x77,ysiz)   ; FB76=xsiz, FB77=ysiz (OEM default 12x16)
6. window geom: FB07/08/09/0A/0B
7. backlight: FD20/21 total, FD28/29 high, FD1F=0x01 (total >= ~0x0100; 00FFh = OFF)
8. input: FED7->FED8->FED7->FEDC shadow sequence (0x59)
9. render: FB0D/00 addr + FB0E/01 tile + FB10 attr
```

**Unlock handshake = OPTIONAL.** `0x5F` is a vendor SFR-access window. The FIZIK epilogue maps to:
- `0x5F,0xBE,0x55 / 0x5F,0xBA,0x00 / 0x5F,0xBE,0xAA` = **watchdog_disable** (BE96h): unlock SFR
  BEh=55h, set BAh=00h (=OFF), re-lock BEh=AAh (opcodes ENGELS:28573-28576). Pauses the running OEM
  watchdog during a long config burst. NOTE: BAh=00h = DISABLE, BAh=01h = ENABLE (watchdog_enable
  @BC68h writes BA=01h, ENGELS:28085).
- `0x5F,0xC6,0x42 / 0x5F,0xC6,0x00` = memory_system SFR 0C6h literal writes. OEM internally toggles
  only bit7 (`or [C6h],80h` / `and [C6h],7Fh`, ENGELS:1253/1256) — the driver's literal 0x42/0x00 is
  NOT the same bit7-only RMW. Treat as insurance.

OSD registers (FBxx) are plain XRAM I/O written by the OEM with bare `movx` — **no unlock gate**. So
the AF/A1..A6/AF=11 prologue + BE/BA epilogue are insurance, not a prerequisite. Keep them (harmless)
but they are not required to draw OSD.

### C. Screen on/off + forced-blank
- `switch_lcd_screen_on` (CLEAN:3729): panel SPI wake (NV3035C R00=03h, HX8238 R03=7664h) +
  FD0E=2Ch (3737-3739), FED5=B1h (3740-3742), FD11/12/13=FFh (3743-3749), FED7|=03h (3750-3753),
  resync pulse with delays (3757-3766).
- `switch_lcd_screen_off` (CLEAN:3771): panel standby + FD0E=20h (3779-3781), FED5=00h (3782-3784),
  FD11=0Fh (3785-3787), FD12=18h (3788-3790), FD13=00h (3791-3793), FED7&=0FCh (3794-3797) — exact
  PLL values, copy verbatim, never improvise.
- `xlat_r7_to_forced_blank_color` (CLEAN:3802) writes FFD2 from @@blank_color_list (CLEAN:3815-3822):
  r7=0→4Fh (show video+OSD), 1→50h(red), 2→51h(green), 3→52h(blue), 4→53h(dgray), 5→55h(lgray),
  6→54h(black) — note firmware swaps the 5/6 entries. **FFD2 does NOT affect OSD** (ENGELS:925) →
  54h = "black screen, OSD/menu still visible". The existing `set_backdrop` use of FFD2 4Fh/54h is
  CORRECT for display on/off (but cannot do blue/snow — see §06.1).

### D. Existing driver verdict
- `apply_osd_init()` (amt630a.cpp:124): CORRECT minimal Path-A entry. The trailing
  `0x5F,0xBE,0x55 / 0x5F,0xBA,0x00 / 0x5F,0xBE,0xAA` is the watchdog-disable (pause) bracket.
- `apply_init_table()` kInit (amt630a.cpp:97-103): essential = FD42/FD1F/FD28-29/FED7/FFD2=54/FB05;
  DANGER-if-mixed = FD11/12/13 — the driver writes FD11=1Fh/FD12=38h/FD13=00h, which is NEITHER the
  CLEAN screen-off set (0Fh/18h/00h) NOR the screen-on set (FFh/FFh/FFh). Use one known-good PLL set
  verbatim; do not improvise.
- `apply_init_table()` kOn (~60 AV-decoder + gamma writes): mostly **cargo-cult for Path A** — OEM
  already loaded equivalents; resending does an off→on of the video path and can disturb the working
  OEM picture. Only the trailing `0x5B,0x05,0x1F` is must-have. Use kOn ONLY if OEM shows no video.
- `is_danger()` guard (amt630a.cpp:66-76): covers MOST of the proven DANGER set but is NARROWER —
  it does NOT block FD13, FD0E, FD19, FD1A (PLL hang) or FDDF (SPI status). Also kInit/kOn use
  `raw_w()` (amt630a.cpp:119,121) which BYPASSES the guard. Keep the guard; consider extending it to
  FD0E/13/19/1A/DDF.

### E. DANGER registers (proven)
FCxx whole bank (0x5C, all Tcon timing) NEVER write; FD32/FD33 SPI-flash pins; FDD0/FDDE/FDE0 SPI
transfer/DMA, FDDF=SPI status; FD11/12/13/19/1A/0E PLL (hang/freeze, copy exact CLEAN:3737-3793);
FD40/41 KillTft/StopDotClk; FD18.bit7 ForceMaxBacklight; FFD5.bit7 PAL color; SFR C6h via 0x5F only
in unlock order. PWM "00FFh fails" proven at ENGELS:26323-26324 (total >= 0x0100).

---

## 5. Section 02 — Video input switching (CVBS AV1/AV2/AV3)

**Primary proof:** ENGELS `apply_av_input_r7` @22391 (addr `9CD6h`). Cross-check: CLEAN
`apply_av_input_a` @3628, `input_selector` @1551.

> **macro duplication:** `apply_av_input_r7` is emitted twice in ENGELS — at `9CD6h` (opcode-backed,
> power-on caller @04D8) and `994Bh` (standby caller @6AF7). Both byte-identical; cite `9CD6h`.

### Registers (bank FExx → I2C 0x59)
| MCU reg | I2C reg | Symbol | Role | Proof |
|---|---|---|---|---|
| FED7 | 0xD7 | IO_AV_video_on_off | bit3,4 = video path on/off (mask 0x18); bit0,1,6,7 = SNOW disable (do NOT clobber) | ENGELS:835, @9CE5/@9CF5 |
| FED8 | 0xD8 | input_select_reg_0 | **bit6,7** = mux hi | ENGELS:836, @9CEA/@9CEE |
| FEDC | 0xDC | input_select_reg_1 | **bit4,5** = mux lo | ENGELS:840, @9CFC/@9D2C |
| FE2A | 0x2A | stat_signal_detect | R: bit4=HaveCVBS1, bit6=HaveCVBS3 | ENGELS:729 |
| FE26 | 0x26 | stat_detect_0 | R: bit1=signal (preferred for C64) | ENGELS:725, CLEAN:1571-1574 |
| FD12 | 0x12 | PLL_12h | bit2 = AV-PLL powered (gates detect) — **DANGER, read-only** | CLEAN:1552 |

### PROVEN per-input values (ENGELS OLD-firmware branch — the arm with opcodes)
| r7 | Input | FED7 bit3,4 | FED8 bit6,7 | FEDC bit4,5 | Physical | Proof |
|---|---|---|---|---|---|---|
| 0 | **AV1** | 0 then 3 | **2** (10b) | **0** (00b) | **CVBS1** | ENGELS:22415-22432 + 22437/22490 |
| 1 | AV2 | 0 then 3 | 2 (10b) | 3 (11b) | ghosts CVBS3 (junk) | ENGELS:22444-22458 + 22512/22515 |
| 2 | **AV3** | 0 then 3 | **0** (00b) | **2** (10b) | **CVBS3** | ENGELS:22461-22491 |
| else | Invalid | 0 then 3 | 3 (11b) | 3 (11b) | undefined | ENGELS:22494-22516 |

The two real usable inputs are **AV1→CVBS1** and **AV3→CVBS3**. CLEAN encodes the same via RMW:
`reg0 = (old & ~0xC0) | r0`, `reg1 = (old & ~0x30) | r1` with r0/r1 = AV1{0x80,0x00},
AV3{0x00,0x20} (CLEAN:3643-3675).

### Exact opcode decode (AV1, entry @9CE1h)
```
9CE5 54 E7   FED7 &= 0xE7      ; video OFF (clear bit3,4)
9CEA 44 80   FED8 |= 0x80      ; set bit7
9CEE 54 BF   FED8 &= 0xBF      ; clear bit6   => bits6,7 = 2
9CF5 44 18   FED7 |= 0x18      ; video ON (set bit3,4)
9CFC 54 DF   FEDC &= 0xDF      ; clear bit5  (written at 9D2A movx)
9D2C 54 EF   FEDC &= 0xEF      ; clear bit4   => bits4,5 = 0
```

### Ordering (resolves design-doc §12.2) — PROVEN
Register-write order is **FED7(off) → FED8(write) → FED7(on) → FEDC(write)**, confirmed for all four
arms. The FED7 off/on bracket around the FED8 write forces the AV decoder to re-lock; FEDC is written
last. RMW step count varies: FED8 AV1/AV2 = two single-bit steps; **AV3 = one step**
(`9D1A 54 3F`, clears bits6,7 together); Invalid = one step (`9D39 44 C0`). FEDC always two steps.

### Settle delay + re-detect — PROVEN
Power-on caller (ENGELS:2109-2126): select → `delay 10` (~8.9 ms) → check FE2A.bit6 → `delay 200`
(~178 ms full-lock). Timing: `slow_delay_r6r7` loops r7× calling `fast_delay_r6r7(400)` = 888 µs each
(ENGELS:27858/28320). r7=0x0A → ~8.9 ms, r7=0x1E → ~26.6 ms, r7=0xC8 → ~177.6 ms.

### Read-back
No hardware register exposes the mux selection (FED7/8/DC are write controls; firmware tracks the
active index in RAM `xram_curr_input`, CLEAN:1583-1590) → **keep a software shadow** of the requested
input. **Signal presence IS readable:** `amt_r(0x59,0x2A)` bit4=HaveCVBS1, bit6=HaveCVBS3. Caveat:
when AV-PLL is down (FD12.bit2==0) only FE2A.bit6 is valid (CLEAN:1556-1561) — do not write FD12
(DANGER). For C64-like sources the firmware prefers FE26.bit1 over FE2A.bit4 (CLEAN:1571-1574).

### Corrected shadow-RMW sequence (AV1, OLD-fw) — full
```
amt_r(0x59,0xD7,&t); amt_w(0x59,0xD7, t & 0xE7);   // video OFF
amt_r(0x59,0xD8,&t); amt_w(0x59,0xD8, t | 0x80);   // FED8 set bit7
amt_r(0x59,0xD8,&t); amt_w(0x59,0xD8, t & 0xBF);   // FED8 clear bit6  => 2
amt_r(0x59,0xD7,&t); amt_w(0x59,0xD7, t | 0x18);   // video ON
amt_r(0x59,0xDC,&t); amt_w(0x59,0xDC, t & 0xDF);   // FEDC clear bit5
amt_r(0x59,0xDC,&t); amt_w(0x59,0xDC, t & 0xEF);   // FEDC clear bit4  => 0
delay_ms(10); amt_r(0x59,0x2A,&s); locked = (s & 0x10);   // bit4=HaveCVBS1
```
AV3 differs: FED8 single `&= 0x3F`; FEDC `|= 0x20` then `&= 0xEF`; verify `s & 0x40` (bit6). If on
the bench AV1/AV3 are swapped, the board runs NEW-fw — exchange the AV1 and AV3 register values
(new-fw: AV1→CVBS3, AV3→CVBS1, CLEAN:3638/3640).

### Why current `select_input()` fails + fix (amt630a.cpp:336-350)
1. **Mislabels** — its `n=1` computes FED8 bits6,7=0 + FEDC=2, which is the firmware's **AV3/CVBS3**
   encoding, NOT "AV2". (AV2 would be FED8=2, FEDC=3.) It only exposes 2 of the 3 hardware inputs.
2. **Absolute FED7 writes** — it writes `FED7=0xE4` (off) then `FED7=0xFC` (on) as ABSOLUTE values,
   clobbering the SNOW-disable bits 0,1,6,7. Use RMW `&0xE7` (off) / `|0x18` (on) — executive §1b.
3. **FEDC written absolute** (`fedc_sel`), not RMW — overwrites unrelated bits. Use `(old & 0xCF) | bits`.
4. **No settle / re-detect** — add `delay_ms(10)` + `amt_r(0x59,0x2A)` lock check (AV1: bit4, AV3: bit6)
   after the switch. Currently only FED8 uses a shadow (`sh_fed8_`); extend shadow-RMW to FED7 and FEDC.

---

## 6. Section 01 — No-signal: snow / blue / black / standby

**Bank:** backdrop/snow/forced-blank all live in MCU bank `FFxx` = **I2C `0x5A`**. Exceptions: AV
video on/off `FED7` and auto-snow sensitivity `FED5` and signal-detect `FE26` = bank `FExx` (`0x59`).

### Register inventory (PROVEN)
| MCU | I2C | Symbol | Role | Proof |
|---|---|---|---|---|
| FFB0 | 0x5A:0xB0 | snow_enable_and_misc | **bit7=SNOW on/off**, bit5 forced 1; init `22h` | ENGELS:11164, CLEAN:700 |
| FFCE/CF/D0 | 0x5A:0xCE/CF/D0 | backdrop Y/Cb/Cr | backdrop color | ENGELS:5650-5658 (BLUE), 5679-5686 (BLACK) |
| FFD2 | 0x5A:0xD2 | forced_blank_color | `4Fh`=show; `50h..55h`=flat blank; does NOT touch OSD | CLEAN:717, ENGELS:A819-A848 |
| FFDA | 0x5A:0xDA | snow_level | snow density, `6Ch` | CLEAN:725, ENGELS:B48C |
| FED5 | 0x59:0xD5 | AV_ctrl_sensitivity_1 | init `B1h` (suppress auto-snow); `00h`=NTSC gray-snow | ENGELS:830, 11159, CLEAN:3740-3742 |
| FED7 | 0x59:0xD7 | AV_video_on_off | **bit3,4=video enable** | ENGELS:9CF5, 9CE5 |
| FE26 | 0x59:0x26 | stat_detect_0 | bit1=signal present (RO) | CLEAN:4604-4608 |

**FFD2 xlat table (ENGELS A819-A848 / CLEAN:3815-3822):** 4Fh=AV-on, 50h=red, 51h=green, 52h=blue,
53h=dgray, 54h=black, 55h=lgray. The dispatch is **index-swapped**: list index 5→55h (lgray),
index 6→54h (black). These are flat panel test-colors that ignore the backdrop regs and snow.

### How firmware draws each mode (authoritative writes)
- **BLUE** (ENGELS:5642-5658 / 17157-17173): FFB0=(old&7F)|20 (snow off), FFCE=13 FFCF=DD FFD0=72.
- **BLACK** (ENGELS:5672-5686 / 17130-17144): FFB0=(old&7F)|20, FFCE=00 FFCF=80 FFD0=80.
- **SNOW** = `lcd_display_snow` (ENGELS:26557-26580): FFB0&=7F; FFB0|=20; FFCE=00 FFCF=80 FFD0=80
  (black backdrop); FFB0|=80 (snow ON); FFDA=6C.
- **BLUE+SNOW** (apply_backdrop, CLEAN:3044-3081): snow on/off = config.bit1 → FFB0.7; FFDA always
  6Ch; backdrop color blue (13/DD/72) vs black (00/80/80) is an independent config bit — so
  blue-backdrop + snow-on is a valid firmware-supported combination.
- **STANDBY/off** (CLEAN:3678): backlight off → xlat r7=06h ⇒ FFD2=54h → switch_lcd_screen_off
  (PLL down, FED7&=FC) → volume off. **Full firmware standby touches PLL FD0E/11/12/13 + panel SPI
  (DANGER) — do NOT replicate externally.** Use the safe external standby (executive §1a mode 6).

In ALL of blue/snow/black, FFD2 stays `4Fh` (show path).

### Auto vs manual
The AV decoder auto-paints gray snow on sync-loss when sensitivity `FED5=00h`. The OEM sets
`FED5=B1h` (CLEAN:3740-3742) to suppress autonomous snow, then drives modes manually via its
IRQ/`input_selector` (CLEAN:1551) and `firm_check_signal` (CLEAN:4604) reading `FE26.bit1`. Because
OEM firmware keeps running, it may re-apply its mode on the next signal edge — to stay deterministic,
the ESP32 should poll `FE26.bit1` and re-assert the chosen registers on transitions.

> **mode-numbering NOTE:** the OEM menu enum (CLEAN:4287) is STANDBY=0, BLUE=1, BLUE+SNOW=2,
> BLACK+SNOW=3, but internal branch dispatchers (ENGELS:5632, 17123) use a different local code
> (snow=01, black=02). The register *values* are firmware-proven and build-independent; do not
> hardcode a no-signal mode *index*.

### Why current `set_backdrop` (amt630a.cpp:352-363) is wrong
It writes ONLY FFD2 (4F=show, 54=black). (1) **Cannot produce BLUE or SNOW** — those need
FFCE/CF/D0 + FFB0.7 + FFDA; this is the main gap. (2) FFD2=54h may be rewritten by firmware on display
on/off + signal transition via `xlat_r7_to_forced_blank_color`, so re-assert it on `FE26.bit1` edges.
(3) The earlier `FEDC=0x40`=black approach was bench-CORRECT but sticky (couldn't restore video) and
collides with input-mux — `FFD2` for black is the right reversible choice; add `FFB0/FFCE-D0` for
blue/snow. (See §0 bench reconciliation.)

(See executive §1a for the 6 copy-pasteable mode sequences.)

---

## 7. Section 04 — OSD engine: windows, BGMAP, palette, attributes, text & menu

**Bank FBxx → I2C `0x5B`.** All addresses proven from ENGELS:313-434; behaviour from opcode bytes.

### Tile numbering & 0x1C0 text base (PROVEN)
10-bit tile index split across FB0E(msb, only bit0-1 R/W) / FB01(lsb). **Text glyphs based at index
0x1C0** (CLEAN:5024 `add a,01c0h`). Empty cell = tile 0x1C0, attr 0x12 (CLEAN:3876-3877). For a
CUSTOM RAM font (one tile per glyph), `tile_code = 0x1C0 + N`. The OEM smallfont doubles
(`add a,a`) — that is smallfont-specific; ModESP's one-tile-per-glyph model is valid.

### Per-character write recipe (PROVEN, MSB-first)
`sysgui_wrtile` (CLEAN:5019-5035): compute 10-bit tile, then **MSB first**:
```
amt_w(0x5B,0x0E, tile_msb)   # FB0E data.msb  (write FIRST)
amt_w(0x5B,0x01, tile_lsb)   # FB01 data.lsb  -> commits cell, FB00 addr.lsb auto-++
# FB00++ ; if wrapped to 0, FB0D++ (manual carry — FB0D does NOT auto-carry)
```
OEM string render (ENGELS:16052-16157) does **two passes**: pass1 writes tiles (FB0E/FB01, manual
lsb→msb shadow carry); pass2 rewinds and writes attributes (FB10) for every drawn cell. Tile path
(FB0E/FB01) and attr path (FB10) are independent registers.

### Window xyloc_msb layout — CONFLICT 12.7 RESOLVED (CLEAN:4661-4763)
xloc = 11-bit pixels: low8 → FB0A, high3 → **FB09 bits0-2** (CLEAN:4704). yloc = 11-bit pixels:
low8 → FB0B, high3 → **FB09 bits4-6** (CLEAN:4752/4756). So `FB09 = (yloc_msb&7)<<4 | (xloc_msb&7)`,
bit7 not R/W. Firmware adds fixed base **+10px X** (CLEAN:4695), **+12px Y** (CLEAN:4747). Input
convention: 0..7F from top/left, -1..-7F from bottom/right (screen 480×272 or 320×240 by panel_type),
0x80 centered.

### Attribute FB10 — CONFLICT 12.9 RESOLVED (CLEAN:8684-8715, ENGELS:11669-11678)
**attr = (BG_index & 7) << 4 | (FG_index & 7)**. Low nibble = FG color, bits4-6 = BG color, bits 3 &
7 no effect. Palette indices: **0=transparent/backdrop, 1=RED, 2=GREEN, 3=BLUE, 4=YELLOW, 5=CYAN,
6=WHITE, 7=BLACK** (CLEAN:8694-8707). White-on-transparent = 0x06; white-on-blue = 0x36; OEM init
filler = 0x12. Proof: osd_fill_attr (CLEAN:8698 `mov a,04h` FG=YELLOW, 8703 `or a,10h` BG=RED);
menu attr packer ENGELS:11671 `swap a`+11672 `and 0F0h` (BG→high) | 11676 `or` (FG→low).

### BGMAP addressing & auto-increment (PROVEN)
Set pointer: FB00=addr.lsb, FB0D=addr.msb. **FB00 auto-increments after each FB01 write; FB0D does
NOT carry** and is manually re-written on wrap (CLEAN:5121-5132 / ENGELS:16107-16116). Cell address
is linear `cell = row*row_width + col`. Full-screen text functions use a **40-cell row stride**
(CLEAN:4356). The scrolling menu lays win0 at bgmap base 0, win1 at base 32 (CLEAN:4459/4471).

### 6-color text palette FB56-61 RGB444 (ENGELS:377-388 / CLEAN:3947-3958)
16-bit per color (msb byte then lsb byte), 12-bit RGB444 = (msb<<8)|lsb, top nibble of msb unused:
RED FB56/57 = 00,0F · GREEN FB58/59 = 00,F0 · BLUE FB5A/5B = 0F,00 · YELLOW FB5C/5D = 00,FF · CYAN
FB5E/5F = 0F,F0 · WHITE FB60/61 = 0F,FF. Color0 transparent & color7 black are fixed. Palette is R/W
(init writes FB56-61) — so recolorable, though OEM never changes it after init.

### Char size & scale — CONFLICT 12.6 RESOLVED (ENGELS:8E2B-8EC1)
FB76/FB77 = char width/height in pixels. Per-window scale FB32(win0)/FB33(win1|win2)/FB34(win3|win4),
each window field = **2×2 bits: low2 ScaleX, high2 ScaleY**.
**THE BUG (CONFIRMED):** writing win2 (ENGELS:8E87 `54 F0`) and win4 (ENGELS:8EBC `54 F0`) uses
`and 0F0h` where it must use `and 0Fh` — the disasm annotates `;<-- BUG: should be 0Fh`. Effect:
setting win2/win4 scale wipes win1/win3's nibble. **Mitigation: our external driver must write the
whole FB33/FB34 byte with both nibbles, never RMW a single window's scale.** (The current driver's
`set_window_scale` uses `sh_fb33_`/`sh_fb34_` shadows for exactly this — correct.)

### Window enable / hide / transparency (PROVEN)
FB05: bit0-4 enable win0-4, **bit6 hide all text**, **bit7 bitmap layer on**. Menu text on = 0x83
(bitmap+win0+win1); single full-screen text = 0x01; OSD off = 0x00. FB06 = 0xC0 semi-transp /
0x00 opaque; level from FB0C = 0x80|transp.

### Print-string recipe (dev7 = 0x5B, window 0)
```
osd_set_cell(col,row,W): amt_w(0x5B,0x00,(row*W+col)&0xFF); amt_w(0x5B,0x0D,((row*W+col)>>8)&1)
osd_putc(ch): tile=0x1C0+N; amt_w(0x5B,0x0E,tile>>8); amt_w(0x5B,0x01,tile&0xFF)  # FB0E before FB01; rewrite FB0D on lsb wrap
osd_attr(fg,bg): amt_w(0x5B,0x10,((bg&7)<<4)|(fg&7))
osd_print: set_cell; loop putc; set_cell again; loop attr  (OEM two-pass, ENGELS:16052-16157)
```

### Scrolling menu engine (PROVEN, CLEAN:4458-4906)
Two 1-row windows: **win0** 32×1 = `'> '+name` at cell0; **win1** 31×1 (vramaddr FB17=32) =
item state/value. Loop sysgui_select_menu (CLEAN:4842): size windows, apply_window_positions
(win0 x+12 y-30, win1 x+12 y-15), enable 0x83, timeout fade toggling FB0C/FB06. Keys: bit0
minus/down, bit2 plus/up, bit1 select. Template tokens: `'%'` = 0-100 bar via sysgui_draw_bar
(font tiles 0x40+); `'@'` jump; else `.`-separated option-name list index.

---

## 8. Section 05 — Custom font upload (FONT RAM, 1bpp tile format, charmap)

Rosetta: OSD bank FBxx == I2C dev7 `0x5B`. All values opcode-backed unless marked INFERRED.

### FONT RAM layout
- Word-addressed; one address == one 16-bit word. "4096×16" (ENGELS:3244-3248) is a hedged free-text
  comment — "0x1000 words / ~8KB" is **INFERRED**, not opcode-proven.
- **Glyph size is NOT hardware-fixed** (PROVEN, resolves 12.14). Words-per-glyph (`INNER.LEN`) is
  computed at upload time from FB76(xsiz)/FB77(ysiz). Threshold opcode `sbc a,#10h / jnc
  @@more_than_16pix` (ENGELS:16182-16184): **xsiz < 16 → INNER.LEN = ysiz** (one word/row);
  **xsiz >= 16 → INNER.LEN = ceil(ysiz*3/2)**. Note **xsiz == 16 takes the 1.5× branch.**
- OEM uses 12×16 (xsiz=12 < 16 → INNER.LEN = ysiz = 0x10 words = 0x20 bytes/glyph).
  `main_font_siz = 0x1540` = 0xAA glyphs (ENGELS:3590).

### Font registers (ENGELS:309-418)
| reg | name | I2C | role |
|---|---|---|---|
| FB02 | font_addr_lsb | amt_w(0x5B,0x02,v) | word addr LSB |
| FB0F | font_addr_msb | amt_w(0x5B,0x0F,v) | word addr MSB (bit4-7 NOT R/W → mask 0x0F) |
| FB04 | font_data HIGH | amt_w(0x5B,0x04,v) | data HIGH byte (written **FIRST**) |
| FB03 | font_data LOW | amt_w(0x5B,0x03,v) | data LOW byte (written **LAST = LATCH**) |
| FB76 | char_xsiz | amt_w(0x5B,0x76,v) | glyph width px (low5) |
| FB77 | char_ysiz | amt_w(0x5B,0x77,v) | glyph height px (low6) |
| FB11 | bitmap_start_lsb | amt_w(0x5B,0x11,v) | TEXT/BITMAP boundary LSB |
| FB70 | bitmap_start_msb | amt_w(0x5B,0x70,v) | boundary MSB (bit2-7 NOT R/W → mask 0x03) |

> ENGELS symbol comments for FB03/FB04 are SWAPPED. Trust the EXECUTING code: FB04 receives the
> source HIGH byte (movx @78DC), FB03 the LOW byte (movx @78F1). **FB04=high FIRST, FB03=low LAST.**

### Manual upload protocol — byte-by-byte (osd_upload_font_characters_manually @782B)
1. INNER.LEN = (xsiz<16) ? ysiz : ceil(ysiz*3/2) (ENGELS:16171-16217).
2. base FB02 = INNER.LEN * dest_tile_index (ENGELS:16219-16224).
3. Per word: **FB0F = addr.msb → FB04 = glyph HIGH byte → FB03 = glyph LOW byte (LATCHES)**.
   Source word read big-endian: high at +0, low at +1 (ENGELS:78BD-78F1).
4. After each glyph: src += INNER.LEN*2 bytes; dest.index++.

For external I2C, set the full address explicitly before each word (the manual-upload path does this).

### In-word bit order — MSB-first (PROVEN, resolves 12.5)
Decoded glyphs "0"/"A"/"1" (ENGELS:3252-3255) are MSB-first, bit15=leftmost, 12px wide (low 4 bits =
padding, e.g. 0x01F0 → high 12 bits form the glyph row). Glyph occupies HIGH bits, left-aligned,
zero-padded at the LSB end.

### DMA path (@9EB7) — DO NOT MIRROR
Reads chip SPI-flash → VRAM (FDD0=0x40, polls FDDF) and pauses rendering via SFR `or [C6h],08h`.
C6h is the DANGER vendor path — use the manual path.

### TEXT/BITMAP boundary FB11/FB70 — RELATIVE to 0x1C0 (PROVEN, resolves 12.4)
CLEAN computes `FB11 = char_lsb - 0xC0` and `FB70 = char_msb - 0x01 - borrow` as a full 16-bit
subtract (CLEAN:9555-9565). So the register = count of 1bpp text tiles starting at 0x1C0. Tiles at
index >= bitmap_start are 4bpp BITMAP. OEM default 0x28. **Colored squares:** if bitmap_start is too
low, uploaded 1bpp text words fall at/above the boundary and HW decodes them as 4bpp (each nibble =
palette color), turning a glyph into a band of colored squares. **Fix: bitmap_start >=
last_text_tile_relative + 1.**

### Charmap
- Ranges: 0x020-0x1BF ROM (firmware-dependent, **unreliable** — resolves 12.4: ship own RAM font +
  UTF-8 map); 0x1C0..(0x1C0+bitmap_start-1) RAM 1bpp text; (0x1C0+bitmap_start)+ RAM 4bpp bitmap.
- Per-tile FONT-RAM word addr for RAM index N: `font_addr = INNER.LEN * N`; BGMAP code = 0x1C0 + N.

### Copy-pasteable I2C upload_font()
```c
void amt_upload_font(const uint16_t* g, uint16_t first_tile, uint8_t count,
                     uint8_t xsiz, uint8_t ysiz) {
  amt_w(0x5B,0x76,xsiz); amt_w(0x5B,0x77,ysiz);      // FB76/FB77 ; xsiz<16 -> INNER.LEN=ysiz
  amt_w(0x5B,0x05,0x00); delay_ms(25);               // windows OFF
  uint16_t bstart = first_tile + count;              // RELATIVE to 0x1C0
  amt_w(0x5B,0x11, bstart & 0xFF);
  amt_w(0x5B,0x70,(bstart>>8)&0x03);                 // FB70 bit2-7 NOT R/W -> mask 0x03
  uint16_t INNER = ysiz;                             // valid only for xsiz<16
  for (uint8_t c=0;c<count;++c){
    uint16_t addr = INNER*(first_tile+c);
    for (uint16_t r=0;r<INNER;++r){
      uint16_t a=addr+r, w=g[c*INNER+r];
      amt_w(0x5B,0x0F,(a>>8)&0x0F);                  // addr msb
      amt_w(0x5B,0x02, a&0xFF);                      // addr lsb
      amt_w(0x5B,0x04,(w>>8)&0xFF);                  // data HIGH (FIRST)
      amt_w(0x5B,0x03, w&0xFF);                      // data LOW  (LAST = LATCH)
    }
  }
  amt_w(0x5B,0x05, restore_mask);
}
// draw tile N: code = 0x1C0 + N
amt_w(0x5B,0x0D,(cell>>8)&1); amt_w(0x5B,0x00,cell&0xFF);
amt_w(0x5B,0x0E,(code>>8)&3); amt_w(0x5B,0x01,code&0xFF);
amt_w(0x5B,0x10,(FG&7)|((BG&7)<<4));
```

---

## 9. Section 06 — Picture: backlight, brightness/contrast/saturation, PAL/NTSC, gamma, ratio, flip, volume

### PWM backlight — conflict 12.10 RESOLVED (use the CLEAN scheme)
**Base clock = 27 MHz** (ENGELS:17869 `019BFCC0h = 27000000`). **Polarity: larger HIGH = brighter**
(PWM0 high time rises monotonically with percent — drop the "bench" caveat).

> **Source caveat:** ENGELS has no apply_backlight routine; `pwm_set_duty_pwm0` (ENGELS:17857) is the
> **VCOM_DC** generator (period `C = 27MHz/2600 ≈ 0x2891`, `high = C*duty/100 + C/200`). 0x2891 is
> PROVEN for VCOM_DC, INFERRED as a backlight scheme. The PROVEN OEM backlight = CLEAN
> `apply_backlight` (CLEAN:3106-3198), a dual-mode duty calc:

- duty 1..49%: fixed `HIGH = 2000`, variable `TOTAL = 2000*100/percent` (CLEAN:3134-3162). Short HIGH
  (<~600) is silently ignored → dark; this mode keeps HIGH=2000 to avoid that.
- duty 0 or 50..100%: fixed `TOTAL = 4000`, `HIGH = 4000/100*percent` (CLEAN:3165-3188). 27MHz/4000
  ≈ 6.75 kHz (audible whine).

Mandatory tail (CLEAN:3191-3198): `FD42 |= 0x03` route P35→PWM0; `FD1F |= 0x01` enable PWM0.
**DANGER:** FD18 bit7 ForceMaxBacklight → PWM dimming OFF (ENGELS:587). Keep it 0.
```
set_backlight(pct):                 ; pct 0..100, larger=brighter
  if pct in 1..49:  HIGH = 2000; TOTAL = 2000*100/pct  (clamp 0xFFFF)
  else:             TOTAL = 4000; HIGH = 4000/100*pct
  amt_w(0x58,0x20, TOTAL&0xFF); amt_w(0x58,0x21, TOTAL>>8)    ; FD20/21 total
  amt_w(0x58,0x28, HIGH&0xFF);  amt_w(0x58,0x29, HIGH>>8)     ; FD28/29 high
  amt_w(0x58,0x42, rd|0x03);    amt_w(0x58,0x1F, rd|0x01)     ; route + enable (RMW-OR)
; quieter ~2.6 kHz alternative: VCOM_DC scheme C=0x2891 total + high=C*pct/100 + C/200 (bench first)
```

### Volume (PWM1) — CLEAN:3229
total=0x64→270kHz, high=pct: `amt_w(0x58,0x22,0x64); amt_w(0x58,0x23,0); amt_w(0x58,0x2A,pct);
amt_w(0x58,0x2B,0); amt_w(0x58,0x42,rd|0x30); amt_w(0x58,0x1F,rd|0x02)`.

### Brightness / Contrast / Saturation (FFD4/D3/D6)
OEM calibrated MEDIUMs (ENGELS:11373-76, lcd_rgb_variant=1, PAL column): brightness=0x8E,
contrast=0x7E, saturation=0x38, tint=0x00. Slider range MEDIUM ± 0x28 for brightness/contrast.
**Saturation: max = MEDIUM+0x28 = 0x60, but min is FIXED 0x00** (`clr a` ENGELS:12617-12619,
comment "saturation MIN=00h (fixed)"). So saturation low bound is **0x00, not 0x10**.
| param | I2C | med | min | max |
|---|---|---|---|---|
| brightness | 0x5A,0xD4 | 0x8E | 0x66 | 0xB6 |
| contrast | 0x5A,0xD3 | 0x7E | 0x56 | 0xA6 |
| saturation | 0x5A,0xD6 | 0x38 | **0x00** | 0x60 |
```
amt_w(0x5A,0xD4, 0x66+(0xB6-0x66)*pct/100)
amt_w(0x5A,0xD3, 0x56+(0xA6-0x56)*pct/100)
amt_w(0x5A,0xD6, 0x00+(0x60-0x00)*pct/100)     ; saturation min = 0x00 (firmware-fixed)
neutral: amt_w(0x5A,0xD4,0x8E); amt_w(0x5A,0xD3,0x7E); amt_w(0x5A,0xD6,0x38); amt_w(0x5A,0xD5,0x00)
```

### Tint (FFD5) + bit7 PAL bug — PROVEN
On PAL/PAL60, firmware forces FFD5=0x00 ("bit7 smashes PAL colors", CLEAN:2806-2818). NTSC xlat
(ENGELS:24068-24122): 50%→0x00; 0..49%→`(0x14 - N*20/50)|0x80`; 51..100%→`((N*30/50)-30)&0x7F`.
```
set_tint(pct,is_pal):
  if is_pal or pct==50: amt_w(0x5A,0xD5,0x00)
  elif pct<50: amt_w(0x5A,0xD5,(20-pct*20/50)|0x80)
  else:        amt_w(0x5A,0xD5,((pct*30/50)-30)&0x7F)
```

### PAL/NTSC & 50/60Hz — CLEAN:2821-2878
Allow native/NTSC: `FC90 &= ~0x01; FE01 &= ~0x01`. Force PAL60: `FC90 |= 0x01; FE01 |= 0x01`
(WARNING: heavy picture roll). FC90 bit0=use 60Hz vertical timing, FE01 bit0=ForcePALcolors. Detect
read-only: FE26.b1 signal, FE28.b2 PAL/NTSC. **(FC90 is bank 0x5C / FCxx — write at init only.)**

### Gamma (FF01/FF20/FF3F, 31 bytes ea) — CLEAN:2902-3004
Selected via xram_sett_rgb_ramps: 0=new, 1=old, else linear.
- new: `03 07 0B 10 15 1B 22 2A 34 3F 4B 58 65 72 7E 89 96 A2 AE BA C4 CD D5 DC E2 E7 EC F0 F4 F8 FB`
- old: `03 06 0A 0E 14 1A 21 29 34 40 4D 59 66 73 81 8E 9C A7 B1 BA C2 CA D0 D7 DD E2 E7 EC F1 F6 FA`
- linear: step +8. Each ramp writes 31 entries (index 0..0x1E) to FF01/FF20/FF3F respectively.

### YUV / color-decode matrix (FFF0..FB, 12B) — CLEAN:3006-3041
new: `11 00 00 E9 E1 0E 09 EE F4 F1 23 81`; old (= ENGELS power-on default): `1A 06 D4 D2 F1 0E 15 E4
F6 F1 1B 81`. Written as a 12-byte block to FFF0.

### Aspect ratio (apply_mode_ratio, CLEAN:3499) — FCxx scaler, init-time only
480×272: 16:9 wide 60Hz=0x05AF/50Hz=0x05A7; 4:3 normal 60Hz=0x0762/50Hz=0x0770. Written to FC96/97
(60Hz) + FCC2/C3 (50Hz). FCE4/FCEA pokes are flagged buggy/no-op. **FCxx = do-not-touch bank; set
ratio at init only.**

### X/Y flip — CLEAN:3257-3282
Double flip: panel SPI (NV3035C R02, HX8238 R01) AND OSD FB78. FB78 = (mapY<<7)|(tileY<<6)|
(mapX<<5)|(tileX<<4): flip both = 0xF0, none = 0x00. FB78 alone flips OSD only.

---

## 10. Section 07 — Signal detection & status registers (readable over I2C?)

### Status register map (FE-bank → I2C 0x59)
| MCU | Name | Bits | Meaning | Proof |
|---|---|---|---|---|
| FE26 | stat_detect_0 | bit1,bit2 | strict valid = (FE26&0x06)==0x06; loose = bit1 | ENGELS:1985-1989 |
| FE27 | stat_detect_1 | bit0 | secondary detect hint | ENGELS:1579 |
| FE28 | stat_framerate_flag | bit2 | 0=NTSC/60, 1=PAL/50 | ENGELS:1995-1997 |
| FE2A | stat_signal_detect | bit4,bit6 | bit4=CVBS1, bit6=CVBS3; **bit6 readable in PLL power-down** | ENGELS:2330-2332 |
| FEAA:AB | stat_sensitivity msb:lsb | 16-bit | 0xFFF=no signal, ~0x287=locked | ENGELS:1507-1513 |
| FE15 (W) | ctrl_sensitivity_0 | byte | 00=max,05=med,09=low | ENGELS:723 |
| FED5 (W) | ctrl_sensitivity_1 | byte | init B1h, bumped to B6 when strength low | ENGELS:830 |

### Valid-signal test (PROVEN)
`mov a,[FE26] / and a,06h / xor a,06h / jz a` → valid ⇔ (FE26 & 0x06)==0x06. Sites: ENGELS:1985,
2100, 2315, 2587, 23159, 23178. Loose form (CLEAN firm_check_signal @4604-4608) tests only FE26.bit1.

### PAL/NTSC (FE28 bit2)
ENGELS:1995-2013 reads FE28, masks 0x04, requires 20 identical samples before commit. 0=NTSC/60,
1=PAL/50.

### Per-input presence (FE2A) — resolves 12.15
input_selector (CLEAN:1568-1580): `jz b.6 = no CVBS3`, `jz b.4 = no CVBS1`; both set → both present.
FE2A bit4=HaveCVBS1, bit6=HaveCVBS3. Power-down caveat (CLEAN:1556-1561): FE26.bit1 dead in PLL-off,
FE2A.bit6 still alive (and is what the standby loop polls).

### Sensitivity (FE15 / FEAA:FEAB)
firm_timer_adjust_sensitivity gates on FE26&0x02, every 100th call, reads FEAA:FEAB. If FE15==05 &
FED5==B5 & strength<0x01A0 (16-bit) then set FED5=B6 and FE15=09. FE15: 00=max,05=med,09=low;
FEAA:FEAB 0xFFF=no signal, ~0x287 when locked.

### OEM polling cadence (Timer1, PROVEN CLEAN:1501-1535)
Timer1 reload D8EFh = 27MHz/12/10000 ≈ 225 Hz (4.44 ms). Sensitivity + boldness every tick;
framerate detect every 4th (~17.8 ms); input_selector every 16th (~71 ms). Framerate/source-menu
commits need 20 agreeing samples (~0.36 s).

### I2C readability
"NOT RW" in the disasm = not WRITABLE (read-only HW status), NOT unreadable. Empirically the existing
driver reads FE26 over I2C (amt630a.cpp:86-90, 444-448) → readable. CAVEAT: the existing
`have_signal()` (amt630a.cpp:447) returns the LOOSE `(v & 0x02)` (FE26 bit1 only), not the strict
`(v&0x06)==0x06`. Fallback if status is stale on a revision: ESP32 ADC sync-detect on raw CVBS, or
chip 12-bit ADC (I2C 0xB0).

### ESP32 recipe (I2C dev7=0x59)
```
have_signal_strict(): amt_r(0x59,0x26) -> (v&0x06)==0x06
have_signal_loose():  amt_r(0x59,0x26) -> (v&0x02)!=0      # what the shipped driver uses
is_ntsc():            amt_r(0x59,0x28) -> (v&0x04)==0 ; is_pal()=!is_ntsc()
which_input_present(): amt_r(0x59,0x2A) -> bit4=CVBS1, bit6=CVBS3
signal_strength():    (amt_r(0x59,0xAA)<<8)|amt_r(0x59,0xAB) ; 0xFFF=none, ~0x287=locked
set_sensitivity_max(): amt_w(0x59,0x15,0x00)   ; 05h=med, 09h=low
```
Poll at ~50 Hz; debounce 20 agreeing samples (~0.4 s); sample ntsc/per-pin only while locked; in PLL
power-down rely on FE2A.bit6 not FE26.bit1; bench-verify FE26 toggles on camera connect, else ADC.

---

## 11. Corrections to the current ModESP driver

File refs: `components/modesp_osd/src/amt630a.cpp` and `include/modesp/osd/amt630a.h`.

### `set_backdrop()` (cpp:431-442, header enum at h:104-106) — **REWRITE**
- **Old:** writes ONLY `FFD2` (`0x54` = black, `0x4F` = show). Enum is `{SNOW, BLUE, BLACK}` but the
  code only handles BLACK vs not-BLACK — SNOW and BLUE produce identical `0x4F`.
- **Why wrong:** FFD2 cannot produce BLUE or SNOW; FFD2=0x54 is non-sticky (firmware rewrites it on
  every display/signal transition via `xlat_r7_to_forced_blank_color`). The header comment claiming
  no-signal = `FEDC` bits is also wrong (FEDC is the input-select register).
- **New:** implement the 5-mode backdrop via bank `0x5A`:
  - LIVE: `FFB0 &= 0x7F; FFD2 = 0x4F; FED7 |= 0x18`
  - BLUE: `FFB0 = 0x20; FFCE=0x13 FFCF=0xDD FFD0=0x72; FFD2 = 0x4F`
  - BLUE+SNOW: blue YCbCr + `FFDA=0x6C; FFB0=0xA0; FFD2=0x4F`
  - BLACK: `FFB0 = 0x20; FFCE=0x00 FFCF=0x80 FFD0=0x80; FFD2 = 0x4F`
  - BLACK+SNOW: black YCbCr + `FFB0=0xA0; FFDA=0x6C; FFD2=0x4F`
  - STANDBY (existing 0x54 path is fine for "blank screen, menu visible").
  Add a `FED5=0xB1` once at init, and re-assert the chosen mode on `FE26.bit1` edges.
  Citations: ENGELS:5642-5686, 26557-26580, CLEAN:3044-3081.

### `select_input()` (cpp:374-429, header enum at h:93-100) — **MOSTLY CORRECT, fix labels**
- **Good:** the runtime code already uses shadow-RMW (`FED7 &= ~0x18` / `|= 0x18`, two-step FED8 and
  FEDC), and has a settle+re-detect loop checking `FE2A & lock` — this matches the proven OEM order
  FED7→FED8→FED7→FEDC. This is the corrected form; the OLD `n=1` mislabel concern is already
  addressed (enum `AV3_CVBS3 = 1` is correct: FED8 bits6,7=0, FEDC bits4,5=2, lock=FE2A bit6).
- **Confirm:** `case 0` (AV1) lock mask `0x10` (bit4) ✓; `case 1` (AV3) lock mask `0x40` (bit6) ✓;
  `case 2` (AV2) documented as junk ✓.
- **Add note:** post-2017 firmware swaps AV1↔AV3 — add a build/runtime flag to exchange the two
  encodings if the bench shows them reversed (CLEAN:3638/3640).

### `set_backlight()` (cpp:358-368) — **two fixes**
- **Missing nothing on pin-mux** — `FD42 = 0x03` IS present (line 362). Good.
- **Old:** `FD1F = 0x03` absolute write (line 367) and `total = 0x1000` (~6.6 kHz). Polarity caveat
  in comment.
- **New:** (1) make `FD1F` RMW-OR (`|= 0x01` for backlight; current `0x03` enables BOTH PWM0+PWM1)
  — use RMW to avoid clobbering the volume-enable bit (CLEAN:3195-3198). (2) Use the CLEAN dual-mode
  duty (TOTAL=4000 at/above 50%, fixed HIGH=2000 below 50%) to dodge the dark-zone, OR the VCOM_DC
  0x2891 period for quieter ~2.6 kHz. (3) Drop the "polarity bench" comment — larger HIGH = brighter
  is PROVEN (ENGELS:17884-17923). (4) Ensure FD18 bit7 stays 0 (ForceMaxBacklight = DANGER).

### `set_saturation()` (cpp:372) — **fix min**
- **Old:** `map_pct(pct, 0x10, 0x60)` (min 0x10).
- **New:** `map_pct(pct, 0x00, 0x60)` — firmware-fixed saturation min is **0x00** (ENGELS:12617-12619).
- brightness (0x66-0xB6) and contrast (0x56-0xA6) maps are CORRECT — leave them.
- Add a PAL tint guard: `set_tint` should write `FFD5=0x00` on PAL/PAL60 (CLEAN:2806-2818).

### `upload_font()` (cpp:325-349) — **near-correct, two polish items**
- **Good:** latch order FB04 (data high) then FB03 (data low) ✓ (cpp:344-345); bitmap_start written
  RELATIVE as `first_tile+count` ✓ (cpp:333-335).
- **Polish:** line 335 masks FB70 with `0xFF`; FB70 is bit2-7 NOT R/W — mask with `0x03` (cosmetic,
  no functional harm while bstart is small).
- **Caution:** `selftest_one_glyph` (cpp:283-284) and `selftest_font_dump` use xsiz=16, ysiz=22.
  xsiz=16 hits the **>=16 (1.5×) branch** in OEM logic (INNER.LEN would be ceil(22*3/2)=33). The
  driver hardcodes 22 words/glyph, which works only because external I2C sets the full address per
  word and FB76 just gates HW decode width. **Use xsiz <= 15 to stay unambiguously in the
  1-word-per-row regime.** Verify on bench that xsiz=16 1bpp tiles decode correctly.

### Palette / attribute (cpp:194-200 `set_palette`; attr usage in `osd_print`/selftests)
- **`set_palette` channel order is INFERRED, not proven.** Current code: `FB56(base)=Blue nibble`,
  `FB56+1 = (G<<4)|R`. The proven index→name mapping is firm (1=RED..6=WHITE), but the literal
  nibble→channel (RGB444 word = msb,lsb with which nibble = which channel) is datasheet-style
  inference. Reference proven init values: RED FB56/57 = 00,0F · GREEN = 00,F0 · BLUE = 0F,00 ·
  YELLOW = 00,FF · CYAN = 0F,F0 · WHITE = 0F,FF (ENGELS:377-388). **Bench-confirm the actual on-screen
  color for each channel before trusting the helper's R/G/B argument order.**
- **Attribute byte** packing in `osd_print` (cpp:209) and selftests (`0x76` = FG6/BG7 white-on-black,
  `0x09`, etc.) follows the proven layout `attr = ((BG&7)<<4) | (FG&7)` ✓. Indices: 1=RED, 2=GREEN,
  3=BLUE, 4=YELLOW, 5=CYAN, 6=WHITE, 7=BLACK, 0=transparent.

### `apply_init_table()` (cpp:94-122) — **trim cargo-cult, fix PLL**
- kInit PLL writes FD11=0x1F/FD12=0x38/FD13=0x00 are an **improvised set** (neither CLEAN screen-on
  FFh/FFh/FFh nor screen-off 0Fh/18h/00h). Use one known-good set verbatim, or drop them on Path A
  (OEM already configured PLL).
- kOn (~60 writes): cargo-cult for Path A — only `0x5B,0x05,0x1F` is must-have. Gate the rest behind
  "OEM shows no video".
- `is_danger()` guard: extend to block FD0E/13/19/1A (PLL hang) and FDDF (SPI status). Note kInit/kOn
  use `raw_w()` which bypasses the guard.

### `have_signal()` (cpp:444-448) — **document loose vs strict**
- Returns loose `(FE26 & 0x02)` (bit1). Fine as a coarse "any signal" check, but add a
  `have_signal_strict()` returning `(FE26 & 0x06)==0x06` for lock-quality gating.

---

## 12. Open hardware questions / bench checklist

**Status readability over I2C (external master, OEM running):**
- Can `FE26/FE28/FE2A` signal-detect bits be read over I2C while the OEM runs? `have_signal()`
  already reads FE26 — confirm the byte actually toggles on camera connect/disconnect. If stale,
  fall back to ESP32 ADC on the CVBS line.
- Confirm which detect register (FE26.bit1/bit2 vs FE2A.bit4/bit6) is more reliable per input when
  AV-PLL is powered (FD12.bit2==1).

**No-signal re-assert behavior:**
- Does the OEM IRQ/input_selector re-assert its own no-signal mode after we override the backdrop
  regs, on the next FE26.bit1 transition? Measure how aggressively it clobbers FFB0/FFCE-D0 to set
  the ESP32 re-assert polling interval.
- FFB0 low bits (bit0-1, init 22h): exact effect on backdrop unknown — preserve via RMW.
- FFDA bit layout (amount/width/density): datasheet-comment only; 6Ch (ENGELS) vs 78h (CLEAN) both
  work — exact density is bench-adjustable.

**Input switching / wiring:**
- Confirm whether the KOZHAN board runs OLD-fw (AV1=CVBS1) or NEW-fw (AV1↔AV3 swapped) — bench by
  selecting AV1 and checking which physical connector shows video.
- Confirm logical AV1/AV3 map to the intended silkscreen connectors (firmware proves silicon pins,
  not labels).
- CVBS2 presence: no dedicated FE2A bit found — if a camera is wired to CVBS2, presence detection is
  unproven (needs apply_av_input + FE26 lock check after selecting it).
- Decide whether ModESP needs the OEM auto-toggle (dual retro-console) behavior or just static
  per-input select.

**Backlight / PWM:**
- Confirm bench polarity of PWM backlight (high=brighter is PROVEN, but verify 0-100% mapping).
- Decide CLEAN dual-mode (~6.75 kHz whine) vs VCOM_DC 0x2891 (~2.6 kHz) period.
- Whether OEM leaves backlight at user-duty or OFF after boot — determines if an immediate backlight
  write is needed.

**OSD / fonts:**
- Whether external I2C writes auto-increment FB02 (font addr) and FB00 (bgmap addr) on data latch —
  the safe path sets the full address per word/cell; bench the 4×-faster addr-once path.
- Whether FB10 attr writes alone advance the FB00 pointer (OEM re-points before attr passes).
- Confirm INNER.LEN for xsiz exactly 16 (1.5× branch) vs 15 — use xsiz<=15 to stay in 1-word/row.
- Whether OEM main loop re-applies FB05/window state on its own GUI events (apply_settings_to_IO_ports)
  and would fight our OSD windows.
- FB10 (FG&7)|((BG&7)<<4) layout: proven by value table but confirm real FG/BG colors on bench.
- 4bpp bitmap region: weakly exercised by OEM — validate FB35 transp / FB36-55 palette path if ever
  needed for graphics.
- win3/win4 vramaddr MSB bit (win2's lives in FB1A.bit7) not separately proven.
- FB09 bit3 / unused loc bits purpose unconfirmed (datasheet calls bit7 not-R/W).

**Palette / color:**
- Exact RGB444 channel→nibble order and FC00 color_swap interaction — bench-confirm each channel's
  on-screen color (the index→name mapping 1=RED..6=WHITE is firmly proven; the literal nibble→channel
  is inference).

**SFR / unlock:**
- Whether 0x5F gives READ access to SFRs (e.g. vsync 91h) — FIZIK only writes; not provable from
  disasm. The 0xBE/0x5F channel is disassembly-only (not in datasheet TOC).

**Datasheet completeness:**
- Obtain the full V1.1 PDF to vendor-confirm FED7/8/DC, FFB0/D2/CE-D0/DA bit semantics (currently
  disassembly-only). The condensed preview lacks §6.1-6.10 register tables.
- Confirm which FFxx addresses belong to the §6.6 RCRT "MCU access only" block (FFB0/D2/CE-D0/DA are
  §6.4/6.5 by address range, likely OK, but bench-verify any new FFxx register).

**PAL/NTSC edge cases:**
- FE28 reports a single PAL/NTSC + 50/60 bit; it does not distinguish PAL60/NTSC50 oddball modes
  (OEM has separate xram_sett_pal_ntsc tweaks). If exact field rate independent of color standard is
  needed, additional FE-bank bits may be required.
- KOZHAN lcd_rgb_variant (PAL medians 0x7E/0x8E/0x38 for variant=1 vs 0x80/0x80/0x36 for .else) —
  assumed variant=1 (the .if branch with absolute addresses). NTSC slots are 0x80/0x80/0x55.

**Aspect ratio:**
- apply_mode_ratio writes FCxx (do-not-touch bank); FCE4/FCEA are author-flagged buggy/no-op —
  set ratio at init only, bench-verify live ratio change risk.

## Citations
ENGELS = `firmware/ENGELS.A22`; CLEAN = `emulator/AMT630A.A22`; datasheet = `datasheet_fitz.txt`.
Per-claim line numbers are given inline throughout. Full durable findings:
`C:/Users/ПК/Downloads/amt630a_analysis/findings/01..08-*.md`.
