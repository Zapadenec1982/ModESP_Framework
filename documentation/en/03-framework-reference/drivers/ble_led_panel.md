# `ble_led_panel` — iPixel / LED_BLE 64×16 RGB panel (BLE connect)

> 📖 **Українською:** [documentation/uk/03-framework-reference/drivers/ble_led_panel.md](../../../uk/03-framework-reference/drivers/ble_led_panel.md)

`ble_led_panel` drives a Chinese **iPixel Color / LED_BLE 64×16 RGB LED matrix** over BLE. Unlike a passive sensor, this is a **connect** device: the driver owns the BLE link and the control plane through the shared `modesp_ble` host (CENTRAL role + the `BlePanel` singleton). It controls only **power** (ON/OFF) and **brightness** (0–100 %) — the displayed **content** belongs to the [`panel`](../modules/panel.md) module, decoupled through `BlePanel`.

The driver registers as an `actuator` with `hardware_type: ble_device`. Unlike `ble_xiaomi_th` (matched by MAC), this device is matched **by advertised name**. On connect the panel is self-driving: it turns ON at the configured brightness.

REQUIRES: the `modesp_ble` component with `CONFIG_MODESP_BLE_ENABLE` and `CONFIG_MODESP_BLE_CENTRAL` (panel connect).

## Hardware (board.json)

The panel is declared in the `ble_devices` section by **advertised name** (not MAC) — `name` is the adv-name prefix the central scans for and connects to:

```json
"ble_devices": [
  { "id": "led_panel", "name": "LED_BLE_E6C5EBE2" }
]
```

| Field | Meaning |
|---|---|
| `id` | Logical name referenced by `hardware` in bindings. |
| `name` | Advertised name to connect to (scan matches the `LED_BLE_` prefix). |

## Bindings

A single binding, bound to the `equipment` module:

```json
{"hardware": "led_panel", "driver": "ble_led_panel", "role": "panel", "module": "equipment"}
```

## Connect flow

The driver owns transport and the control plane through `BlePanel`:

```
scan adv-name prefix "LED_BLE_"  ──▶  connect
        │
        └─▶ discover service 0x00FA
              ├─ write  char fa02   (commands)
              └─ notify char fa03
        │
        └─▶ READY  ──▶  power ON at configured brightness (self-driving)
```

Connecting pauses the BLE observer scan, then resumes it once connected.

## Commands

Control is a small byte protocol written to the **fa02** write characteristic. Full spec: [`docs/ble/panel_protocol.md`](../../../../docs/ble/panel_protocol.md).

| Action | Bytes |
|---|---|
| Power ON | `05 00 07 01 01` |
| Power OFF | `05 00 07 01 00` |
| Brightness | `05 00 04 80 <pct>` (`<pct>` = 0..100) |

## API

Content is sent through the `BlePanel` singleton, shared with the `panel` module:

```cpp
void set_target(const char* name_prefix);   // adv-name prefix to connect to
bool is_connected();
void write_cmd(const uint8_t* data, size_t len, bool with_response);
void show_text(const char* s,
               uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
               uint8_t anim = 0, uint8_t speed = 0x32, uint8_t rainbow = 0);
```

The **driver** owns transport/control only (power + brightness). The **panel module** owns *what* is shown — it calls `BlePanel::show_text` with the native iPixel TEXT frame (font glyphs + icons + colour + animation). See [`panel`](../modules/panel.md) for the displayed readout, threshold colours, and the native animation modes.

> ℹ️ **Why not richer graphics?** DIY per-pixel drawing is one BLE round-trip per pixel — too slow. Full-frame PNG upload works for static images, but on-device PNG *compression* (ROM miniz) exhausts the free heap (~64 KB) on this device, so it isn't viable. The native TEXT frame is the chosen, working path.

## Optionality (Kconfig)

The panel-control feature lives under the **"ModESP BLE"** menu, not "ModESP Drivers":

```
CONFIG_MODESP_BLE_ENABLE    (master — BLE host)
CONFIG_MODESP_BLE_CENTRAL   (panel connect)
```

With `CONFIG_MODESP_BLE_CENTRAL` off, the central / `BlePanel` connect path is unavailable. `modesp_ble` PRIV_REQUIRES `bt esp_coex nvs_flash`; `main/CMakeLists` links `idf::modesp_ble` when `CONFIG_MODESP_BLE_ENABLE`.

## See also

- **[modules/panel.md](../modules/panel.md)** — the `panel` module: owns the displayed content (clock / temperature / humidity), icons, threshold colours, native animation.
- **[ble_xiaomi_th.md](ble_xiaomi_th.md)** — the Xiaomi sensor driver (the other feature riding on the BLE host).
- **modesp_ble** — the shared BLE host (observer / central / peripheral roles).
- **[04-hardware/board-config.md](../../04-hardware/board-config.md)** — the `ble_devices` section.
