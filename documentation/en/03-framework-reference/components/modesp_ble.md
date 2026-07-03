# `modesp_ble` — shared BLE host (observer + central + peripheral)

> 📖 **Українською:** [documentation/uk/03-framework-reference/components/modesp_ble.md](../../../uk/03-framework-reference/components/modesp_ble.md)

`modesp_ble` is the framework's shared Bluetooth Low Energy infrastructure for board `stand_s3` (ESP32-S3). It runs **one** NimBLE host that coexists with Wi-Fi, and lives in `components/modesp_ble` — not in `modules/`. Unlike a product module, it is registered in `main.cpp` as a `BaseModule` **service** at **priority 1**, so it comes up early and is available to the drivers and modules that ride on it.

The host serves **three roles simultaneously**:

| Role | What it does |
|---|---|
| **Observer** | Passive scan. Hands each advertisement's 16-bit service-data to the decoders that BLE sensor drivers register (`adv_decoder.h`); a recognized reading is cached **per MAC** for the bound driver. The transport knows no device format — the decoders themselves (e.g. BTHome `0xFCD2`, pvvx/ATC `0x181A`) live in the driver, e.g. `ble_xiaomi_th`. |
| **Central** | Connects to a device (the iPixel LED panel) and writes commands. Exposed as the `BlePanel` singleton. Connecting **pauses** the observer scan, then **resumes** it afterwards. |
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

## `BlePanel` API (central role)

The central role is exposed as the `BlePanel` singleton — the single point through which the `ble_led_panel` driver owns the BLE link target, and through which the `panel` module — the single writer of power, brightness, effect and content — drives the panel. This decouples **transport** (driver) from **control + displayed content** (module).

```cpp
void set_target(const char* adv_name_prefix);  // adv-name prefix to scan & connect
bool is_connected();
void write_cmd(const uint8_t* data, size_t len, bool with_response);
void show_text(const char* s,
               uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
               uint8_t anim = 0, uint8_t speed = 0x32, uint8_t rainbow = 0);
```

`show_text` renders via the native iPixel TEXT frame (font glyphs + icons + colour + animation). The full panel-control byte protocol lives in [`docs/ble/panel_protocol.md`](../../../../docs/ble/panel_protocol.md).

## Features riding on the host

### Sensor — `ble_xiaomi_th` (observer)

A **sensor** driver (`hardware_type "ble_device"`) that reads a Xiaomi LYWSD03MMC hygro-thermo sensor flashed with custom firmware (pvvx / ATC / BTHome) via the **observer** — no connection, passive broadcast. One physical sensor maps to 3 channels (temperature / humidity / battery), selected by the binding's `address`. Bound to the `equipment` module, it publishes `equipment.room_temp` / `equipment.room_humid` / `equipment.room_batt` (plus `equipment.<role>_ok` health flags). Stale timeout **60 s** — no broadcast → not healthy. See [drivers/ble_xiaomi_th.md](../drivers/ble_xiaomi_th.md).

### Panel — `ble_led_panel` (central) + `panel` module

An **actuator** driver (`hardware_type "ble_device"`, a **connect** device matched by **adv-name**, not MAC) drives a Chinese iPixel Color / LED_BLE 64x16 RGB matrix. It owns only the BLE link target via `BlePanel` (scan adv-name prefix `"LED_BLE_"` → connect → discover write char `fa02` + notify char `fa03`, service `0x00FA` → READY). The `panel` **module** is the single writer of power, brightness *and* what is shown (clock / temperature / humidity rotation, icons, threshold colours, animation), pushing it all through `BlePanel` via `write_cmd` / `show_text`. See [drivers/ble_led_panel.md](../drivers/ble_led_panel.md) and [modules/panel.md](../modules/panel.md).

## See also

- **[drivers/ble_xiaomi_th.md](../drivers/ble_xiaomi_th.md)** — passive observer sensor (Xiaomi LYWSD03MMC).
- **[drivers/ble_led_panel.md](../drivers/ble_led_panel.md)** — connect/control actuator for the iPixel panel.
- **[modules/panel.md](../modules/panel.md)** — the content owner for the LED panel.
- **[`docs/ble/panel_protocol.md`](../../../../docs/ble/panel_protocol.md)** — panel-control byte protocol.

## Sources

- [`components/modesp_ble`](../../../../components/modesp_ble)
- `main.cpp` — `BaseModule` service registration (priority 1)
- `main/CMakeLists.txt` — `idf::modesp_ble` link guard
