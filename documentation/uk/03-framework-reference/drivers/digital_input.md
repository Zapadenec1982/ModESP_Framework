# `digital_input` — GPIO дискретний вхід

> 📖 **In English:** [documentation/en/03-framework-reference/drivers/digital_input.md](../../../en/03-framework-reference/drivers/digital_input.md)

GPIO digital input — binary sensor що reads switch, door contact,
limit switch, або any dry-contact device. Найпростіший possible sensor
driver — reads GPIO level on each tick і publishes bool.

Driver registers як `sensor` з `hardware_type: gpio_input` і
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

`pin` references board-defined GPIO input (з pull-up/pull-down
configuration). Driver не configures pin сам —
configuration належить board.json.

## Settings

| Key | Type | Default | Notes |
|---|---|---|---|
| `invert` | bool | false | Інверсія логіки (NC vs NO contacts). Persisted. |

Це все. Debouncing належить consumer (наприклад door alarm module),
не driver — different domains need different debounce timings.

## Provides

`{"type": "bool"}` — current pin level (із optional invert), publishes
до `equipment.<role>`.

## Pattern: consumed by business module

```cpp
// у вашому BaseModule::on_update:
bool door_open;
if (state.get("equipment.door_contact", door_open) && door_open) {
    // door is open
}
```

Debounce, edge detect, timeouts — все живе у consumer. Driver
publishes **raw level**.

## Hardware notes

- ESP32 GPIO inputs 3.3 V tolerant. Для 5 V або 24 V contacts use
  optocoupler / level shifter.
- Without pull-up/pull-down, floating inputs read random. Configure
  `pull_up: true` (active-low contact) у board.json.
- Long wire runs — antennas — RFI на contact може cause spurious
  edges. Add capacitor (~10 nF) close до GPIO, або filter у
  consumer.

## Discovery

None. Bindings declared manually.

## Common pitfalls

**Random states at boot:** якщо pin не має pull-up configured у
board, ви бачите noise. Завжди set pull-up або pull-down у board.json.

**Inverted contact:** NC contact reads HIGH коли closed, LOW коли open
— opposite того що naive reader expects. Set `invert: true` щоб make
"contact closed" → true.

**Edge skipping:** driver polls at tick rate (~100 Hz). Pulses
shorter than ~10 ms можуть бути missed. Для interrupt-grade input, write
dedicated driver з GPIO ISR.

## UI surface

None у shipped manifest. Add cards block у вашому fork якщо
operators need flip `invert` у field; standard pattern —
toggle widget binding до `<binding_id>.invert`.

## Чому це good driver to read

- Найпростіший sensor driver — counterpart до `relay` на actuator side.
- Single bool setting demonstrates persisted-setting pattern.
- Clean publisher → equipment.<role> contract.

## Що далі

- **[drivers/pcf8574_input.md](pcf8574_input.md)** — I2C-expanded variant.
- **[drivers/relay.md](relay.md)** — counterpart actuator driver.
- **[modules/equipment.md](../modules/equipment.md)** — як sensors
  become `equipment.<role>` keys.

## Source

- [`drivers/digital_input/manifest.json`](../../../../drivers/digital_input/manifest.json)
- [`drivers/digital_input/include/digital_input_driver.h`](../../../../drivers/digital_input/include/digital_input_driver.h)
- [`drivers/digital_input/src/digital_input_driver.cpp`](../../../../drivers/digital_input/src/digital_input_driver.cpp)
