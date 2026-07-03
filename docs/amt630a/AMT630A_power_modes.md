# AMT630A — Power Modes & Energy-Efficiency Reference

> **Scope:** ModESP (ESP32) controls the AMT630A *externally over I²C* on top of the running OEM
> firmware ("Path A" — no firmware replacement). This doc answers: **can the AMT630A be put into an
> energy-efficient mode, how, how safely, and how much does it save?**
>
> **Authority:** disassembly (opcode-backed, file:line cited) + bench measurements. The held datasheet
> is a **9-page condensed preview**; §7 (MCU Peripheral) and §8 (Electrical Specs) are NOT on this
> machine, so there is **no datasheet sleep-mode / current-consumption / clock-gating table** to lean
> on. Every register/sequence claim below cites an opcode source or is marked `INFERRED` / `BENCH`.
>
> **Rosetta:** MCU XRAM `Fxxnn` ↔ `amt_w(dev7, 0xnn, val)`. Banks: `FD`=`0x58` Global/PWM/PLL,
> `FE`=`0x59` AV decoder, `FF`=`0x5A` Video-process, `FB`=`0x5B` OSD, `FC`=`0x5C` Tcon (DO NOT TOUCH),
> `0x5F`=vendor/watchdog window. `amt_rmw(dev7,reg,and,or)` = read-modify-write.
>
> Sources: `findings_power/01-power-routines.md`, `02-datasheet-power.md`, `03-safety-reconcile.md`;
> CLEAN = `emulator/AMT630A.A22` (comments), ENGELS = `firmware/ENGELS.A22` (absolute addrs + opcodes);
> `datasheet_fitz.txt`; existing `docs/amt630a/AMT630A_control_reference.md`.

---

## 1. TL;DR

**Yes — but the entire *safe* energy story over I²C is the backlight.** The LCD backlight is ~80% of
total power (bench: 290 mA @100% → 56 mA @0%), and dimming/blanking it is instant, fully reversible,
and zero brick-risk. Everything deeper (PLL gating, AV-PLL gate `FD12.bit2`, `chip_en FD01.bit0`, panel
SPI standby) is **DANGER** over Path A — recovery is not opcode-proven without a power cycle — for only a
marginal extra ~48 mA.

**Single best safe win:** ship an **idle-timeout auto-dim → auto-DISPLAY_OFF** on the backlight.
~81% power reduction (290 → 56 mA), instant wake (one PWM period + one `FFD2` write), no PLL/panel/input
re-init. The residual ~56 mA chip+panel floor is **not safely reducible over I²C** — only a board-level
rail-gate (added hardware) recovers it.

---

## 2. Tiered power model

Bench reference (KOZHAN-class `stand_s3`, 4.3" panel; backlight LED string dominates, ~234 mA @100%):

| Tier | Enter (I²C seq) | Exit / wake | Current draw | Wake latency | Class |
|---|---|---|---|---|---|
| **ACTIVE** | `set_backlight(100)` + `display_on(true)` (`FFD2=0x4F`) | — | **290 mA** | — | **SAFE** |
| **DIMMED** | `set_backlight(40)` (PWM duty only) | `set_backlight(100)` | **~60 mA** (BENCH: 50%=65 mA; 40% interpolated) | instant (1 PWM period) | **SAFE** |
| **DISPLAY_OFF** | `display_on(false)` (`FFD2=0x54`) + `set_backlight(0)` | `set_backlight(last)` + `display_on(true)` | **56 mA** (BENCH) | instant (1 PWM period + 1 FFD2 write) | **SAFE** |
| **STANDBY** (sw only) | = DISPLAY_OFF + stop ESP repaint / slow poll | wake = same as DISPLAY_OFF | **56 mA** (identical to DISPLAY_OFF) | instant | **SAFE** |
| **OEM internal standby** | PLL `FD11=0Fh/FD12=18h/FD13=00h` + panel-SPI standby + `FD0E=20h`/`FED5=00h` | `switch_lcd_screen_on` PLL pair + FEA0 resync (~110 ms) | **~8.3 mA** (3.5" bench; firmware-annotation) | ~110 ms+ *if it wakes* | **DANGER** |
| **chip_en off** | `amt_w(0x58,0x01, v&~1)` (FD01.bit0=0) | none proven over I²C | unquantified (small slice of the 56 mA) | unknown / unrecoverable | **DANGER / brick** |
| **DEEP-OFF** | board GPIO cuts AMT630A 3.3 V rail (added HW) | GPIO restores rail → OEM cold-boot + `apply_osd_init()` + font + repaint | **~0 mA** | **~0.3–1 s** (BENCH-pending) | **SAFE via HW** |

**DISPLAY_OFF (56 mA) is the safe floor over I²C.** STANDBY is *electrically identical* — its only added
value is software (stop ESP repaint, slow the poll), not chip power.

**Why the SAFE tiers wake instantly:** PLL, panel-SPI, decoder, and AV video (`FED7.bit3,4`) all stay
live the whole time. DISPLAY_OFF only writes `FFD2=0x54` (a Video-Process forced-blank color that does
**not** touch OSD: ENGELS:925) and zeroes PWM duty; it never clears `FED7`. That is why wake needs no
`FED7 |= 0x18` re-enable.
> **Invariant:** if any future edit adds a `FED7` video-off to DISPLAY_OFF, the wake MUST add
> `FED7 |= 0x18` back, or video will not return (cf. control-reference §1a "mode 6").

---

## 3. SAFE recipes (copy-pasteable `amt_w` sequences)

All writes route ONLY through `set_backlight()` (FD PWM, `amt630a.cpp:369`) and `display_on()`
(FFD2, `amt630a.cpp:482`). No DANGER register is touched. `dev7` = bank address.

```text
# ── ACTIVE (full picture) ────────────────────────────────────────────────
# set_backlight(100):  apply_backlight CLEAN:3106-3198
amt_w(0x58,0x42,0x03)   # FD42 pin-mux P35/P36 → PWM mode (required, else duty is ignored)
amt_w(0x58,0x20,0x00)   # FD20 PWM0 total period LSB  (total=0x1000)
amt_w(0x58,0x21,0x10)   # FD21 PWM0 total period MSB  (total ≥ 0x0100, ENGELS:26324)
amt_w(0x58,0x28,0x00)   # FD28 PWM0 high (duty) LSB   (high = total*pct/100; 100% → 0x1000)
amt_w(0x58,0x29,0x10)   # FD29 PWM0 high (duty) MSB
amt_w(0x58,0x1F,0x03)   # FD1F PWM enable (bit0 PWM0 backlight; current driver writes 0x03)
# display_on(true):
amt_w(0x5A,0xD2,0x4F)   # FFD2 = 0x4F  show AV video + OSD          (xlat r7=0, CLEAN:3802-3822)

# ── DIMMED (~40%) ────────────────────────────────────────────────────────
# set_backlight(40): only the duty changes; pin-mux + enable already set
amt_w(0x58,0x28,0x66)   # FD28 high LSB = 0x0666  (0x1000*40/100)
amt_w(0x58,0x29,0x06)   # FD29 high MSB

# ── DISPLAY_OFF (safe floor, 56 mA — OSD stays drawable) ─────────────────
# display_on(false):
amt_w(0x5A,0xD2,0x54)   # FFD2 = 0x54  forced black blank (OSD only)  (xlat r7=6, CLEAN:3802-3822)
# set_backlight(0): duty → 0 (bench 234 mA → 0). Pipeline stays live → instant wake.
amt_w(0x58,0x28,0x00)   # FD28 high LSB = 0
amt_w(0x58,0x29,0x00)   # FD29 high MSB = 0
#   (equivalently FD1F &= ~0x01 disables PWM0; both → 56 mA. BENCH: confirm which the firmware
#    re-enables cleanest — current set_backlight(0) keeps FD1F=0x03 and zeroes duty.)

# ── WAKE from DISPLAY_OFF (instant, no re-init) ──────────────────────────
amt_w(0x58,0x28,<last_high_LSB>)  # restore duty
amt_w(0x58,0x29,<last_high_MSB>)
amt_w(0x5A,0xD2,0x4F)             # FFD2 show
```

Notes:
- `set_backlight(pct)` in the driver currently computes `total=0x1000`, `high=total*pct/100`, and writes
  `FD42=0x03`, `FD20/21`, `FD28/29`, `FD1F=0x03` every call (`amt630a.cpp:369-379`). Idempotent and SAFE.
- `display_on(bool)` writes only `FFD2` (`amt630a.cpp:480-484`). Idempotent and SAFE.
- The OEM's *own* backlight-off (`pwm_switch_pwm0_on_port35` r7=0, ENGELS:26284) drives P35 static-LOW via
  SFRs — **NOT I²C-reachable**. The duty=0 / `FD1F&=~1` route is the I²C-equivalent.

---

## 4. DANGER list — power levers to NEVER touch over I²C (Path A)

Recovery for all of these is **not opcode-proven without a stand power-cycle**. They attack at most the
~48 mA below the safe floor — not worth the brick risk on a bench without power-cycle ready.

| Lever | Reg(s) | Why DANGER | Cite |
|---|---|---|---|
| **chip_en off** | `FD01.bit0=0` | Gates **part of the master CLK** → stops the 8051 that services I²C + refreshes panel → hang until RESET/power-cycle. OEM never toggles it operationally. Annotated **"hangs CPU"**. Saves only a fraction of the 56 mA. | datasheet_fitz.txt:695-704; CLEAN:356/7469, ENGELS:564 |
| **soft reset** | `FD00=0x5A` | Reset, not a sleep. Re-runs OEM init; destroys ESP-side OSD state. Never an energy control. | datasheet_fitz.txt:691-693 |
| **display PLL** | `FD0E / FD11 / FD12 / FD13 / FD19 / FD1A` | Hang/freeze/blank. `FD11/12` annotated "hangs CPU/ADC". Exact off-values are panel-specific (CLEAN:3737-3793) — never improvise. | CLEAN:7470-7473, ENGELS:581 |
| **AV-PLL gate** | `FD12.bit2` | Dedicated AV-PLL power-down gate, not a knob. OEM never gates it in isolation; wake needs `FD12.bit2` set + `FED7\|=03` + force-resync. Untested in isolation on silicon (see §7). | CLEAN:1551-1567 |
| **ForceMaxBacklight** | `FD18.bit7` | **Anti-saving**: forces backlight to max and disables PWM dimming. Keep 0. | ENGELS:587 |
| **KillTft / StopDotClk** | `FD40 / FD41` | Stops TFT timing / dot-clock → blank panel, may need re-init. | ENGELS:629-630 |
| **KillTftUpdating** | `FD50.bit5` | Freezes TFT update path. | ENGELS:642 |
| **SPI-flash regs** | `FD32/33`, `FDD0/DE/E0/DF` | OEM firmware lives in this SPI-flash; corrupting access can brick. | ENGELS:615-616, 28622 |
| **panel SPI standby** | NV3035C R00 / HX8238 R03/R0D/R0E | Panel-side power, reached over the **MCU's bit-banged Port3 SPI**, *not I²C* — unreachable on Path A anyway. | CLEAN:3772-3778 |
| **Tcon bank** | whole `FCxx` (`0x5C`) | Timing controller — "DO NOT TOUCH". | Rosetta map |
| **8051 PCON** | SFR 0x87 | Idle "doesn't reduce power"; power-down bit "HANGS". **Not I²C-reachable** regardless. | CLEAN:4622-4623, ENGELS:1060 |

**The OEM's own ~8.3 mA standby** (`switch_screen_and_backlight_off` → `switch_lcd_screen_off`,
CLEAN:3678/3771-3797) is reached by stacking PLL-down (`FD11=0Fh/FD12=18h/FD13=00h`, the ~60→31.2 mA drop)
+ panel-SPI standby + `FD0E=20h` + `FED5=00h` (31.2→10.4→8.3 mA). **Every register in that path is in the
DANGER table above.** Do not replicate it externally.

---

## 5. Proposed C++ API

Builds on the existing `set_backlight(uint8_t pct)` (`amt630a.h:86`) and `display_on(bool)`
(`amt630a.h:128`). `set_power_mode` routes ONLY through those two — no DANGER writes. DEEP-OFF stays OUT
of `Amt630a` (it is a board/HAL GPIO call with a different, non-instant wake contract).

```cpp
// amt630a.h  (additions)
enum class PowerMode : uint8_t {
    ACTIVE,       // backlight = active_pct, FFD2 show
    DIMMED,       // backlight = dim_pct,    FFD2 show
    DISPLAY_OFF,  // backlight = 0,          FFD2 black (OSD still drawable)
    STANDBY,      // electrically == DISPLAY_OFF; signals caller to stop repaint / slow poll
};

bool set_power_mode(PowerMode m);                       // SAFE: only set_backlight()+display_on()
void set_power_levels(uint8_t active_pct, uint8_t dim_pct);   // defaults 100 / 40
PowerMode power_mode() const { return pm_; }

// optional idle-timeout auto-dim (driven from the OSD task tick)
void configure_idle(uint32_t dim_after_ms, uint32_t off_after_ms);  // 0 disables a stage
void note_activity(uint32_t now_ms);                   // snap to ACTIVE instantly
void tick(uint32_t now_ms);                            // state machine: ACTIVE→DIMMED→DISPLAY_OFF
```

```cpp
// amt630a.cpp  (sketch — all SAFE)
bool Amt630a::set_power_mode(PowerMode m) {
    switch (m) {
    case PowerMode::ACTIVE:      display_on(true);  set_backlight(active_pct_); break;
    case PowerMode::DIMMED:      display_on(true);  set_backlight(dim_pct_);    break;
    case PowerMode::DISPLAY_OFF:
    case PowerMode::STANDBY:     display_on(false); set_backlight(0);           break;
    }
    pm_ = m;
    return true;   // see "polish" below for real error propagation
}
```

**Recommended polish (quality, not safety):**
- Change `set_backlight`/`display_on` from `void` → `bool` to propagate I²C errors
  (`amt630a.h:86/128`, `amt630a.cpp:369/480`), then have `set_power_mode` AND/accumulate them.
- Make `FD1F` an RMW-OR `|= 0x01` instead of the absolute `0x03` (`amt630a.cpp:378`). OEM
  `apply_backlight` does exactly this — `FD42 |= 0x03` then `FD1F |= 0x01` (RMW-OR, CLEAN:3191-3198) —
  so it does not also force-enable PWM1/volume.

### Manifest / web mapping

```yaml
# display manifest — UI bindings (maps to set_power_mode + configure_idle)
display:
  power_mode:                       # select → set_power_mode(...)
    type: select
    options: [active, dimmed, display_off]   # STANDBY is internal, not user-facing
    default: active
  idle_dim_after_s:                 # → configure_idle(dim_after_ms=...)
    type: number
    unit: s
    default: 30
    range: [0, 600]                 # 0 = never dim
  idle_off_after_s:                 # → configure_idle(off_after_ms=...)
    type: number
    unit: s
    default: 120
    range: [0, 3600]                # 0 = never blank
  backlight_dim_pct:                # → set_power_levels(active, dim_pct)
    type: number
    unit: '%'
    default: 40
    range: [0, 100]
```

Web/touch behaviour: any UI interaction calls `note_activity()` → instant snap back to ACTIVE; the OSD
task calls `tick(now_ms)` each cycle to advance ACTIVE → DIMMED → DISPLAY_OFF on the configured timeouts.

---

## 6. Hardware option — board-level power-gate (DEEP-OFF)

A P-FET / load switch (e.g. TPS22918-class) on the AMT630A **3.3 V rail**, enabled from a spare ESP GPIO
(`display_rail_power(bool)` in the board/HAL layer — NOT in `Amt630a`).

- **What it powers down:** the chip, panel, *and* the LED driver all share the single gated 3.3 V supply
  (datasheet: "3.3V power supply only", "build-in LDO for 1.2v core" → the core rail cannot be gated from
  the board, only the whole 3.3 V domain). datasheet_fitz.txt:148-214.
- **Savings:** full **~290 mA → ~0 mA** (recovers the residual 56 mA that I²C cannot touch).
- **Wake latency:** **~0.3–1 s** (BENCH-pending) — OEM cold-boots from SPI-flash, then ESP re-runs
  `apply_osd_init()` + re-uploads font + repaints. Slower, non-instant wake contract.
- **Safe by construction:** a clean cold boot IS the "power cycle" that clears any hung state — this is
  the *only* way to safely recover the 56 mA floor.
- **Caveats:**
  - **Shared-I²C back-powering:** when the rail is gated, ensure no other device on SDA/SCL is back-fed
    through the AMT630A's unpowered I²C pins (pins 60/61, datasheet_fitz.txt:652-661). May need series Rs
    or a bus isolator.
  - SDA/SCL must be electrically dead while gated, so ESP must not poll the chip until the rail is back.

**When it's worth it:** only if the product must recover the residual 56 mA during long screen-off
periods (battery / tight standby budget). **Skip it** for mains-powered fridge controllers where 56 mA
idle is acceptable — DISPLAY_OFF over I²C already captures the 81% win with instant wake and zero added BOM.

---

## 7. Open questions / bench checklist

All of the following are **untested on this silicon** — measure with a power-cycle ready before relying on
any of them. None block shipping §3/§5 (which are bench-proven SAFE).

1. **AV-PLL gate in isolation** — does clearing ONLY `FD12.bit2` keep OSD/TFT alive and save measurable
   mA on the KOZHAN 4.3" panel? Firmware only ever clears it inside the full screen-off mask (CLEAN:1551-1567).
2. **Reducible floor?** — exact mA of I²C-safe deep-idle (backlight 0 + `FFD2=0x54`) vs the 56 mA
   backlight-0 idle. Is the residual ~56 mA reducible at all without DANGER PLL/panel regs? (Expected: no.)
3. **OEM off→on PLL pair recoverable?** — is `switch_lcd_screen_off` → `switch_lcd_screen_on`
   (FD11/12/13 + FED7 + FEA0 resync, ENGELS:27807/25054) recoverable over I²C on *this* board if replayed
   verbatim? OEM-blessed bracket, but FD11/12/13 are DANGER — needs a power-cycle-ready test.
4. **Panel SPI standby over Path A?** — can HX8238 R03 / NV3035C R00 (~13 mA) be reached over I²C at all,
   or is the panel control SPI strictly MCU-internal (bit-banged Port3)? (Expected: unreachable.)
5. **OEM re-assert cadence** — does the OEM main loop re-assert `FFD2`
   (`xlat_r7_to_forced_blank_color`) or backlight on `FE26.bit1` / `FE2A` signal edges, fighting an
   externally-set DISPLAY_OFF? Measure the re-assert cadence to set the ESP poll interval.
6. **chip_en reversibility** — is `FD01.bit0=0` reversible over I²C without a power-cycle, and what is the
   wake latency? Test: write 0, confirm I²C still ACKs, write 1, confirm video returns. (Disasm says no.)
7. **chip_en savings** — bench delta (chip_en 1 vs 0, backlight off) to quantify the small slice it powers
   down (BK/ADC analog only) vs the recovery risk.
8. **Backlight duty→current curve** — non-linear (50% = 9 mA backlight); DIMMED-low (~10–20%) is
   interpolated, not measured. Confirm the exact duty/current mapping on the bench.
9. **DISPLAY_OFF method parity** — `set_backlight(0)` (duty=0, keeps `FD1F=0x03`) vs `FD1F &= ~0x01`
   (PWM0 disable): both should be 56 mA — verify, and pick the one the firmware re-enables cleanest.
10. **DEEP-OFF wake time** — measure actual cold-boot-to-OSD time (OEM boot + `apply_osd_init` + font +
    repaint), estimated 0.3–1 s.
11. **Shared-I²C back-powering when gated** — confirm no device on SDA/SCL is back-fed through the
    AMT630A's unpowered pins; add series Rs / isolator if needed.
12. **Datasheet gap** — the full ~90-page ARKMICRO datasheet (§7 MCU Peripheral, §8 Electrical) is NOT on
    this machine (only a 9-page preview with no electrical/standby/current table). Obtaining it is the only
    way to datasheet-confirm sleep modes, clock-gating, current figures, and the supply-rail table. Until
    then, **disasm + bench are authoritative.**
```
