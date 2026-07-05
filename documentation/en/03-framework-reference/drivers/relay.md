# `relay` — GPIO relay actuator

> 📖 **Українською:** [documentation/uk/03-framework-reference/drivers/relay.md](../../../uk/03-framework-reference/drivers/relay.md)

GPIO relay actuator — one binary output per binding. Drives а
mechanical relay, SSR, MOSFET, або anything else triggered by а GPIO
high/low level. Зadдrives a typical compressor, fan, heater, valve
у refrigeration.

The driver registers as an `actuator` and provides the `relay_out`
capability (`hardware_type: gpio_output`, `requires_address: false`).
A role declares the `relay_out` capability, not this driver
(R0.1/R3.1) — the signal source stays swappable. One actuator per
bound GPIO.

REQUIRES: `modesp_hal`. No external dependencies.

## Bindings

```json
{
  "id": "compressor",
  "driver": "relay",
  "hardware_id": "compressor_relay",
  "role": "compressor",
  "pin": "gpio_relay_1"
}
```

`pin` references а board-defined GPIO output. Equipment Manager
publishes `equipment.req_compressor` (the request), і this driver
writes the level to the bound GPIO.

## Settings

None у the shipped manifest. The driver is intentionally minimal — any
debouncing, min-on/min-off times, або lockout logic belongs у the
upstream business module, not у the actuator driver.

If you want polarity inversion, do it via the board.json `active_low`
flag on the GPIO definition, not у the driver.

## Provides

`{"capability": "relay_out", "type": "bool"}` — current commanded
state, mirrored to `equipment.<role>`. A role with the `relay_out`
capability (out direction) matches this driver (R3.1).

## Pattern: how а business module drives це

```cpp
// у your BaseModule::on_update:
if (heating_needed) {
    state.set("equipment.req_compressor", true);
} else {
    state.set("equipment.req_compressor", false);
}
```

Equipment Manager arbitrates the `req_<role>` key і forwards the
final command to this driver, що writes GPIO.

## Common pitfalls

**Compressor short-cycling:** relay reacts у <1 ms. Compressors hate
short cycles. Add а min-off time у your business module — don't try to
push це into the driver.

**Inductive kick:** mechanical relays driving inductive loads (motors,
solenoids) need flyback diodes / snubbers on the load side. Driver
won't save you if you skip це at PCB level.

**Active-low confusion:** opto-isolated relay boards typically need
GPIO LOW to energize. Set `active_low: true` у board.json for that GPIO.

**Cold-start glitch:** ESP32 GPIO defaults to input during boot,
що can read as random level externally. Add а pull-down resistor on
your relay control line to guarantee OFF on boot.

## UI surface

No driver-specific UI card. Operators see the bound `equipment.<role>`
state у Equipment Manager's auto-generated cards.

## Why це а good driver to read

- Simplest possible IDriver implementation (~80 LOC).
- Zero settings — demonstrates the minimal manifest case.
- Clear actuator pattern для consumption by Equipment Manager.

## Next steps

- **[drivers/pcf8574_relay.md](pcf8574_relay.md)** — I2C-expanded
  variant (8 relays per chip).
- **[modules/equipment.md](../modules/equipment.md)** — how `req_*` keys
  flow to це driver.
- **[04-hardware/bindings.md](../../04-hardware/bindings.md)** —
  binding schema reference.

## Source

- [`drivers/relay/manifest.json`](../../../../drivers/relay/manifest.json)
- [`drivers/relay/include/relay_driver.h`](../../../../drivers/relay/include/relay_driver.h)
- [`drivers/relay/src/relay_driver.cpp`](../../../../drivers/relay/src/relay_driver.cpp)
