# `presence` — occupancy module

> 📖 **Українською:** [documentation/uk/03-framework-reference/modules/presence.md](../../../uk/03-framework-reference/modules/presence.md)

Presence/occupancy business module on top of the [`ld2410b`](../drivers/ld2410b.md) mmWave radar. A **pure SharedState consumer** (like [`simple_thermo`](simple_thermo.md)): it never touches the driver — it reads `equipment.presence` (published by EquipmentBase from the bound radar), applies logic, and emits `presence.*` to the web UI, MQTT, and DataLogger.

## Data flow

```
ld2410b ──▶ EquipmentBase ──▶ equipment.presence / equipment.move_distance / equipment.still_distance
                                          │
                                  presence module
              (enable → zone-gate(max_distance) → occupancy-hold(hold_sec))
                                          │
                          presence.detected / presence.state / presence.*
                                          │
                              web "Presence" · OSD · MQTT · DataLogger (event 40)
```

Expected binding roles (driver `ld2410b`): `presence` (required), optionally `move_distance` (address=moving) and `still_distance` (address=static) — for moving/static classification and the software distance filter.

## Settings (web, persisted)

Declared as module state keys (`access: readwrite`, `persist: true`) — the generator wires them into the web UI / NVS / MQTT automatically:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `presence.enabled` | bool | true | Enable/disable detection. |
| `presence.hold_sec` | int 0-300 s | 5 | Occupancy hold after the target clears (0 = instant). |
| `presence.max_distance` | int 0-800 cm | 600 | **Ignore targets beyond this** (cm-precise, 10 cm step; 0 = off). Requires a bound distance channel. |

## Indication (read-only state)

| Key | Meaning |
|---|---|
| `presence.detected` | Presence (after gating + hold) — the primary signal. |
| `presence.state` | `disabled`/`none`/`present`/`moving`/`static`/`both`. |
| `presence.moving_distance` / `presence.static_distance` | Distances, cm. |
| `presence.idle_sec` | Seconds since presence. |
| `presence.sensor_ok` | Sensor health (from `equipment.presence_ok`). |

> **For consumers:** read **`presence.detected`** (gated), not raw `equipment.presence` (full range, ignores `max_distance`).

## Logic

`enabled && sensor_ok && raw && !gated`, then an occupancy hold of `hold_sec`. `gated` = the nearest target is beyond `max_distance` (when a distance channel is bound). The `sensor_ok` gate keeps presence from latching ON when the radar disconnects.

## MQTT / DataLogger

Publishes `presence.detected/state/moving_distance/idle_sec`; subscribes to `enabled/hold_sec/max_distance`. DataLogger event `presence.detected` (id 40, both-edge).

## Optionality

The module is in `project.json` → always compiled (lightweight, inert without the radar). The **driver** is optional via `CONFIG_MODESP_DRIVER_LD2410B` — see [`drivers/ld2410b.md`](../drivers/ld2410b.md#optionality-kconfig).

## Sources

- [`modules/presence/manifest.json`](../../../../modules/presence/manifest.json)
- [`modules/presence/src/presence_module.cpp`](../../../../modules/presence/src/presence_module.cpp)
- [`drivers/ld2410b.md`](../drivers/ld2410b.md) — the radar driver.
