# `modesp_ble` — shared BLE host (observer + central + peripheral)

> 📖 **Українською:** [documentation/uk/03-framework-reference/components/modesp_ble.md](../../../uk/03-framework-reference/components/modesp_ble.md)

`modesp_ble` is the framework's shared Bluetooth Low Energy infrastructure for board `stand_s3` (ESP32-S3). It runs **one** NimBLE host that coexists with Wi-Fi, and lives in `components/modesp_ble` — not in `modules/`. Unlike a product module, it is registered in `main.cpp` as a `BaseModule` **service** at **priority 1**, so it comes up early and is available to the drivers and modules that ride on it.

The host serves **three roles simultaneously**:

| Role | What it does |
|---|---|
| **Observer** | Passive scan. Hands each advertisement's 16-bit service-data to the decoders that BLE sensor drivers register (`adv_decoder.h`); a recognized reading is cached **per MAC** for the bound driver. The transport knows no device format — the decoders themselves (e.g. BTHome `0xFCD2`, pvvx/ATC `0x181A`) live in the driver, e.g. `ble_xiaomi_th`. |
| **Central** | Connects to a device and writes/subscribes to its GATT characteristics. A CONNECT driver registers a `ConnectProfile` (adv-name + write/notify UUIDs) via `central_link.h` and gets a generic `ICentralLink` — the transport knows no device format. Connecting **pauses** the observer scan, then **resumes** it afterwards. |
| **Peripheral** | Its own GATT server (telemetry/control + Wi-Fi provisioning), advertising the name `"ModESP"`. |

Modules never touch BLE (or GPIO) directly — **drivers do the I/O, modules read/write SharedState**. The Xiaomi sensor (`ble_xiaomi_th`, passive observer) and the iPixel panel (`ble_led_panel` connect + `panel` module content) are two independent features riding on this one host.

## Build & configuration (Kconfig)

Menu **"ModESP BLE"**:

| Symbol | Meaning |
|---|---|
| `CONFIG_MODESP_BLE_ENABLE` | Master switch for the whole component. |
| `CONFIG_MODESP_BLE_CENTRAL` | Central role — connect to the LED panel. |
| `CONFIG_MODESP_BLE_PROVISIONING` | Wi-Fi provisioning over the GATT server. |

`PRIV_REQUIRES`: `bt`, `esp_coex`, `nvs_flash`. `main/CMakeLists.txt` links `idf::modesp_ble` only when `CONFIG_MODESP_BLE_ENABLE` is set.

Targets ESP-IDF v5.5 / v6.0, NimBLE host.

## Central link (`central_link.h` seam)

The central role is exposed **generically** — the transport owns the radio and the
connect/discover/write state machine but no device knowledge. A CONNECT driver
registers a profile at factory time (idempotent) and writes through the returned link:

```cpp
struct ConnectProfile {                 // driver-supplied device knowledge (DATA)
    const char*       name_prefix;      // adv-name prefix to scan & connect
    const ble_uuid_t* write_uuid;       // characteristic captured as the write handle
    const ble_uuid_t* notify_uuid;      // subscribe (CCCD enable); nullptr = write-only
    CentralNotifyCb   on_notify; void* ctx;   // notify RX sink (host task); nullptr ok
};
ICentralLink* register_connect_profile(const ConnectProfile&);

class ICentralLink {
    bool connected() const;
    bool write(const uint8_t* data, uint16_t len, bool with_response);
    bool write_frame(bool (*body)(ICentralLink*, void*), void* arg);   // atomic multi-write
};
```

`write_frame` holds the transport's recursive write-mutex across the whole `body`, so a
driver can build **and chunk** a multi-write frame (e.g. an image/text frame) atomically
against control writes from other callers. All device byte-encoding, GATT UUIDs and fonts
live in the driver. This is the CONNECT analogue of `adv_decoder.h` (observer sensors).
The `panel` module drives content through the driver's [`IPanelPort`](../../../../components/modesp_hal/include/modesp/hal/panel_port.h)
(resolved by role: `find_actuator(role)->as_panel()`), never touching BLE. Panel byte protocol:
[`docs/ble/panel_protocol.md`](../../../../docs/ble/panel_protocol.md).

## Features riding on the host

### Sensor — `ble_xiaomi_th` (observer)

A **sensor** driver (`hardware_type "ble_device"`) that reads a Xiaomi LYWSD03MMC hygro-thermo sensor flashed with custom firmware (pvvx / ATC / BTHome) via the **observer** — no connection, passive broadcast. One physical sensor maps to 3 channels (temperature / humidity / battery), selected by the binding's `address`. Bound to the `equipment` module, it publishes `equipment.room_temp` / `equipment.room_humid` / `equipment.room_batt` (plus `equipment.<role>_ok` health flags). Stale timeout **60 s** — no broadcast → not healthy. See [drivers/ble_xiaomi_th.md](../drivers/ble_xiaomi_th.md).

### Panel — `ble_led_panel` (central) + `panel` module

A driver (`hardware_type "ble"`, a **connect** device matched by **adv-name**, not MAC) drives a Chinese iPixel Color / LED_BLE 64x16 RGB matrix. It registers a `ConnectProfile` (adv-name prefix `"LED_BLE_"` + write char `fa02` / notify char `fa03`) with the central link, and **owns all the iPixel wire format**: the control byte-commands, the native text-frame encoder + glyph font, and a background render task. It also implements `IPanelPort`, which the `panel` **module** — the content owner (clock / temperature / humidity rotation, icons, threshold colours, animation) — resolves by role (`find_actuator(role)->as_panel()`) and drives with `set_power` / `set_brightness` / `show_text`. The module never mentions BLE. See [drivers/ble_led_panel.md](../drivers/ble_led_panel.md) and [modules/panel.md](../modules/panel.md).

## See also

- **[drivers/ble_xiaomi_th.md](../drivers/ble_xiaomi_th.md)** — passive observer sensor (Xiaomi LYWSD03MMC).
- **[drivers/ble_led_panel.md](../drivers/ble_led_panel.md)** — connect/control actuator for the iPixel panel.
- **[modules/panel.md](../modules/panel.md)** — the content owner for the LED panel.
- **[`docs/ble/panel_protocol.md`](../../../../docs/ble/panel_protocol.md)** — panel-control byte protocol.

## Sources

- [`components/modesp_ble`](../../../../components/modesp_ble)
- `main.cpp` — `BaseModule` service registration (priority 1)
- `main/CMakeLists.txt` — `idf::modesp_ble` link guard
