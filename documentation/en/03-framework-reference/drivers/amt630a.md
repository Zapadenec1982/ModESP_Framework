# `amt630a` — I²C OSD/TFT video-SoC as a display (capability `display`)

> 📖 **In Ukrainian:** [.../uk/.../amt630a](../../../uk/03-framework-reference/drivers/amt630a.md)

A display-backend driver for the **AMT630A** video-SoC (ZCD-630A-4.3D board): the chip
drives a TFT panel and composite video, and the ESP32 overlays an OSD on top of it (text,
menus, notification banners) over I²C. The driver provides the **`display`** capability through
the `Amt630aPort` adapter, which implements the `IDisplayPort` seam (`present_main` /
`present_menu` / `present_edit` / `present_notice`). The
[`display`](../modules/display.md) module owns only the *content* (what to show) and
calls the port by role — it knows nothing of the chip itself, the I²C banks, or the register
sequences (see [R0.1](../rules.md#r01--роль--здатність-capability-ніколи-не-драйвер),
[ADR-002](../../../../docs/display/ADR-002-display-architecture.md)).

**The protocol format belongs to the driver.** The entire AMT630A register layout
(vendor-unlock, OSD windows, FONT RAM, palette, PWM backlight, CVBS mux,
no-signal backdrop, power-gate) lives in the driver component
(`osd::Amt630a` + `Amt630aPort`). The module supplies a semantic intent
(`MainView` / `MenuView` / `EditView` / `Notice`), and the port decides the *presentation*
itself — a large primary value in window W0 ×2, small lines in W1, a notification banner
colored by level (ADR-002 §6: "the driver decides presentation").

`hardware_type` is `i2c_display`, `transport` is `wired`. The binding goes through
`i2c_displays` in **board.json** (geometry + I²C bus), not through a MAC or an address —
`requires_address: false`. The driver is optional: its SRCS are gated by
`CONFIG_MODESP_DRIVER_AMT630A` (menu **"ModESP Drivers"**).

## Hardware

| Parameter | Value |
|----------|----------|
| Chip | AMT630A (OSD/TFT video-SoC) on the ZCD-630A-4.3D board |
| Panel | 480×272 TFT (overscan), composite CVBS video |
| Transport | I²C (`wired`), 6 device addresses (banks): `0x58`/`0x59`/`0x5A`/`0x5B`/`0x5C`/`0x5F` |
| Presence probe | ACK on the OSD bank `0x5B` (`i2c_master_probe`) |
| Video inputs | 2 × CVBS (AV1/CVBS1, AV3/CVBS3; AV2 — junk) |
| Backlight | PWM0 duty (`FD42`/`FD1F`), 0–100 % |
| Power-gate | optional load-switch on a GPIO (0 mA when OFF) |
| Font | Cyrillic RAM font 16×20, 1bpp, in the chip's FONT RAM |

## Capability and channels

The driver provides exactly one capability — **`display`** (`provides.type: display`),
`settings: []` (no per-driver settings in the manifest). There are no channels in the
"temperature/humidity" sense here: `display` is an *output* capability that the module
connects to through `IDisplayPort`, not a set of named state keys.

The port declares its abilities through `caps()` — the module reads them and adapts its UX:

| `DisplayCaps` | Value | What it provides |
|---------------|----------|--------|
| `has_color` | `true` | OSD palette (color1 red, color2 yellow…) |
| `has_backlight` | `true` | `set_backlight(pct)` — PWM0 duty |
| `has_video_params` | `true` | `set_brightness` / `set_contrast` / `set_saturation` |
| `has_inputs` | `true`, `input_count = 2` | `select_input(n)` — CVBS mux |
| `has_backdrop` | `true` | `set_backdrop(SNOW/BLUE/BLACK)` — no-signal background |
| `has_power` | `power_gpio >= 0` | `set_rail(on)` + non-blocking chunked OSD recovery |

## Bindings

### board.json

The display is declared in `i2c_displays` — that is where the bus, the OSD-grid geometry
(`cols`/`rows`), and the overscan calibration (`cal_x`/`cal_y` in pixels) live:

```json
"i2c_buses": [
  { "id": "i2c_0", "sda": 6, "scl": 5, "freq_hz": 100000 }
],
"i2c_displays": [
  { "id": "disp_0", "bus": "i2c_0", "chip": "amt630a",
    "cols": 20, "rows": 10, "cal_x": -8, "cal_y": -8 }
]
```

### bindings.json

A single entry binding the display to the `display` module through the role-capability
`display_main`:

```json
{ "hardware": "disp_0", "driver": "amt630a", "role": "display_main", "module": "display" }
```

| Field | Value |
|------|----------|
| `hardware` | `id` of the display from `i2c_displays` (`disp_0`) |
| `driver` | `amt630a` |
| `role` | `display_main` — declares the owning module `display` |
| `module` | `display` |

Optional **power-gate**: add `"power_gpio": <n>` to the binding — the driver will bring up
the chip's power at startup and will be able to power the panel down in sleep
(`caps().has_power` becomes `true`). Without it, `set_rail()` is a no-op with a warning.

## Protocol (in brief)

The port configures the OSD **on top of the running OEM firmware** — no off→on cycling
of the video banks, so as not to disturb the working image. Key implementation invariants:

- **DANGER registers are locked** (`is_danger`): PLL, SPI-flash pins, the Tcon bank
  `0x5C` — writing to them hangs the chip or corrupts flash; `amt_w` silently drops them.
- **Windows are enabled AFTER writing BGMAP** (`window_enable` at the end of `render`) —
  the frame appears atomically, without flashing garbage on startup.
- **The Cyrillic font** is loaded into FONT RAM as 1bpp with a relative
  `bitmap_start` (0x1C0 base); UTF-8 → codepoint → tile via `amt630a_cp_to_tile`.
- **Power-gate recovery is non-blocking** (`service()` steps per tick): after a
  cold boot of the chip the OSD is lost, so the port chunked-reconfigures (WAIT→SETUP→FONT)
  without blocking the main loop (a monolithic reconfig of ~5–7 s would trip the TWDT). While
  `busy()`, the module waits, then re-submits the frame.

Deeper detail is in the driver's design docs: the register layout
([AMT630A_control_reference](../../../../docs/amt630a/AMT630A_control_reference.md)),
the driver model ([AMT630A_driver_design](../../../../docs/amt630a/AMT630A_driver_design.md)),
sleep/power ([AMT630A_power_modes](../../../../docs/amt630a/AMT630A_power_modes.md)),
notification delivery ([ADR-001](../../../../docs/amt630a/ADR-001-osd-notifications.md)).

## Factory and registration

A single registration point (R3.2): the factory resolves the `i2c_display` and the bus through HAL and
builds a **singleton** port (zero heap, static in place):

```cpp
IDisplayPort* amt630a_factory(const modesp::Binding& b, modesp::HAL& hal) {
    auto* dcfg = hal.find_i2c_display(b.hardware_id);          // board.json → geometry
    auto* bus  = hal.find_i2c_bus(dcfg->bus_id);               // board.json → bus
    const int power_gpio = static_cast<int>(b.setting_or("power_gpio", -1.0f));
    static Amt630aPort port(bus->bus_handle, bus->freq_hz,
                            dcfg->cols, dcfg->rows, dcfg->cal_x, dcfg->cal_y, power_gpio);
    return &port;
}
MODESP_REGISTER_DISPLAY(amt630a, &amt630a_factory)
```

## Optionality (Kconfig)

```
CONFIG_MODESP_DRIVER_AMT630A   (menu "ModESP Drivers")
```

A disabled driver is not compiled (SRCS gate, R5.2). If board.json binds
`amt630a` while the toggle is disabled, the build fails with FATAL; reconcile with
`python tools/drivers_sync.py --fix`.

## What's next

- **[modules/display.md](../modules/display.md)** — the module that owns the `display_main` role (menus, editing, notifications).
- **[ADR-002](../../../../docs/display/ADR-002-display-architecture.md)** — the two-layer architecture: the `IDisplayPort` seam + driver adapter.
- **[ADR-003](../../../../docs/display/ADR-003-amt630a-adr002-compliance.md)** — the AMT630A port's compliance with ADR-002.
- **[rules.md](../rules.md)** — R0.1 (role=capability), R3.2/R3.3 (peripheral route), R5.2 (optionality).
- **[project-hierarchy.md](../project-hierarchy.md)** — the peripheral route Module↔Role↔Device↔Binding + invariants.
- **Sibling backend:** [ble_led_panel](ble_led_panel.md) — another output driver (`IPanelPort`) over BLE.

## Sources

- [`drivers/amt630a/manifest.json`](../../../../drivers/amt630a/manifest.json)
- [`drivers/amt630a/src/amt630a_port.cpp`](../../../../drivers/amt630a/src/amt630a_port.cpp) — the `IDisplayPort` adapter + factory/registration
- [`drivers/amt630a/src/amt630a.cpp`](../../../../drivers/amt630a/src/amt630a.cpp) — chip control (I²C banks, OSD, font, CVBS, backdrop, PWM)
