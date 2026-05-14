# `digital_input` — GPIO contact input

> 📖 **Українською:** [documentation/uk/03-framework-reference/drivers/digital_input.md](../../../uk/03-framework-reference/drivers/digital_input.md)

GPIO digital input — а binary sensor reading а switch, door contact,
limit switch, або any dry-contact device. The simplest possible sensor
driver — reads а GPIO level on each tick і publishes а bool.

The driver registers as а `sensor` із `hardware_type: gpio_input` і
`requires_address: false`. One sensor per bound GPIO.

REQUIRES: `modesp_hal`. No external dependencies.

## Bindings

```json
{
  "id": "door",
  "driver": "digital_input",
  "hardware_id": "door_contact",
  "role": "door_contact",
  "pin": "gpio_input_2"
}
```

`pin` references а board-defined GPIO input (із pull-up/pull-down
configuration). The driver doesn't configure the pin itself —
configuration belongs у board.json.

## Settings

| Key | Type | Default | Notes |
|---|---|---|---|
| `invert` | bool | false | Invert logic (для NC vs NO contacts). Persisted. |

That's it. Debouncing belongs у the consumer (e.g. door alarm module),
not у the driver — different domains need different debounce timings.

## Provides

`{"type": "bool"}` — current pin level (із optional invert), published
to `equipment.<role>`.

## Pattern: consumed by business module

```cpp
// у your BaseModule::on_update:
bool door_open;
if (state.get("equipment.door_contact", door_open) && door_open) {
    // door is open
}
```

Debounce, edge detect, timeouts — all live у the consumer. Driver
publishes the **raw level**.

## Hardware notes

- ESP32 GPIO inputs are 3.3 V tolerant. For 5 V or 24 V contacts use
  optocoupler / level shifter.
- Without а pull-up/pull-down, floating inputs read random. Configure
  `pull_up: true` (active-low contact) у board.json.
- Long wire runs are antennas — RFI on the contact may cause spurious
  edges. Add а capacitor (~10 nF) close to the GPIO, або filter у the
  consumer.

## Discovery

None. Bindings declared manually.

## Common pitfalls

**Random states at boot:** if pin doesn't have а pull-up configured у
board, you'll see noise. Always set pull-up або pull-down у board.json.

**Inverted contact:** NC contact reads HIGH when closed, LOW when open
— opposite of what а naive reader expects. Set `invert: true` to make
"contact closed" → true.

**Edge skipping:** driver polls at the tick rate (~100 Hz). Pulses
shorter than ~10 ms can be missed. For interrupt-grade input, write а
dedicated driver із GPIO ISR.

## UI surface

None у the shipped manifest. Add а cards block у your fork if your
operators need to flip `invert` у the field; standard pattern is
toggle widget binding to `<binding_id>.invert`.

## Why це а good driver to read

- Simplest sensor driver — counterpart to `relay` on the actuator side.
- Single bool setting demonstrates persisted-setting pattern.
- Clean publisher → equipment.<role> contract.

## Next steps

- **[drivers/pcf8574_input.md](pcf8574_input.md)** — I2C-expanded variant.
- **[drivers/relay.md](relay.md)** — counterpart actuator driver.
- **[modules/equipment.md](../modules/equipment.md)** — how sensors
  become `equipment.<role>` keys.

## Source

- [`drivers/digital_input/manifest.json`](../../../../drivers/digital_input/manifest.json)
- [`drivers/digital_input/include/digital_input_driver.h`](../../../../drivers/digital_input/include/digital_input_driver.h)
- [`drivers/digital_input/src/digital_input_driver.cpp`](../../../../drivers/digital_input/src/digital_input_driver.cpp)
