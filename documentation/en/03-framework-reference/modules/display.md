# `display` — on-device menu generated from manifests

> 📖 **Українською:** [documentation/uk/03-framework-reference/modules/display.md](../../../uk/03-framework-reference/modules/display.md)

`display` is a generic framework module that renders the device's local screen menu. Like WebUI and MQTT, the menu is **generated from manifests**: each module describes a `display:` section in its manifest.json, the generator merges them into one constexpr tree (`generated/display_screens.h`), and this module handles navigation, value display, and parameter editing through SharedState.

The module is **hardware-agnostic**: it builds a text frame (`DisplayFrame`, 4 UTF-8 rows) and hands it to a renderer through the `IDisplayRenderer` interface. Without hardware the `LogRenderer` is used — the frame shows up in the serial log. A real display driver (SSD1306, HD44780, TFT) implements the same interface and is attached via `set_renderer()`.

REQUIRES: `modesp_core`. No GPIO — SharedState only.

## How it works

```
manifest.json (display:) ──┐
manifest.json (display:) ──┼→ generate_ui.py → display_screens.h (MENU_NODES, MAIN_VALUES)
manifest.json (display:) ──┘                          │
                                                      ▼
buttons (WebUI / MQTT / GPIO) → SharedState → DisplayModule → MenuEngine → DisplayFrame → IDisplayRenderer
```

- **MenuEngine** — pure logic (host-tested, zero heap): the `MAIN → MENU → EDIT` FSM.
- **DisplayModule** — the BaseModule wrapper: reads buttons from SharedState, ticks the engine at 100 Hz, renders the frame only when it changes.

## Screens

| Screen | Behaviour |
|---|---|
| `MAIN` | Module main values (`main_value` from manifests): "Термостат 22.5°C". Pages rotate every 4 s; `[OK]` opens the menu. |
| `MENU` | List: module submenus → items. `>` cursor, scrolling, virtual `< Назад` / `< Вихід` entry. |
| `EDIT` | Value editing: UP/DOWN ± `step` clamped to `min`/`max` (from the state declaration); enums cycle `options`; bools toggle. `[OK]` saves to SharedState. |

After 30 s without input the engine auto-returns to `MAIN` (unsaved edits are discarded). Saved values go through the standard SharedState path: NVS persist (if `persist: true`), WS broadcast to WebUI, MQTT publish.

## Navigation: three buttons

The module reads momentary keys from SharedState and clears them back to `false` after handling:

| Key | Event |
|---|---|
| `display.btn_up` | Up / increment |
| `display.btn_down` | Down / decrement |
| `display.btn_select` | Select / save |

Presses can come from anywhere: **WebUI** (the "Дисплей" page has virtual buttons — a full test without hardware), **MQTT**, or **physical buttons** via a `digital_input`/`pcf8574_input` driver writing these keys.

## State keys

| Key | Type | Notes |
|---|---|---|
| `display.enabled` | bool | Disables rendering and button handling. Persisted. |
| `display.btn_up/down/select` | bool | Momentary, self-clearing. |
| `display.screen` | string | Current screen: `main`, `menu:root`, `menu:<label>`, `edit:<label>`. |

## Attaching a display driver

```cpp
#include "display/renderer.h"

class Ssd1306Renderer : public modesp::display::IDisplayRenderer {
public:
    bool init() override { /* i2c init */ return true; }
    void render(const modesp::display::DisplayFrame& f) override {
        // f.rows[0..3] — UTF-8 strings; draw and flush
    }
};
```

`render()` is only called when the frame changes. The renderer decides how many glyphs fit; frame rows are capped at 40 UTF-8 bytes.

### Built-in renderer: AT7456E (OSD composite overlay)

The framework ships a built-in renderer for the **AT7456E** (MAX7456-compatible OSD chip) — it overlays a character grid (PAL 16×30 / NTSC 13×30) on an analog composite video signal. It needs a video monitor on the CVBS output; the chip can run on its own internal sync, so the screen lights up even with no input video.

The driver is portable — component `components/modesp_osd/` (shared with sibling projects). Enable it in `idf.py menuconfig` → **ModESP Display → AT7456E OSD renderer**, which also sets the pins (CS/DATA/CLK/MISO), video standard, and sync. When enabled, `DisplayModule` defaults to `AT7456ERenderer` instead of `LogRenderer`.

**Font and Cyrillic.** The AT7456E keeps its font in character NVM (256 glyphs, 12×18px). The stock font has **no Cyrillic**, so a custom font is uploaded to NVM for the Ukrainian UI. The layout is defined by [osd_charmap.h](../../../../components/modesp_osd/include/modesp/osd/osd_charmap.h) (ASCII identity, Cyrillic U+0410-044F → 0x80+, Ukrainian specials Є/І/Ї/Ґ at fixed indices), and the driver can flash the font (`upload_font`, with a sentinel check to avoid re-flashing NVM). The `.mcm` font itself is produced by `tools/gen_osd_font.py` *(in progress)*; until the font is flashed, Cyrillic renders as `?`.

## Adding your module to the menu

Add a `display:` section to your module's manifest.json (full spec in [manifest.md](../../02-module-author-guide/manifest.md)):

```json
"display": {
  "main_value": {"key": "my.temp", "format": "%.1f°C"},
  "menu_label": "Мій модуль",
  "menu_items": [
    {"label": "Уставка", "key": "my.setpoint"}
  ]
}
```

Editability, bounds, step, units, and options are taken from the key's `state` declaration — nothing is duplicated.

## Tests

- `tools/tests/test_generator.py::TestDisplayScreensGenerator` — tree generation (pytest).
- `tests/host/test_display_menu.cpp` — 16 doctest cases for MenuEngine: navigation, clamping, enum/bool, idle timeout. Run: `python -m pytest tools/tests/test_cpp_host.py -v`.
