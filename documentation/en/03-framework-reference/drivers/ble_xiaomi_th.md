# `ble_xiaomi_th` — Xiaomi BLE temp/humidity/battery sensor

> 📖 **Українською:** [documentation/uk/03-framework-reference/drivers/ble_xiaomi_th.md](../../../uk/03-framework-reference/drivers/ble_xiaomi_th.md)

`ble_xiaomi_th` reads a **Xiaomi LYWSD03MMC** hygro-thermometer flashed with custom firmware (**pvvx / ATC / BTHome**) over BLE. There is **no connection**: the sensor broadcasts its readings, and the driver receives them passively through the shared BLE **observer** (`modesp_ble`). One physical sensor exposes three values — temperature, humidity, battery — each selected by the binding's `address`.

The driver registers as a `sensor` with `hardware_type: ble_device`. **It owns the wire format**: at factory time it registers its advertisement decoders (pvvx/ATC `0x181A`, BTHome `0xFCD2`) with `modesp_ble` via `adv_decoder.h`. The transport owns only the radio and the passive scan — it knows no device format — and hands every 16-bit service-data frame to the registered decoders. A decoder that recognizes its format publishes the reading (`ble::report_sensor`) into the per-MAC cache the driver reads. The device is matched **by MAC**; if no broadcast arrives for **60 s** the channel goes stale and the driver reports unhealthy.

REQUIRES: the `modesp_ble` component (BLE host + observer) enabled via `CONFIG_MODESP_BLE_ENABLE`.

## Hardware (board.json)

The sensor is declared in the `ble_devices` section by MAC:

```json
"ble_devices": [
  { "id": "xiaomi_room", "mac": "a4:c1:38:b4:dc:11", "format": "auto" }
]
```

| Field | Meaning |
|---|---|
| `id` | Logical name referenced by `hardware` in bindings. |
| `mac` | BLE MAC the observer matches advertisements against. |
| `format` | Advertisement format: `"auto"` auto-detects pvvx / ATC / BTHome. |

## Bindings & address channels

One physical sensor exposes several values. Since `read()` returns a single number, **the `address` field selects the channel**. All three roles point at the same `hardware` (MAC) and differ only by `address`:

| `address` | Channel | Units |
|---|---|---|
| `temperature` | air temperature | °C |
| `humidity` | relative humidity | % |
| `battery` | battery level | % |

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
