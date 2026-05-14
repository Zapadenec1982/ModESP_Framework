# `pcf8574_relay` — I2C-expanded реле actuator

> 📖 **In English:** [documentation/en/03-framework-reference/drivers/pcf8574_relay.md](../../../en/03-framework-reference/drivers/pcf8574_relay.md)

PCF8574 — 8-bit I/O expander на I2C. Кожен chip exposes 8 quasi-bidirectional
pins; цей driver використовує їх як outputs для relay boards. Up to 8 chips
per bus (addresses 0x20-0x27) = 64 relays sharing 2 GPIOs. Standard choice
коли board pin count exhausted.

Driver registers як `actuator` з `hardware_type: i2c_expander_output`
і `multiple_per_bus: true`. Один binding per relay (per chip pin).

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
- `pin` — output bit 0-7 на chip.

Equipment Manager publishes `equipment.req_<role>`; driver
aggregates всі bindings sharing same `(bus, chip_address)` І issues
single I2C write per chip per change, мінімізуючи bus traffic.

## Settings

None у shipped manifest. Та сама philosophy як `relay`: actuator drivers
stay dumb. Inversion через board, timing через business module.

## Provides

`{"type": "bool"}` — commanded level на bound expander pin, mirrored
до `equipment.<role>`.

## Hardware notes

- PCF8574 outputs **quasi-bidirectional** — LOW strong drive, HIGH —
  weak pull-up. Most relay modules drive ACTIVE-LOW (LED
  forward через opto-isolator на relay board). Match accordingly
  через `active_low: true` у board.json або conventional relay-board
  wiring.
- Driver caches per-chip output state і issues one I2C write per change.
  Multiple simultaneous changes на тому ж chip coalesced у one
  transaction.
- І²C transaction cost ≈ 100-200 µs at 100 kHz; nearly free.
- Driver shares bus з input expander (`pcf8574_input`) і other I2C
  devices — `modesp_hal` serialises access через I2C mutex.

## Discovery

No driver-specific scan endpoint; use generic
`POST /api/onewire/scan` analog для I2C з HAL utility (planned).
Поки що declare chip addresses manually based on jumper settings.

## Common pitfalls

**Wrong address у bindings:** якщо `chip_address` incorrect, writes
silently NACK і no relay fires. Verify через `i2cdetect`-equivalent у
board's diagnostic tools.

**Shared chip across multiple drivers:** ніколи не bind PCF8574 до both
`pcf8574_relay` AND `pcf8574_input` — driver behaviour undefined
because outputs і inputs share same port latch. Use separate chips.

**Power-on glitch:** chip resets із all pins HIGH (quasi-pull-up).
На opto-coupled active-low boards, це momentarily energises all relays
під час boot — за кілька ms. Mitigate з pull-up на relay coil або
slow-start logic у board.json.

**Bus voltage:** PCF8574 wants 5 V для best output drive І І²C
swing. Use level shifter на 3.3 V ESP32 board якщо reliability matters.

## UI surface

None per-driver. Operators see bound `equipment.<role>` state.

## Чому це good driver to read after `relay`

- Same actuator contract, different transport.
- Demonstrates `multiple_per_bus: true` з chip-level aggregation.
- Demonstrates shared-resource caching pattern (per-chip latch).
- ~150 LOC implementation.

## Що далі

- **[drivers/relay.md](relay.md)** — direct-GPIO variant.
- **[drivers/pcf8574_input.md](pcf8574_input.md)** — same chip used
  як input expander.
- **[modules/equipment.md](../modules/equipment.md)**

## Source

- [`drivers/pcf8574_relay/manifest.json`](../../../../drivers/pcf8574_relay/manifest.json)
- [`drivers/pcf8574_relay/include/pcf8574_relay_driver.h`](../../../../drivers/pcf8574_relay/include/pcf8574_relay_driver.h)
- [`drivers/pcf8574_relay/src/pcf8574_relay_driver.cpp`](../../../../drivers/pcf8574_relay/src/pcf8574_relay_driver.cpp)
