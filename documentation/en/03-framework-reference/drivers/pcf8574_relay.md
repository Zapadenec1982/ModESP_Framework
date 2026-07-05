# `pcf8574_relay` — I2C-expanded relay actuator

> 📖 **Українською:** [documentation/uk/03-framework-reference/drivers/pcf8574_relay.md](../../../uk/03-framework-reference/drivers/pcf8574_relay.md)

PCF8574 is an 8-bit I/O expander on I2C. Each chip exposes 8 quasi-bidirectional
pins; this driver uses them as outputs for relay boards. Up to 8 chips
per bus (addresses 0x20-0x27) = 64 relays sharing 2 GPIOs. Standard choice
when board pin count is exhausted.

The driver registers as an `actuator` із `hardware_type: i2c_expander_output`
і `multiple_per_bus: true`. One binding per relay (per chip pin).

REQUIRES: ESP-IDF I2C driver, `modesp_hal`.

## Bindings

```json
{
  "id": "valve_a",
  "driver": "pcf8574_relay",
  "hardware_id": "expander_valve_a",
  "role": "valve_a",
  "bus": "i2c_bus_0",
  "chip_address": "0x20",
  "pin": 3
}
```

- `bus` — board-defined I2C master.
- `chip_address` — PCF8574 I2C address (0x20-0x27, set by А0-А2 pins).
- `pin` — output bit 0-7 on the chip.

Equipment Manager publishes `equipment.req_<role>`; the driver
aggregates all bindings sharing the same `(bus, chip_address)` AND issues
а single I2C write per chip per change, минімізуючи bus traffic.

## Settings

None у the shipped manifest. Same philosophy as `relay`: actuator drivers
stay dumb. Inversion via board, timing via business module.

## Provides

`{"capability": "relay_out", "type": "bool"}` — commanded level on the
bound expander pin, mirrored to `equipment.<role>`. The consuming role
declares the `relay_out` capability, not this driver; any actuator with
the same capability is substitutable (R0.1, R3.1).

## Hardware notes

- PCF8574 outputs are **quasi-bidirectional** — LOW is а strong drive,
  HIGH is а weak pull-up. Most relay modules drive ACTIVE-LOW (LED
  forward through opto-isolator on the relay board). Match accordingly
  via `active_low: true` у board.json або conventional relay-board
  wiring.
- Driver caches per-chip output state і issues one I2C write per change.
  Multiple simultaneous changes на the same chip are coalesced у one
  transaction.
- І²C transaction cost ≈ 100-200 µs at 100 kHz; nearly free.
- Driver shares bus із input expander (`pcf8574_input`) і other I2C
  devices — `modesp_hal` serialises access through the I2C mutex.

## Discovery

No driver-specific scan endpoint; use the generic
`POST /api/onewire/scan` analog for I2C from а HAL utility (planned).
For now, declare chip addresses manually based on jumper settings.

## Common pitfalls

**Wrong address у bindings:** if `chip_address` is incorrect, writes
silently NACK і no relay fires. Verify із `i2cdetect`-equivalent у
board's diagnostic tools.

**Shared chip across multiple drivers:** never bind а PCF8574 to both
`pcf8574_relay` AND `pcf8574_input` — driver behaviour is undefined
because outputs і inputs share the same port latch. Use separate chips.

**Power-on glitch:** chip resets із all pins HIGH (quasi-pull-up).
On opto-coupled active-low boards, це momentarily energises all relays
during boot — for а few ms. Mitigate із а pull-up on relay coil або
slow-start logic у board.json.

**Bus voltage:** PCF8574 wants 5 V для best output drive AND І²C
swing. Use level shifter on a 3.3 V ESP32 board if reliability matters.

## UI surface

None per-driver. Operators see the bound `equipment.<role>` state.

## Why це а good driver to read after `relay`

- Same actuator contract, different transport.
- Demonstrates `multiple_per_bus: true` із chip-level aggregation.
- Demonstrates shared-resource caching pattern (per-chip latch).
- ~150 LOC implementation.

## Next steps

- **[drivers/relay.md](relay.md)** — direct-GPIO variant.
- **[drivers/pcf8574_input.md](pcf8574_input.md)** — same chip used
  as input expander.
- **[modules/equipment.md](../modules/equipment.md)**

## Source

- [`drivers/pcf8574_relay/manifest.json`](../../../../drivers/pcf8574_relay/manifest.json)
- [`drivers/pcf8574_relay/include/pcf8574_relay_driver.h`](../../../../drivers/pcf8574_relay/include/pcf8574_relay_driver.h)
- [`drivers/pcf8574_relay/src/pcf8574_relay_driver.cpp`](../../../../drivers/pcf8574_relay/src/pcf8574_relay_driver.cpp)
