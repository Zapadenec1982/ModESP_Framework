# `display` — on-device menu generated from manifests

> 📖 **Українською:** [documentation/uk/03-framework-reference/modules/display.md](../../../uk/03-framework-reference/modules/display.md)

`display` is a generic framework module that renders the device's local screen menu. Like WebUI and MQTT, the menu is **generated from manifests**: each module describes a `display:` section in its manifest.json, the generator merges them into one constexpr tree (`generated/display_screens.h`), and this module handles navigation, value display, and parameter editing through SharedState.

The module is **hardware-agnostic**: it never touches pixels, colours, rows/columns, or a specific chip. It pushes **semantic Views** (`MainView` / `MenuView` / `EditView` / `Notice`) through the `IDisplayPort` seam and reads the backend's `caps()`. A driver-adapter (`LogPort`, `At7456ePort`, `Amt630aPort`) turns those Views into hardware. Without hardware the default `LogPort` prints the Views to the serial log.

This two-layer split is **ADR-002** ([docs/display/ADR-002-display-architecture.md](../../../../docs/display/ADR-002-display-architecture.md)); the AMT630A adapter was brought into compliance by **ADR-003** ([docs/display/ADR-003-amt630a-adr002-compliance.md](../../../../docs/display/ADR-003-amt630a-adr002-compliance.md)).

REQUIRES: `modesp_core`. No GPIO of its own — SharedState only (the backend owns its bus/pins).

## Architecture: three layers

| Layer | Where | Knows |
|---|---|---|
| **Abstract module** | `modules/display/` (`DisplayModule`, `MenuEngine`, `NotificationQueue`) | only *intent* — semantic Views + `caps()`; never pixels/colours/chip |
| **Port adapter** `XxxPort : IDisplayPort` | `modules/display/src/` (`LogPort`, `At7456ePort`, `Amt630aPort`) | turns Views → chip; owns **all layout** (via the optional `CharGridLayout` helper), colour, scroll, capabilities |
| **Portable chip driver** | `drivers/at7456e/`, `drivers/amt630a/` (`At7456e`, `Amt630a` — chip code lives with its driver) | raw SPI/I²C registers; **zero ModESP semantics** |

```
manifest.json (display:) ──┐
manifest.json (display:) ──┼→ generate_ui.py → display_screens.h (MENU_NODES, MENU_NODE_CAPS, MAIN_VALUES)
manifest.json (display:) ──┘                          │
                                                      ▼
buttons (WebUI / MQTT / GPIO) → SharedState → DisplayModule → MenuEngine → MainView/MenuView/EditView
                                                      │
                                          IDisplayPort (present_*, caps, set_*, as_*) → backend → chip
```

- **MenuEngine** — pure logic (host-tested, zero heap): the `MAIN → MENU → EDIT` FSM. Emits semantic Views; the driver computes scroll/cursor/colour. Gates menu items by `caps()` (see below).
- **DisplayModule** — the BaseModule wrapper: reads buttons from SharedState, ticks the engine at 100 Hz, routes screen parameters → backend, drives the notification banner, and gives the port a periodic `service(dt)` heartbeat.
- **CharGridLayout** — optional host-tested helper for *character* backends: `MenuView` + `(cols, rows)` → a grid of rows with semantic `RowRole`. The driver decides how a role looks. Pixel backends don't use it.

## Screens

| Screen | Behaviour |
|---|---|
| `MAIN` | Module main values (`main_value` from manifests), e.g. "Термостат 22.5°C". `[OK]` opens the menu. |
| `MENU` | List: module submenus → items. `>` cursor, scrolling, virtual `< Назад` / `< Вихід` entry. Items are **filtered by `caps()`**. |
| `EDIT` | Value editing: UP/DOWN ± `step` clamped to `min`/`max` (from the state declaration); enums cycle `options`; bools toggle. `[OK]` saves to SharedState. |

After 30 s without input the engine auto-returns to `MAIN` (unsaved edits are discarded). Saved values go through the standard SharedState path: NVS persist (if `persist: true`), WS broadcast to WebUI, MQTT publish — and are then routed to the backend by `apply_screen_params()`.

## Navigation: three buttons

The module reads momentary keys from SharedState and clears them back to `false` after handling:

| Key | Event |
|---|---|
| `display.btn_up` | Up / increment |
| `display.btn_down` | Down / decrement |
| `display.btn_select` | Select / save |

Presses can come from anywhere: **WebUI** (the "Дисплей" page has virtual buttons — a full test without hardware), **MQTT**, or **physical buttons** via a `digital_input`/`pcf8574_input` driver writing these keys.

## Capabilities — `caps()` gates the menu

A backend reports a `DisplayCaps` struct; the module reads it once (`on_init`) and uses it to **filter which menu items are shown** — an item declares a required capability (`cap`) in its manifest, and `MenuEngine` hides it when the backend lacks it. So the same manifest produces a rich menu on AMT630A and a trimmed one on a plain backend.

| Capability | Meaning | Gated menu item / control |
|---|---|---|
| `has_color` | programmable palette | colour of text/roles |
| `has_backlight` | PWM backlight | `display.backlight` |
| `has_video_params` | decoder brightness/contrast/saturation | `display.brightness/contrast/saturation` |
| `has_inputs` | CVBS input select (`as_video_inputs()`) | `display.input` |
| `has_backdrop` | no-signal backdrop (snow/blue/black) | `display.backdrop` |
| `has_power` | power-gate (load-switch on a GPIO, `as_power()`) | `display.power` |

Structurally-foreign capabilities are reached via zero-cost `as_*()` accessors (return `nullptr` if absent): `as_video_inputs()`, `as_graphic()`, `as_power()`.

## State keys

| Key | Type | Notes |
|---|---|---|
| `display.enabled` | bool | Disables rendering + button handling (backlight off). Persisted. |
| `display.btn_up/down/select` | bool | Momentary, self-clearing. |
| `display.screen` | string | Current screen: `main`, `menu:<label>`, `edit:<label>`. |
| `display.banner` / `display.banner_level` | string / int | Mirror of the active notification banner (for WebUI/MQTT). |
| `display.backlight` | int 0–100 | PWM backlight % (gated by `has_backlight`). Persisted. |
| `display.brightness/contrast/saturation` | int 0–100 | Decoder video params (gated by `has_video_params`). Persisted. |
| `display.backdrop` | int (0=Snow,1=Blue,2=Black) | No-signal backdrop (gated by `has_backdrop`). Persisted. |
| `display.input` | int (0=AV1,1=AV3) | CVBS input select (gated by `has_inputs`). Persisted. |
| `display.power` | bool | Power-gate the chip rail (gated by `has_power`). Persisted. |

Notifications arrive as a `MsgUiNotice` on the message bus (ADR-001) → `NotificationQueue` (priority + TTL) → `present_notice()`.

## Backends

The active backend is selected at compile time (`idf.py menuconfig` → **ModESP Display**, a Kconfig `choice`) and resolved at runtime from `bindings.json` (`{"driver":"…","role":"display_main","module":"display"}`); geometry/pins come from `board.json`.

| Backend | Chip driver | Notes |
|---|---|---|
| **LogPort** (default) | — | Prints Views to the serial log. `caps()` all false. Works with no hardware. |
| **At7456ePort** | `modesp_osd::At7456e` (SPI) | MAX7456-compatible OSD overlay on analog CVBS (PAL 16×30 / NTSC 13×30). `caps()` all false (pure overlay). |
| **Amt630aPort** | `modesp_osd::Amt630a` (I²C) | Full-featured: colour, per-window hardware scale, 5 OSD windows, CVBS input select, no-signal backdrop, PWM backlight, video params, optional power-gate. `caps()` all true. |

### AMT630A specifics

- **Config:** `board.json` `i2c_displays` entry — `cols`/`rows`, plus `cal_x`/`cal_y` (per-panel overscan offset in px, applied to every OSD window). `bindings.json` may add `"settings": {"power_gpio": N}` to wire a load-switch for the power-gate.
- **Cyrillic font:** loaded into FONT RAM (16×20 1bpp) — the chip's ROM font has no Cyrillic. Map: `drivers/amt630a/include/modesp/osd/amt630a_charmap.h`. Generated by `tools/gen_osd_font.py --target amt630a`.
- **Power / recovery:** `display.power=false` cuts the rail (≈0 mA); `true` powers it back and the port re-initialises the OSD internally (non-blocking, chunked — driven by `service(dt)`), since a cold boot loses all ESP-side OSD state.
- **Full register reference:** [docs/amt630a/AMT630A_control_reference.md](../../../../docs/amt630a/AMT630A_control_reference.md). Energy options: [docs/amt630a/AMT630A_power_modes.md](../../../../docs/amt630a/AMT630A_power_modes.md).

## Attaching a new backend

Implement `IDisplayPort` (semantic seam) — and, for a character display, reuse `CharGridLayout`:

```cpp
#include "display/display_port.h"
#include "display/char_grid.h"

class MyPort : public modesp::display::IDisplayPort {
public:
    bool init() override { /* bus/pins */ return true; }
    modesp::display::DisplayCaps caps() const override { return {}; }   // declare what you support
    void present_main(const modesp::display::MainView& v) override { /* draw idle screen */ }
    void present_menu(const modesp::display::MenuView& v) override {
        modesp::display::CharGrid g;
        modesp::display::CharGridLayout::layout_menu(v, cols_, rows_, g);  // scroll/cursor/clamp
        /* render g.lines (each has a RowRole) */
    }
    void present_edit(const modesp::display::EditView& v) override { /* ... */ }
    void present_notice(const modesp::display::Notice& n) override { /* banner */ }
    void clear_notice() override {}
    // optional: set_backlight/contrast/brightness/saturation/set_backdrop, as_video_inputs/as_power
};
```

`present_*` is called only when the View changes (the driver keeps its own shadow for diffing). Register the backend (`MODESP_REGISTER_DISPLAY(myport, &factory)`) and add a Kconfig choice entry. No change to `DisplayModule` — exactly like adding a sensor/actuator driver.

## Adding your module to the menu

Add a `display:` section to your module's manifest.json (full spec in [manifest.md](../../02-module-author-guide/manifest.md)):

```json
"display": {
  "main_value": {"key": "my.temp", "format": "%.1f°C"},
  "menu_label": "Мій модуль",
  "menu_items": [
    {"label": "Уставка", "key": "my.setpoint"},
    {"label": "Вхід",    "key": "my.input", "cap": "inputs"}
  ]
}
```

Editability, bounds, step, units, and options are taken from the key's `state` declaration — nothing is duplicated. The optional `"cap"` hides the item on backends that lack that capability.

## Tests

- `tools/tests/test_generator.py::TestDisplayScreensGenerator` — tree + `MENU_NODE_CAPS` generation (pytest).
- `tests/host/test_display_menu.cpp` — doctest cases for `MenuEngine`: navigation, clamping, enum/bool, idle timeout, caps-gating.
- `tests/host/test_char_grid.cpp` / `test_notification_queue.cpp` / `test_display_module.cpp` — layout, banner queue, glue. Run: `python -m pytest tools/tests/test_cpp_host.py -v`.
