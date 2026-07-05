# `pcf8574_input` — I2C-expanded contact input

> 📖 **Українською:** [documentation/uk/03-framework-reference/drivers/pcf8574_input.md](../../../uk/03-framework-reference/drivers/pcf8574_input.md)

PCF8574 used as 8-bit input expander. Same chip as `pcf8574_relay`
but bound here as а **sensor** — each pin reads as а dry contact via
the chip's quasi-pull-up. Up to 8 chips per bus = 64 contact inputs
sharing one I2C bus.

The driver registers as а `sensor` із `hardware_type: i2c_expander_input`,
provides capability `binary_in` і has `multiple_per_bus: true`. One binding
per input bit. The role binds by capability, not by driver (R0.1 / R3.1) —
the module asks for `binary_in` without knowing who provides it.

REQUIRES: ESP-IDF I2C driver, `modesp_hal`.

## Bindings

```json
{
  "id": "door_freezer",
  "driver": "pcf8574_input",
  "hardware_id": "exp_door_a",
  "role": "door_freezer",
  "bus": "i2c_bus_0",
  "chip_address": "0x21",
  "pin": 2
}
```

- `bus`, `chip_address`, `pin` — same semantics as `pcf8574_relay`.
- Each binding is а separate driver instance: on every tick it issues its
  **own I2C read** of the chip (`read_state`) and extracts its bit. Several
  bindings on one chip = several reads per cycle (~6 reads per 100 ms at
  100 kHz ~ 1.8 ms total — acceptable).

## Settings

| Key | Type | Default | Notes |
|---|---|---|---|
| `invert` | bool | false | Invert logic per binding. |

Set за key, persisted via PersistService.

## Provides

`{"capability": "binary_in", "type": "bool"}` — current contact state
(із optional invert), mirrored to `equipment.<role>`.

## Hardware notes

- PCF8574 inputs use the **quasi-bidirectional** port — а weak internal
  pull-up sources ~100 µА. Це enough для dry contacts (door switches),
  not enough для high-impedance optocoupler outputs at long wire runs.
- For long-run contacts, add an external pull-up (4.7 kΩ to 5 V)
  before the chip input.
- Read latency: I2C transaction ≈ 100-200 µs at 100 kHz. Polling at
  100 Hz costs <2% of bus bandwidth per chip.

## Discovery

None. Chips і pins declared manually.

## Common pitfalls

**Shared chip із output driver:** як і `pcf8574_relay`, never bind the
same chip as both input AND output. Use separate chips for input vs
output banks.

**Floating pins:** PCF8574 has weak pull-up only. If а pin is bound but
no contact wired, you'll get noisy `false`/`true` flapping. Either bind
every pin (даже unused), or ground unused pins у hardware.

**Address conflicts із output expanders:** PCF8574 input AND output
variants share the SAME I2C address range (0x20-0x27) — they're
physically the same chip. Plan your address allocation так, щоб each
chip is used for one purpose only.

**Edge skipping:** як і `digital_input`, driver polls at tick rate. For
short pulses ви потрібний interrupt routing — PCF8574 has an INT pin що
asserts on any change; route it to а GPIO і wire ISR-aware logic у
your business module (not у driver).

## UI surface

None у shipped manifest.

## Why це а good driver to read

- Variant of `pcf8574_relay` — same transport, different direction.
- Demonstrates the `multiple_per_bus` pattern — several driver instances on
  one chip, each with its own read.
- Per-binding `invert` із persistence.

## Next steps

- **[drivers/digital_input.md](digital_input.md)** — GPIO counterpart.
- **[drivers/pcf8574_relay.md](pcf8574_relay.md)** — same chip as actuator.
- **[modules/equipment.md](../modules/equipment.md)**

## Source

- [`drivers/pcf8574_input/manifest.json`](../../../../drivers/pcf8574_input/manifest.json)
- [`drivers/pcf8574_input/include/pcf8574_input_driver.h`](../../../../drivers/pcf8574_input/include/pcf8574_input_driver.h)
- [`drivers/pcf8574_input/src/pcf8574_input_driver.cpp`](../../../../drivers/pcf8574_input/src/pcf8574_input_driver.cpp)
