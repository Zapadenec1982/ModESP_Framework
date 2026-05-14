# `relay` — GPIO реле actuator

> 📖 **In English:** [documentation/en/03-framework-reference/drivers/relay.md](../../../en/03-framework-reference/drivers/relay.md)

GPIO реле actuator — один binary output per binding. Drives
mechanical relay, SSR, MOSFET, або anything else triggered by GPIO
high/low level. Drives typical compressor, fan, heater, valve
у refrigeration.

Driver registers як `actuator` з `hardware_type: gpio_output`
і `requires_address: false`. One actuator per bound GPIO.

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

`pin` references board-defined GPIO output. Equipment Manager
publishes `equipment.req_compressor` (request), і це driver
writes level до bound GPIO.

## Settings

None у shipped manifest. Driver intentionally minimal — будь-яке
debouncing, min-on/min-off times, або lockout logic належить
upstream business module, не actuator driver.

Якщо хочете polarity inversion — do it через board.json `active_low`
flag на GPIO definition, не у driver.

## Provides

`{"type": "bool"}` — current commanded state, mirrored до
`equipment.<role>`.

## Pattern: як business module drives це

```cpp
// у вашому BaseModule::on_update:
if (heating_needed) {
    state.set("equipment.req_compressor", true);
} else {
    state.set("equipment.req_compressor", false);
}
```

Equipment Manager arbitrates `req_<role>` key і forwards final
command до цього driver, що writes GPIO.

## Common pitfalls

**Compressor short-cycling:** relay reacts за <1 ms. Compressors hate
short cycles. Add min-off time у вашому business module — не try push
це у driver.

**Inductive kick:** mechanical relays driving inductive loads (motors,
solenoids) need flyback diodes / snubbers на load side. Driver
не save you якщо skip це at PCB level.

**Active-low confusion:** opto-isolated relay boards typically need
GPIO LOW щоб energize. Set `active_low: true` у board.json для того GPIO.

**Cold-start glitch:** ESP32 GPIO defaults до input під час boot,
що can read як random level externally. Add pull-down resistor на
ваш relay control line щоб guarantee OFF on boot.

## UI surface

No driver-specific UI card. Operators see bound `equipment.<role>`
state у Equipment Manager's auto-generated cards.

## Чому це good driver to read

- Simplest possible IDriver implementation (~80 LOC).
- Zero settings — demonstrates minimal manifest case.
- Clear actuator pattern для consumption by Equipment Manager.

## Що далі

- **[drivers/pcf8574_relay.md](pcf8574_relay.md)** — I2C-expanded
  variant (8 relays per chip).
- **[modules/equipment.md](../modules/equipment.md)** — як `req_*` keys
  flow до цього driver.
- **[04-hardware/bindings.md](../../04-hardware/bindings.md)** —
  binding schema reference.

## Source

- [`drivers/relay/manifest.json`](../../../../drivers/relay/manifest.json)
- [`drivers/relay/include/relay_driver.h`](../../../../drivers/relay/include/relay_driver.h)
- [`drivers/relay/src/relay_driver.cpp`](../../../../drivers/relay/src/relay_driver.cpp)
