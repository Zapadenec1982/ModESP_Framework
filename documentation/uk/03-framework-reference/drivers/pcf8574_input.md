# `pcf8574_input` — I2C-expanded дискретний вхід

> 📖 **In English:** [documentation/en/03-framework-reference/drivers/pcf8574_input.md](../../../en/03-framework-reference/drivers/pcf8574_input.md)

PCF8574 used як 8-bit input expander. Той самий chip як `pcf8574_relay`
але bound тут як **sensor** — кожен pin reads як dry contact через
chip's quasi-pull-up. Up to 8 chips per bus = 64 contact inputs
sharing one I2C bus.

Driver registers як `sensor` з `hardware_type: i2c_expander_input`
і `multiple_per_bus: true`. One binding per input bit.

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

- `bus`, `chip_address`, `pin` — same semantics як `pcf8574_relay`.
- Driver aggregates всі bindings sharing one `(bus, chip_address)` І
  issues **single I2C read per chip per tick** — distributes
  byte до всіх 8 bindings без individual transactions.

## Settings

| Key | Type | Default | Notes |
|---|---|---|---|
| `invert` | bool | false | Інверсія логіки per binding. |

Set per key, persisted через PersistService.

## Provides

`{"type": "bool"}` — current contact state (з optional invert),
mirrored до `equipment.<role>`.

## Hardware notes

- PCF8574 inputs use **quasi-bidirectional** port — weak internal
  pull-up sources ~100 µА. Це enough для dry contacts (door switches),
  не enough для high-impedance optocoupler outputs at long wire runs.
- Для long-run contacts, add external pull-up (4.7 kΩ до 5 V)
  before chip input.
- Read latency: I2C transaction ≈ 100-200 µs at 100 kHz. Polling at
  100 Hz costs <2% bus bandwidth per chip.

## Discovery

None. Chips і pins declared manually.

## Common pitfalls

**Shared chip із output driver:** як і `pcf8574_relay`, ніколи не bind
той самий chip як both input AND output. Use separate chips для input vs
output banks.

**Floating pins:** PCF8574 has weak pull-up only. Якщо pin bound але
no contact wired, ви отримаєте noisy `false`/`true` flapping. Either bind
кожен pin (навіть unused), або ground unused pins у hardware.

**Address conflicts з output expanders:** PCF8574 input І output
variants share SAME I2C address range (0x20-0x27) — physically це same
chip. Plan address allocation так, щоб each chip used для одної purpose only.

**Edge skipping:** як і `digital_input`, driver polls at tick rate. Для
short pulses потрібен interrupt routing — PCF8574 має INT pin що
asserts на будь-яку change; route його до GPIO і wire ISR-aware logic у
вашому business module (не у driver).

## UI surface

None у shipped manifest.

## Чому це good driver to read

- Variant `pcf8574_relay` — same transport, different direction.
- Demonstrates per-chip read aggregation pattern.
- Per-binding `invert` з persistence.

## Що далі

- **[drivers/digital_input.md](digital_input.md)** — GPIO counterpart.
- **[drivers/pcf8574_relay.md](pcf8574_relay.md)** — той самий chip як actuator.
- **[modules/equipment.md](../modules/equipment.md)**

## Source

- [`drivers/pcf8574_input/manifest.json`](../../../../drivers/pcf8574_input/manifest.json)
- [`drivers/pcf8574_input/include/pcf8574_input_driver.h`](../../../../drivers/pcf8574_input/include/pcf8574_input_driver.h)
- [`drivers/pcf8574_input/src/pcf8574_input_driver.cpp`](../../../../drivers/pcf8574_input/src/pcf8574_input_driver.cpp)
