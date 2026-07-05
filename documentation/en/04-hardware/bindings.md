# bindings.json — wiring drivers to roles

> 📖 **Українською:** [documentation/uk/04-hardware/bindings.md](../../uk/04-hardware/bindings.md)

`bindings.json` is the **deployment-specific wiring** — which driver type
handles which hardware pin, і what semantic **role** (a name like
`air_temp`, `actuator_1`) it provides to the rest of the system. While
`board.json` says "I have these GPIO outputs", `bindings.json` says "GPIO
relay 1 is actuator_1, talk to it through the `relay` driver".

> **Role = capability, never a driver (R0.1).** A role accepts its
> hardware by **capability** (temperature / relay_out / …) and direction
> (in/out), not by driver name (R3.1). The `driver` in a binding only
> says *what to instantiate* for this particular piece of hardware; the
> module consuming the role never knows the driver. The same thermostat
> takes "temperature" whether it comes from `ds18b20`, `ntc`, or a future
> BLE channel. `address` (where present) selects a **channel** within that
> hardware (e.g. the ROM of one specific sensor on a bus) — not a device
> identity.

This page is the reference для writing bindings, supplemented із real
examples з the dev і KC868-A6 reference boards.

## Where bindings.json sits у the pipeline

```
   board.json              bindings.json            modules
   ─────────               ─────────────            ───────
   "GPIO 14 exists"   →    "GPIO 14 is actuator_1   →   read_bool("equipment.actuator_1")
                            driven by relay driver"
```

Three pieces decouple:

1. **Board capabilities** stay constant per hardware revision.
2. **Bindings** vary per deployment (cold room vs. greenhouse vs. brewing
   setup may reuse the same PCB із different role assignments).
3. **Modules** only care about role names (`equipment.air_temp`,
   `equipment.req_actuator_1`) — they don't know which GPIO або which
   driver provides the data.

You change bindings without rebuilding firmware — drop а new
`bindings.json` into LittleFS via OTA, or via the WebUI's bindings editor
(planned), and the role mapping updates.

## File location

```
boards/<board_name>/bindings.json
```

Selected together із `board.json` by the Kconfig `CONFIG_MODESP_BOARD=...`.
Both files copy into LittleFS at build time. Runtime path:
`/data/bindings.json`.

## Top-level shape

```json
{
  "manifest_version": 1,
  "bindings": [
    // ... array of binding objects
  ]
}
```

That's it. Single array. Each object у the array is one binding —
а driver instance attached to one hardware pin / channel / address.

## Anatomy of one binding

```json
{
  "hardware": "ow_1",         // board.json id (which pin / bus / expander pin)
  "driver": "ds18b20",        // driver type to instantiate
  "role": "air_temp",         // logical name — appears as equipment.<role> у SharedState
  "module": "equipment",      // module that consumes це binding (almost always "equipment")
  "address": "28:8C:5E:...",  // optional ROM / extra addressing
  "settings": {"offset": 0.5} // optional per-binding driver settings (see below)
}
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `hardware` | string | yes | Must match an `id` declared у `board.json` (any section: `gpio_outputs`, `onewire_buses`, `expander_outputs`, etc.). |
| `driver` | string | yes | Must match а driver name declared у `drivers/<name>/manifest.json`. |
| `role` | string | yes | Semantic name. A role accepts this hardware by **capability**, not by driver (R0.1/R3.1). Becomes `equipment.<role>` (sensors) or `equipment.req_<role>` (actuators) у SharedState. |
| `module` | string | yes | Module що owns the binding and declares the role (today: almost always `"equipment"`; connect-style drivers like `display`/`player` declare the role in their own module). |
| `address` | string | sometimes | Selects a **channel** within the hardware (1-Wire ROM, I²C extra addressing) — not a device identity. Required if driver manifest declares `"requires_address": true`. |
| `settings` | object | optional | Per-binding driver settings — see below. |

### Per-binding settings

A driver manifest may declare a `settings` array (each entry: `key`, `type`,
`default`, `min`/`max`). A binding can override any of them with a `settings`
object; **keys you omit fall back to the driver default**, so `settings` is
always optional. Values are numeric (ints or floats).

```json
{"hardware": "adc_1", "driver": "ntc", "role": "air_temp", "module": "equipment",
 "settings": {"beta": 3435, "r_series": 10000, "r_nominal": 10000, "offset": -0.3}}
```

The driver's factory reads them via `binding.setting_or("key", default)` and
applies them in `apply_settings()`. Today: `ntc` honours
`beta` / `r_series` / `r_nominal` / `offset` / `read_interval_ms`; `ds18b20`
honours `offset` / `read_interval_ms`. These are **per-binding** — two NTC
probes on the same board can carry different calibration. (This replaces the
old global `equipment.ntc_*` / `equipment.ds18b20_offset` state keys, which
applied one calibration to every sensor and were never actually wired.)

## Concrete examples

### Dev board — minimal sensor + relay

`boards/dev/bindings.json`:

```json
{
  "manifest_version": 1,
  "bindings": [
    {"hardware": "ow_1",    "driver": "ds18b20", "role": "air_temp",   "module": "equipment"},
    {"hardware": "relay_1", "driver": "relay",   "role": "actuator_1", "module": "equipment"}
  ]
}
```

This board has one OneWire bus і four GPIO relays declared у `board.json`.
We bind:
- The first OneWire bus to а `ds18b20` driver, role `air_temp` — temperature
  appears у `equipment.air_temp`.
- The first relay to а generic `relay` driver, role `actuator_1` — controllable
  through `equipment.req_actuator_1` (writes by business modules) and
  reflected у `equipment.actuator_1` (current state).

Note **no `address`** on the OneWire binding: the driver auto-discovers the
single sensor on the bus. Якщо there were multiple sensors, each would need
а separate binding із its ROM address.

### KC868-A6 — full relay and input set

`boards/kc868a6/bindings.json`:

```json
{
  "manifest_version": 1,
  "bindings": [
    {"hardware": "relay_1", "driver": "pcf8574_relay", "role": "actuator_1", "module": "equipment"},
    {"hardware": "relay_2", "driver": "pcf8574_relay", "role": "actuator_2", "module": "equipment"},
    {"hardware": "relay_3", "driver": "pcf8574_relay", "role": "actuator_3", "module": "equipment"},
    {"hardware": "relay_4", "driver": "pcf8574_relay", "role": "actuator_4", "module": "equipment"},
    {"hardware": "relay_5", "driver": "pcf8574_relay", "role": "actuator_5", "module": "equipment"},
    {"hardware": "relay_6", "driver": "pcf8574_relay", "role": "actuator_6", "module": "equipment"},
    {"hardware": "din_1",   "driver": "pcf8574_input", "role": "input_1",    "module": "equipment"},
    {"hardware": "din_2",   "driver": "pcf8574_input", "role": "input_2",    "module": "equipment"},
    {"hardware": "din_3",   "driver": "pcf8574_input", "role": "input_3",    "module": "equipment"},
    {"hardware": "din_4",   "driver": "pcf8574_input", "role": "input_4",    "module": "equipment"},
    {"hardware": "din_5",   "driver": "pcf8574_input", "role": "input_5",    "module": "equipment"},
    {"hardware": "din_6",   "driver": "pcf8574_input", "role": "input_6",    "module": "equipment"},
    {"hardware": "ow_1",    "driver": "ds18b20",       "role": "air_temp",   "module": "equipment"},
    {"hardware": "ow_2",    "driver": "ds18b20",       "role": "temp_2",     "module": "equipment"}
  ]
}
```

The reference board wires every KC868-A6 peripheral under **generic** roles:
the six PCF8574 expander outputs `relay_1`..`relay_6` become
`actuator_1`..`actuator_6`; the six opto-isolated inputs `din_1`..`din_6`
become `input_1`..`input_6`; two separate OneWire buses (`ow_1`, `ow_2`)
provide `air_temp` and `temp_2`.

Notice there is **no `address`** on the OneWire bindings — each sensor sits
alone on its own bus (`ow_1`, `ow_2`) and is auto-discovered. Multiple
sensors on a *single* bus would need explicit ROM addresses — see the
section below.

> **Generic vs. semantic roles.** Generic names (`actuator_1`, `input_3`)
> describe the *hardware*. When you build a specific product you give those
> same pins semantic roles instead — a cold room might map them to
> `compressor`, `evap_fan`, `cond_fan`, `defrost_relay` on the relays and
> `door_contact` on an input, plus `evap_temp` on the second sensor. Only
> this bindings file changes; the board and the modules stay the same.

## Multiple sensors on one bus

OneWire is the most common multi-device case. Each sensor has а 64-bit
ROM address (printed on the sensor body для some manufacturers, but
usually discovered у software). Bindings reference the bus's `id` (z
`board.json::onewire_buses`) і supply the `address` для each sensor:

```json
{"hardware": "ow_1", "driver": "ds18b20", "role": "air_temp", "module": "equipment", "address": "28:8C:5E:45:D4:08:44:09"},
{"hardware": "ow_1", "driver": "ds18b20", "role": "temp_2",   "module": "equipment", "address": "28:40:0A:45:D4:72:7E:F0"},
{"hardware": "ow_1", "driver": "ds18b20", "role": "temp_3",   "module": "equipment", "address": "28:55:1B:35:E1:90:6A:24"}
```

The `multiple_per_bus: true` flag у the driver manifest tells the framework
that multiple bindings can target the same `hardware` ID.

### Channels and the WebUI channel picker

`address` selects a **channel** of the capability. The generator emits
`role.channels_by_driver` for each role — the channel list grouped by the
**bound** driver, not by the role-aggregate (R3.5). The WebUI shows a
channel picker **only** when the bound driver exposes 2+ channels of the
same capability; a single channel auto-binds with no extra step. This is
why per-driver attributes (`requires_address` / `channels` / `scan`) are
keyed on the **bound** driver: a wired binding never demands, say, a BLE
address just because some *other* driver of the same capability needs one.

## Sensors vs. actuators у SharedState

After bindings load, Equipment Manager spawns driver instances і exposes
state keys:

| Binding pattern | SharedState keys generated |
|---|---|
| Sensor (`category: "sensor"`) | `equipment.<role>` (current value, read-only) <br> `equipment.<role>_ok` (health, bool) |
| Actuator (`category: "actuator"`) | `equipment.<role>` (current actual state, read-only) <br> `equipment.req_<role>` (requested state, writable) |

So writing `equipment.req_actuator_1 = true` is how a business module
turns an actuator ON. Equipment Manager reads the request key, forwards
to the bound actuator driver, and reflects the actual outcome back to
`equipment.actuator_1`.

## Address discovery

For drivers із `discovery.supported: true` у their manifest, the framework
exposes а scanner endpoint:

```bash
# DS18B20 example
curl -u admin:modesp http://192.168.1.85/api/drivers/ds18b20/scan
```

Returns an array of discovered devices із their addresses і а
current reading (useful for identifying physically which sensor goes where:
warm up the one you're trying to identify із your hand і look для the
rising reading).

```json
[
  {"address": "28:8C:5E:45:D4:08:44:09", "temperature": 22.5, "parasitic": false},
  {"address": "28:40:0A:45:D4:72:7E:F0", "temperature": 22.6, "parasitic": false}
]
```

Copy the addresses into your `bindings.json`, rebuild, flash.

The WebUI's discovery panel (planned, when bindings editor lands) does this
automatically — scan, identify, drag-drop into bindings, save.

## Optional / fallback bindings

Bindings can declare themselves as optional via а driver manifest's
`optional` field у `requires`. Equipment Manager skips missing optional
bindings silently. Required bindings що can't be resolved abort startup
із а log message — so production deployments don't run із silently-broken
hardware.

## Identity lives on the device, never on the binding

`bindings.json` describes **wired** hardware only. The identity of remote
devices (BLE MAC, future LoRa devaddr / MQTT topic) **never** lives on a
role binding (R0.3) — otherwise the role would be pinned to a transport.
Such devices are added at **runtime** (scan → subscribe on the "Devices"
page), written to `/data/devices.json`, and a role resolves to them by
capability exactly as it would to a wired channel. A `Binding` holds only
the device `id`; `find_remote_device(id)` resolves `id → identity/name`.

That is why the reference `boards/stand_s3/bindings.json` contains **only**
wired bindings (display `disp_0`, audio `i2s_0`), while every BLE device
(Xiaomi/nRF observers, iPixel panel) is added at runtime and binds the
`room_temp` / `orientation` / `panel` roles there.

## Validation

`generate_ui.py` cross-validates bindings against `board.json` + driver manifests
at build time — a mis-wired binding fails the build, it never reaches firmware as
a silent runtime skip. Every check is an ERROR:

1. Every `hardware` resolves to an `id` in board.json (and the board has no
   duplicate ids).
2. Every `driver` resolves to a `drivers/<name>/manifest.json`.
3. **The driver's `hardware_type` matches the board section of its `hardware`** —
   e.g. `ds18b20` (onewire_bus) bound to an `adc_channel` is rejected.
4. A driver with `requires_address: true` has a non-empty `address`. (`ds18b20`
   is `requires_address: false` — single-sensor SKIP_ROM with no address is fine.)
5. A `hardware` id is reused only if the driver is `multiple_per_bus: true`, and
   then each binding carries a distinct non-empty `address`.
6. `role` is unique within a module.

Separately, the firmware build **fails** if the active board binds a driver that
is **disabled in menuconfig** (`CONFIG_MODESP_DRIVER_<NAME>=n`) — checked in
`components/modesp_hal/CMakeLists.txt`. Reconcile with:

```bash
python tools/drivers_sync.py --fix          # enable bound-but-disabled drivers
python tools/drivers_sync.py --prune        # also disable unused drivers (shrinks the binary)
python tools/drivers_sync.py --dry-run      # show the diff, change nothing
```

The build also prints an advisory `INFO:` listing drivers enabled but unused by
the active board (excluding discovery-capable drivers like `ds18b20`).

## Common mistakes

**Wrong hardware ID:** typo у `hardware` field — board.json has `relay_1`
but bindings says `Relay_1`. Build fails із "hardware ID 'Relay_1' not
declared у board.json". Fix: case-sensitive copy.

**Mismatched driver і hardware category:** binding а GPIO output to а
sensor driver (`ds18b20` expects OneWire bus). Either correct the driver
or fix the hardware ID.

**Missing address for multi-device bus:** OneWire із 2+ sensors but bindings
missing `address` field — driver reads pick whichever responds first
(usually the lowest-ROM device). Bug nightmare — readings cross between
sensors. Always supply ROM addresses for multi-device buses.

**Duplicate role:** two bindings із the same `role` — Equipment Manager
crashes on init. Each role must be unique within а module.

**Editing bindings без OTA / rebuild:** the file lives у LittleFS. Edits
on host that don't propagate to the device's flash won't take effect.
Re-flash, або use OTA file replacement, або edit through the WebUI bindings
editor (planned).

## Workflow для а new deployment

1. **Decide your roles** based on the recipe / use case. The reference board
   ships with generic roles (`actuator_1`, `input_1`, `air_temp`); for a
   specific product you pick semantic names instead — e.g. for a cold room:
   `air_temp`, `evap_temp`, `compressor`, `evap_fan`, `defrost_relay`,
   `door_contact`.
2. **Match to board hardware.** Look at `board.json` to see what's
   available — relays, OneWire buses, GPIO inputs.
3. **Choose drivers** per hardware: GPIO relay → `relay`; PCF8574 relay →
   `pcf8574_relay`; DS18B20 sensor → `ds18b20`; NTC sensor → `ntc`.
4. **Write `bindings.json`** із one binding per role.
5. **Run address discovery** для OneWire / multi-device buses; copy
   addresses into bindings.
6. **Build, flash, monitor.** Verify each role's `equipment.<role>` key
   appears у `/api/state` із sane values.
7. **Iterate** if anything's off — wrong sensor identified, relay polarity
   inverted, etc.

## Next steps

- **[board-config.md](board-config.md)** — what board.json declares
  (prerequisite to writing bindings).
- **[modules/equipment.md](../03-framework-reference/modules/equipment.md)**
  *(planned)* — Equipment Manager — the consumer of bindings.
- **[writing-a-driver.md](../02-module-author-guide/writing-a-driver.md)**
  — implement а driver for new hardware.
- **[deployment.md](deployment.md)** *(planned)* — full deployment workflow.
