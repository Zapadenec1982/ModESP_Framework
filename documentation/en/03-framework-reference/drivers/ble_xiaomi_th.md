# `ble_xiaomi_th` — Xiaomi BLE temp/humidity/battery sensor

> 📖 **Українською:** [documentation/uk/03-framework-reference/drivers/ble_xiaomi_th.md](../../../uk/03-framework-reference/drivers/ble_xiaomi_th.md)

`ble_xiaomi_th` reads a **Xiaomi LYWSD03MMC** hygro-thermometer flashed with custom firmware (**pvvx / ATC / BTHome**) over BLE. There is **no connection**: the sensor broadcasts its readings, and the driver receives them passively through the shared BLE **observer** (`modesp_ble`). One physical sensor exposes three values — temperature, humidity, battery — each a distinct `capability`, selected by the binding's `address` (R3.5).

The driver registers as a `sensor` with `hardware_type: ble` (`transport: "ble"`). **It owns the wire format**: at factory time it registers its advertisement decoders (pvvx/ATC `0x181A`, BTHome `0xFCD2`) with `modesp_ble` via `adv_decoder.h`. The transport owns only the radio and the passive scan — it knows no device format — and hands every 16-bit service-data frame to the registered decoders. A decoder that recognizes its format publishes the reading (`ble::report_sensor`) into the per-MAC cache the driver reads. A role binds by **capability** (R0.1/R3.1), never by driver or MAC; the **device** carries the identity (MAC), never the role (R0.3) — factory resolves device `id`→MAC via `find_ble_device` (alias of `find_remote_device`). If no broadcast arrives for the `stale_ms` window (**default 60 s**) the channel goes stale and the driver reports unhealthy.

REQUIRES: the `modesp_ble` component (BLE host + observer) enabled via `CONFIG_MODESP_BLE_ENABLE`.

## Hardware (board.json)

The sensor is declared in the `ble_devices` section by MAC (`ble_devices` is the legacy key alias of the transport-generic `remote_devices`, R4.1):

```json
"ble_devices": [
  { "id": "xiaomi_room", "mac": "a4:c1:38:b4:dc:11", "format": "auto" }
]
```

| Field | Meaning |
|---|---|
| `id` | Logical name referenced by `hardware` in bindings. |
| `mac` | The device's `identity` — the BLE MAC the observer matches advertisements against. Lives on the device, never on the role (R0.3). |
| `format` | Advertisement format: `"auto"` auto-detects pvvx / ATC / BTHome. |

## Bindings & address channels

One physical sensor exposes several values, each a distinct `capability`. Since `read()` returns a single number, **the `address` field selects the channel**. The generator emits the channels into `role.channels_by_driver`; a channel `<select>` appears only when a capability has 2+ channels, otherwise the single channel auto-binds (R3.5). All three roles point at the same `hardware` (device id) and differ only by `address`:

| `address` | `capability` | Channel | Units |
|---|---|---|---|
| `temperature` | `temperature` | air temperature | °C |
| `humidity` | `humidity` | relative humidity | % |
| `battery` | `battery` | battery level | % |

```json
{"hardware": "xiaomi_room", "driver": "ble_xiaomi_th", "role": "room_temp",  "address": "temperature", "module": "equipment"},
{"hardware": "xiaomi_room", "driver": "ble_xiaomi_th", "role": "room_humid", "address": "humidity",    "module": "equipment"},
{"hardware": "xiaomi_room", "driver": "ble_xiaomi_th", "role": "room_batt",  "address": "battery",     "module": "equipment"}
```

Bound to the `equipment` module, EquipmentBase publishes each role to `equipment.<role>` and a per-role health flag to `equipment.<role>_ok`:

| Role | State key | Health flag |
|---|---|---|
| `room_temp` | `equipment.room_temp` | `equipment.room_temp_ok` |
| `room_humid` | `equipment.room_humid` | `equipment.room_humid_ok` |
| `room_batt` | `equipment.room_batt` | `equipment.room_batt_ok` |

The `_ok` flag clears once the stale timeout (60 s with no broadcast) elapses.

## Read architecture

```
LYWSD03MMC ──BLE adv──▶ modesp_ble (OBSERVER: radio + passive scan)
   (pvvx/ATC/BTHome)         │  dispatch each service-data frame
                             ▼
              ble_xiaomi_th decoders (0x181A / 0xFCD2)   ← parse bytes (in the driver)
                             │  ble::report_sensor → per-MAC cache (BleCentral)
                  ┌──────────┼──────────┐
            temperature   humidity   battery   ← ble_xiaomi_th (per-channel view)
                  │           │          │
                  └─ equipment.room_* ◀── EquipmentBase (read(), publishes)
                              │
                          equipment.<role>_ok  (stale > 60 s → false)
```

## Consumers

The published `equipment.room_*` keys feed the rest of the firmware — e.g. the **panel** module reads `equipment.room_temp` / `equipment.room_humid` (each health-gated on `equipment.<role>_ok`) to display the temperature and humidity readout on the LED panel.

## See also

- **[ble_led_panel.md](ble_led_panel.md)** — the iPixel LED panel actuator driver (the other feature riding on the BLE host).
- **modesp_ble** — the shared BLE host (observer / central / peripheral roles).
- **[04-hardware/board-config.md](../../04-hardware/board-config.md)** — the `ble_devices` section.
