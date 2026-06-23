# iPixel LED panel — wiring & control

> 📖 **Українською:** [documentation/uk/04-hardware/ipixel-panel.md](../../uk/04-hardware/ipixel-panel.md)

A practical guide: how to connect the Chinese **iPixel Color / LED_BLE 64×16** RGB LED matrix to ModESP over BLE and drive it — from the WebUI (power, brightness, effect, custom message, colour) and programmatically from other modules (text slots). This is a **how-to**; for internals see the reference pages [`drivers/ble_led_panel`](../03-framework-reference/drivers/ble_led_panel.md), [`modules/panel`](../03-framework-reference/modules/panel.md), [`components/modesp_ble`](../03-framework-reference/components/modesp_ble.md).

**REQUIRES:** ESP32-**S3** (BLE), `modesp_ble` enabled (CENTRAL role), an iPixel/LED_BLE panel in range.

## What you need

- An **iPixel Color / LED_BLE 64×16** panel (adv-name like `LED_BLE_XXXXXXXX`).
- An **ESP32-S3** board (e.g. `stand_s3`).
- Wi-Fi — for the WebUI (iPixel tab).

> Find the panel's adv-name with any BLE scanner (nRF Connect) or the official iPixel app — the driver connects by the **`LED_BLE_`** prefix.

## Step 1 — Enable BLE + CENTRAL (Kconfig)

`idf.py menuconfig` → menu **"ModESP BLE"**:

```
[*] CONFIG_MODESP_BLE_ENABLE      # shared BLE host
[*] CONFIG_MODESP_BLE_CENTRAL     # connect to the panel
```

Without `CENTRAL` the driver has nothing to connect to the panel with.

## Step 2 — Declare the panel (board.json + bindings.json)

The active config is `data/board.json` + `data/bindings.json` (the board template lives in `boards/<board>/`). The panel is declared in `ble_devices` by **`name`** (the adv-name, not a MAC — it is a connect device):

```json
// board.json
"ble_devices": [
  {"id": "led_panel", "name": "LED_BLE_E6C5EBE2",
   "label": "iPixel Color 16x64 LED matrix (connect device)"}
]
```

```json
// bindings.json
{"hardware": "led_panel", "driver": "ble_led_panel", "role": "panel", "module": "equipment"}
```

Replace `LED_BLE_E6C5EBE2` with **your** panel's adv-name. Field details: [board-config.md](board-config.md) and [bindings.md](bindings.md).

## Step 3 — Wire in the modules

Content and web control come from the `panel` module; the `ble_led_panel` driver owns the BLE link.

```json
// project.json
"modules": [ "...", "panel" ]
```

```cmake
# main/CMakeLists.txt
PRIV_REQUIRES ... panel
```

The `ble_led_panel` driver becomes available in menuconfig automatically (the generator adds the toggle). If the board binds a driver that is disabled in menuconfig, the build fails with a FATAL — reconcile with `python tools/drivers_sync.py --fix`.

## Step 4 — Build & flash

```bash
idf.py build
idf.py flash monitor
```

On boot the log shows `panel connected (power/brightness/effect/text driven by the panel module)`. The panel turns on and starts rotating clock / temperature / humidity (when those sensors are present).

## Control from the WebUI ("iPixel" tab)

Open the WebUI (the board's IP in a browser) → the **iPixel** tab. Controls apply live and persist across reboots:

| Control | What it does |
|---|---|
| **Power** | Panel ON / OFF |
| **Brightness** | 5..100 % |
| **Rotation** | Auto (clock/temp/humidity) or Pause (hold the frame) |
| **Effect** | Text animation `0..7` (see below) |
| **Message** | Custom text (up to 31 chars) — shown **instead of** the rotation; clear the field → rotation resumes |
| **Colour** | Message colour (native picker) |
| **Module slots** | Read-only view of text posted by other modules (see below) |

The same keys are controllable over **MQTT** (except the text ones: `panel.power` / `panel.brightness` / `panel.rotate` / `panel.anim`).

### Animation effects (`anim`)

HW-confirmed on the panel:

| `anim` | Effect | `anim` | Effect |
|---|---|---|---|
| 0 | static | 4 | top → down |
| 1 | scroll right → left | 5 | blink |
| 2 | scroll left → right | 6 | breathe |
| 3 | bottom → up | 7 | drop-in (assembles row-by-row) |

## Posting text from other modules (slots API)

Any module can put its own line on the panel through **5 shared text slots**. It is a thin convention over SharedState — the slots are ordinary string keys, so you write them with your own `state_set`:

```cpp
#include "modesp/panel_text.h"
// ... from inside a module method (on_update / on_init):
state_set(modesp::panel_text::slot(0), "ALARM");     // post to slot 0
state_set(modesp::panel_text::slot(1), "DEFROST");
state_set(modesp::panel_text::slot(0), "");          // clear slot 0
```

Non-empty slots **rotate on the display** (white) alongside the sensors. An empty (`""`) slot is not shown. Full description: [modules/panel § Text-output API](../03-framework-reference/modules/panel.md#text-output-api-module-slots).

## Common pitfalls

- **Panel not found** — wrong adv-name in `board.json`, `CONFIG_MODESP_BLE_CENTRAL` disabled, or the panel is out of range. Check the name with a BLE scanner.
- **Build FATAL about a disabled driver** — the board binds `ble_led_panel` but it is disabled in menuconfig. `python tools/drivers_sync.py --fix`.
- **Message won't clear in the WebUI** — delete all the text and click outside the field (commit on blur/Enter); the empty value brings back the rotation.
- **Text truncates at 31 chars** — that is the string-state ceiling (`etl::string<32>`); the panel scrolls up to 31 chars.
- **`idf.py` complains about the toolchain version** — a poisoned `build/` after switching IDF versions; run `idf.py fullclean` and rebuild.

## Next steps

- **[modules/panel.md](../03-framework-reference/modules/panel.md)** — the content module reference (icons, colours, effects, slots API).
- **[drivers/ble_led_panel.md](../03-framework-reference/drivers/ble_led_panel.md)** — the driver (BLE link, control plane).
- **[components/modesp_ble.md](../03-framework-reference/components/modesp_ble.md)** — the shared BLE host (observer / central / peripheral).
- **[bindings.md](bindings.md)** — binding drivers to hardware.

## Source

- [`boards/stand_s3/board.json`](../../../boards/stand_s3/board.json), [`boards/stand_s3/bindings.json`](../../../boards/stand_s3/bindings.json)
- [`drivers/ble_led_panel/`](../../../drivers/ble_led_panel/)
- [`modules/panel/`](../../../modules/panel/)
- [`components/modesp_core/include/modesp/panel_text.h`](../../../components/modesp_core/include/modesp/panel_text.h) — slots API
- [`docs/ble/panel_protocol.md`](../../../docs/ble/panel_protocol.md) — panel byte protocol
