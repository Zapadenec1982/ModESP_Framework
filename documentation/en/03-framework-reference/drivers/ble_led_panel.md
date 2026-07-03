# `ble_led_panel` — iPixel / LED_BLE 64×16 RGB panel (BLE connect)

> 📖 **Українською:** [documentation/uk/03-framework-reference/drivers/ble_led_panel.md](../../../uk/03-framework-reference/drivers/ble_led_panel.md)

`ble_led_panel` drives a Chinese **iPixel Color / LED_BLE 64×16 RGB LED matrix** over BLE. Unlike a passive sensor, this is a **connect** device. The driver **owns the entire iPixel wire format**: the GATT UUIDs, the control byte-commands (power / brightness), the native text-frame encoder (glyphs + CRC32 + chunking) and its background render task. It obtains its BLE link by registering a **connect profile** with the shared `modesp_ble` host's generic central-link seam (`central_link.h`) — that transport knows *no* device format. The [`panel`](../modules/panel.md) module owns only the displayed **content**; it drives the driver through the `IPanelPort` interface.

The driver wears **two hats**:

- **`modesp::IActuatorDriver`** — still bound to the `equipment` module; `EquipmentBase` drives `set_value` = brightness (0..1 → 5..100 %). Its `update()` just logs the connect edge and `set()` (power) is a no-op — power is owned by the `panel` module.
- **`modesp::panel::IPanelPort`** — published at factory time via `DriverRegistry::set_panel_port(this)`; the `panel` module resolves it and drives content (power / brightness / text) through it.

The driver registers as an `actuator` with `hardware_type: ble_device`. Unlike `ble_xiaomi_th` (matched by MAC), this device is matched **by advertised name**.

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

The driver supplies its `ConnectProfile` (adv-name prefix + write/notify UUIDs) to `modesp_ble`'s generic central link, which then runs the connect/discover/READY state machine:

```
scan adv-name prefix "LED_BLE_"  ──▶  connect
        │
        └─▶ discover
              ├─ write  char fa02   (commands, bound as the write handle)
              └─ notify char fa03   (subscribed)
        │
        └─▶ READY  ──▶  panel module applies power + brightness + text
```

Connecting pauses the BLE observer scan, then resumes it once connected. The `panel` module is the single content writer and re-applies power + brightness on each (re)connect (sentinel reset), so user settings survive a reconnect with no connect-edge race. (The driver *also* writes brightness via `EquipmentBase` `set_value` — unchanged from before.)

## Commands

Control is a small byte protocol written to the **fa02** write characteristic — encoded and sent by the **driver** (`set_power` / `set_brightness`, invoked by the `panel` module through `IPanelPort`). Full spec: [`docs/ble/panel_protocol.md`](../../../../docs/ble/panel_protocol.md).

| Action | Bytes |
|---|---|
| Power ON | `05 00 07 01 01` |
| Power OFF | `05 00 07 01 00` |
| Brightness | `05 00 04 80 <pct>` (`<pct>` = 0..100) |

## API

Two seams meet in this driver. Toward the transport it registers a `ConnectProfile` and writes through the returned `ICentralLink` (`components/modesp_ble/include/modesp/ble/central_link.h`):

```cpp
// modesp::ble — the generic transport seam the driver supplies device data to:
struct ConnectProfile {
    const char*       name_prefix;   // adv-name prefix to scan+connect ("LED_BLE_")
    const ble_uuid_t* write_uuid;    // fa02 — bound as the write handle
    const ble_uuid_t* notify_uuid;   // fa03 — subscribed
    CentralNotifyCb   on_notify;     // notify sink (nullptr here)
    void*             ctx;
};
ICentralLink* register_connect_profile(const ConnectProfile&);  // called from the factory

class ICentralLink {                          // the driver writes THROUGH this:
    bool connected() const;
    bool write(const uint8_t* data, uint16_t len, bool with_response);
    bool write_frame(bool (*body)(ICentralLink*, void*), void* arg);  // atomic multi-chunk frame
};
```

Toward the `panel` module it implements `IPanelPort` (`components/modesp_hal/include/modesp/hal/panel_port.h`) — the module calls these; the driver encodes the bytes:

```cpp
// modesp::panel::IPanelPort — the semantic surface the panel module drives:
bool connected() const;
void set_power(bool on);                       // encodes 05 00 07 01 <on>
void set_brightness(int pct);                  // encodes 05 00 04 80 <pct>
void show_text(const char* s, uint8_t r, uint8_t g, uint8_t b,
               uint8_t anim, uint8_t speed, uint8_t rainbow);  // enqueues → render task
```

The **driver** owns the whole wire format: it defines the fa02/fa03 UUIDs, encodes the control bytes (`set_power` / `set_brightness`), and encodes the native iPixel **TEXT frame** (font glyphs from `generated/panel_font_data.h` + CRC32, chunked 244 bytes/write through `link->write_frame`) on a background render task. The **panel module** decides *what* to show — it reads `panel.power` (bool), `panel.brightness` (int %), `panel.anim` (int 0..7 effect) and `panel.rotate` (bool) from SharedState (set by the web "iPixel" tab / MQTT) and calls `set_power` / `set_brightness` / `show_text` on the `IPanelPort`; it never encodes a control byte and never touches BLE. See [`panel`](../modules/panel.md) for the displayed readout, threshold colours, and the native animation modes.

> ℹ️ **Why not richer graphics?** DIY per-pixel drawing is one BLE round-trip per pixel — too slow. Full-frame PNG upload works for static images, but on-device PNG *compression* (ROM miniz) exhausts the free heap (~64 KB) on this device, so it isn't viable. The native TEXT frame is the chosen, working path.

## Optionality (Kconfig)

The panel-control feature lives under the **"ModESP BLE"** menu, not "ModESP Drivers":

```
CONFIG_MODESP_BLE_ENABLE    (master — BLE host)
CONFIG_MODESP_BLE_CENTRAL   (panel connect)
```

With `CONFIG_MODESP_BLE_CENTRAL` off, the central connect path (`central_link.h`) is unavailable and the driver has no link to register its profile against. `modesp_ble` PRIV_REQUIRES `bt esp_coex nvs_flash`; `main/CMakeLists` links `idf::modesp_ble` when `CONFIG_MODESP_BLE_ENABLE`.

## See also

- **[modules/panel.md](../modules/panel.md)** — the `panel` module: owns the displayed content (clock / temperature / humidity), icons, threshold colours, native animation.
- **[ble_xiaomi_th.md](ble_xiaomi_th.md)** — the Xiaomi sensor driver (the other feature riding on the BLE host).
- **modesp_ble** — the shared BLE host (observer / central / peripheral roles).
- **[04-hardware/board-config.md](../../04-hardware/board-config.md)** — the `ble_devices` section.
