# `ble_nrf_tilt` — nRF52832 BLE tilt/orientation beacon

> 📖 **Українською:** [../../../uk/03-framework-reference/drivers/ble_nrf_tilt.md](../../../uk/03-framework-reference/drivers/ble_nrf_tilt.md)

A sensor driver for a tilt/orientation beacon built on the **nRF52832** (HolyIOT
21011 module + LIS2DH12 accelerometer). The driver passively listens to BLE
advertisements through the **OBSERVER** role of the shared `modesp_ble` host — no
connection is established. Unlike the Xiaomi sensors, the beacon advertises
**manufacturer data** (company `0xFFFF`) rather than service-data, so the decoder
registers in the transport's manufacturer-decoder pool
(`register_adv_mfg_decoder`). A single physical device yields **six channels**
(tilt angle, tilted flag, battery, raw X/Y/Z axes), each with its own **capability** —
the channel is selected by the `address` field in the binding.

**The data format belongs to the driver.** This device's fields are specific (angle,
tilted, battery, raw axes), so the driver keeps its **own per-MAC cache**
(`NrfTiltReading`) rather than the shared temp/hum/battery cache of the BTHome
sensors. The transport owns only the radio and passive scanning — it does not know the
device format — and hands every manufacturer-data frame to the registered decoders. A
decoder that recognizes its format (company `0xFFFF`, ver=1, exactly 15 bytes, magic
marker `0xA7` at `md[8]`) puts a reading into the per-MAC cache, which the driver reads.

`hardware_type` is `ble`, `transport` is `ble`. **Identity (MAC) lives on the device**
(`RemoteDeviceConfig{transport, identity, name}`), not on the role (R0.3). The beacon
is enrolled at runtime via the **Devices** page (`/data/devices.json`), and the binding
references a **device id**, never a MAC (R4.3). The driver depends on the `modesp_ble`
component, which must be enabled (`CONFIG_MODESP_BLE_CENTRAL`), while the driver itself
is optional (`CONFIG_MODESP_DRIVER_BLE_NRF_TILT`).

## Capability and channels

A role never knows the driver — it declares a **capability** (R0.1, R3.1). A single
physical device feeds several roles; `address` in the binding selects the quantity. This
is a **fixed enum** of channels (`address_channels`), not a bus scan.

| `address` | Capability | Label (picker) | Value |
|-----------|-----------|----------------|----------|
| `angle`   | `angle`     | *(from `capabilities.json`: Angle)* | Tilt angle, ° (`-1` → invalid frame) |
| `tilted`  | `binary_in` | `Tilted (0/1)` | Tilted flag: `1.0` / `0.0` |
| `battery` | `battery`   | *(from `capabilities.json`: Battery mV)* | Battery, mV |
| `ax`      | `accel`     | `Axis X (raw)` | Raw accelerometer X axis |
| `ay`      | `accel`     | `Axis Y (raw)` | Raw Y axis |
| `az`      | `accel`     | `Axis Z (raw)` | Raw Z axis |

The `angle`/`battery` labels are **omitted** in the manifest and derived from
`capabilities.json` (R1.3). `tilted` and the three axes carry explicit labels:
`binary_in`/`accel` are generic capabilities, so the driver names the channels to tell
them apart in the picker. The generator emits these channels in
`role.channels_by_driver` — the channel `<select>` appears only when there are 2+
channels of the same capability (e.g. the three `accel` axes); a single channel
auto-binds (R3.5).

A device is considered unhealthy (`is_healthy() == false`) if no adv frame has arrived
within `stale_ms` (60 s by default).

## Bindings

### devices.json (runtime)

The beacon is a remote device: it shows up in the unified BLE scan (`GET /api/ble/scan`)
even before any binding, because the decoder registers at BOOT. The operator enrolls it
on the **Devices** page; the enrollment is written by the device into `/data/devices.json`
(R4.3 — runtime-only, never a build input, gitignored):

```json
{
  "devices": [
    { "id": "tank_tilt", "transport": "ble", "identity": "e2:81:15:44:aa:03", "name": "Tank tilt" }
  ]
}
```

### bindings.json

One entry per channel — several bindings of the same `hardware` (device id) with
different `address` / `role`. `hardware` references the device id; there is no MAC on the
binding:

```json
[
  { "hardware": "tank_tilt", "driver": "ble_nrf_tilt", "role": "tank_angle",  "module": "equipment", "address": "angle" },
  { "hardware": "tank_tilt", "driver": "ble_nrf_tilt", "role": "tank_tipped", "module": "equipment", "address": "tilted" },
  { "hardware": "tank_tilt", "driver": "ble_nrf_tilt", "role": "tank_batt",   "module": "equipment", "address": "battery" }
]
```

`binding.module` must name a module from `project.json` that owns the role (R3.4).
Modules never touch BLE directly — the driver does the I/O, the module only reads
SharedState (R3.3).

## Settings

A single per-binding setting (manifest `settings`), edited in the WebUI on the **Sensor
settings** page (card `nRF tilt: {{hardware_id}}`, `access_level: service`):

| Key | Type | Default | Range | Description |
|------|-----|--------|----------|------|
| `stale_ms` | int | `60000` | `5000`…`600000`, step `1000` (ms) | No adv frame for longer → sensor unhealthy |

## Protocol

The beacon advertises manufacturer data with company `0xFFFF` (a shared test id), so the
driver makes the match deterministic: exactly 15 bytes, `ver=1`, magic marker `0xA7` at
`md[8]` (a constant from the nRF firmware). Frame layout (including the 2-byte company
prefix):

```
[0..1]  FF FF            company
[2]     ver = 0x01
[3]     flags            bit0 = tilted
[4]     tilt_deg         0xFF = invalid (angle → -1)
[5..6]  vbat_mV          LE
[7]     seq
[8]     0xA7             magic marker (deterministic gate)
[9..14] ax, ay, az       int16 LE — raw accelerometer axes
```

The decoder also calls `ble::report_seen(mac, rssi, "ble_nrf_tilt", "45° 2900mV")` — a
short string of the current readings, so the operator can tell two nRF sensors apart in
the scan (tilt one — and see which row's angle changes live).

The byte parsing lives **in the driver** — the transport does not know the format. The
device id → MAC identity is resolved from the merged registry (devices.json ∪ board.json)
via `hal.find_ble_device`; the byte order of the display MAC is reversed into NimBLE
`addr.val` (little-endian), so the factory and the decoder key **the same** cache slot by
MAC.

## Common pitfalls

- **MAC on the binding.** Identity is on the device (devices.json), not on the role. A
  MAC in bindings.json ties the role to the transport and violates R0.3.
- **Binding before enrollment.** First enroll the device on **Devices** (it appears in
  the scan); only then does `hardware: <id>` resolve. Otherwise — `not in board.json nor devices.json`.
- **Driver disabled in menuconfig.** A board that binds a disabled driver → FATAL at
  build time. Reconcile: `python tools/drivers_sync.py --fix` (R8.3).
- **Cache pool — 6 distinct MACs.** `MAX_NRF_DEVICES = 6`; a seventh physical beacon won't
  fit in the cache. Bindings (channels) — up to `6 × 6`.
- **Do not edit `data/`.** Edit board/bindings in `boards/<board>/`; don't leave
  `devices.json` in `data/` during the build (it would end up in data.bin and overwrite the
  real enrollments).

## Next steps

- **[bindings.md](../../04-hardware/bindings.md)** — bindings.json syntax and how
  `address` selects a channel.
- **[ble_xiaomi_th.md](ble_xiaomi_th.md)** — a neighboring BLE driver (service-data,
  shared cache) — contrast with the manufacturer-data + own-cache of this driver.
- **[ble_led_panel.md](ble_led_panel.md)** — a BLE actuator (panel), the other capability direction.
- **[project-hierarchy.md](../project-hierarchy.md)** — the peripheral route
  Module↔Role↔Device↔Binding + invariants.
- **[rules.md](../rules.md)** — R0.1 (role=capability), R0.3 (identity on the device),
  R3.5 (per-driver channels), R4.1 (`RemoteDeviceConfig`).

## Source

- [drivers/ble_nrf_tilt/manifest.json](../../../../drivers/ble_nrf_tilt/manifest.json) —
  capability/channels/settings/hardware_type/transport.
- [drivers/ble_nrf_tilt/src/ble_nrf_tilt_driver.cpp](../../../../drivers/ble_nrf_tilt/src/ble_nrf_tilt_driver.cpp) —
  decoder + own cache + factory + register hook.
- [drivers/ble_nrf_tilt/include/ble_nrf_tilt_driver.h](../../../../drivers/ble_nrf_tilt/include/ble_nrf_tilt_driver.h) —
  `NrfTiltReading`, `Channel`, frame layout.
- [components/modesp_ble/](../../../../components/modesp_ble/) — transport (OBSERVER,
  `adv_decoder.h`, `report_seen`).
