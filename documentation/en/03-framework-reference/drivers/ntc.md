# `ntc` — NTC thermistor on ADC channel

> 📖 **Українською:** [documentation/uk/03-framework-reference/drivers/ntc.md](../../../uk/03-framework-reference/drivers/ntc.md)

NTC thermistor via ESP32 ADC channel із B-parameter equation. Cheap,
simple, common у refrigeration і HVAC where ±0.5 °C accuracy is enough.
Range typically -40…+125 °C depending on resistor і thermistor choice.

The driver registers as а `sensor` із `hardware_type: adc_channel`,
provides capability `temperature` і has `requires_address: false`.
Single sensor per bound ADC channel. А role binds by capability, not by
driver (R0.1 / R3.1) — the module asks for `temperature` without knowing
who supplies it (ntc / ds18b20 / а BLE channel).

REQUIRES: ESP-IDF ADC driver, `modesp_hal`.

## Bindings

```json
{
  "id": "ambient",
  "driver": "ntc",
  "hardware_id": "adc_1",
  "role": "ambient_temp"
}
```

`role` declares а capability (`capability: temperature`), not а driver —
the Equipment Manager resolves it by role (e.g. `equipment.ambient_temp`),
the source is swappable (R0.1). `hardware_id` references а board-defined
ADC channel (`adc_channels[].id` in board.json, із attenuation і
bit-width) — the driver resolves it via `find_adc_channel`. The driver
doesn't configure the ADC itself — that's the board's responsibility.

## Settings (not persisted by default — adjust manifest)

| Key | Type | Default | Notes |
|---|---|---|---|
| `beta` | int 2000-5000 | 3950 | B-coefficient of the thermistor. |
| `r_series` | int 1k-100kΩ | 10000 | Series resistor (pull-up). |
| `r_nominal` | int 1k-100kΩ | 10000 | NTC resistance at 25 °C. |
| `read_interval_ms` | int 100-60000 | 1000 | Polling interval. |
| `offset` | float -5…+5 °C | 0.0 | Calibration correction. |

Most NTCs ship із datasheet values for B і R25. Common: B = 3950, R25 = 10 kΩ.

## Provides

`{"capability": "temperature", "type": "float", "unit": "°C", "range": [-40, 125]}` — published to
`equipment.<role>`.

If the ADC reading is at rail (raw < 50 or > 4045 → sensor open or
short) or the temperature is out of range, the driver skips the update
and keeps the last valid value; `read()` returns `false` until а valid
reading has been taken. After 5 consecutive errors the sensor is marked
unhealthy (`is_healthy()` = `false`).

## B-parameter equation

Standard Steinhart-simplified form:

```
1/T = 1/T0 + (1/B) * ln(R / R0)
T   = T - 273.15  (Celsius)
```

Where `R` is the measured NTC resistance computed from ADC voltage і
the series resistor у а voltage divider.

The driver applies the formula on every sample. No table lookup,
no interpolation. Compute cost ~5 µs.

## Discovery

NTC has no discovery — there's nothing to scan. Bindings must be
declared manually.

## Common pitfalls

**Wrong divider direction:** typical schematic puts NTC to GND і series
resistor to Vcc. Driver assumes це. If your schematic is inverted, swap
у hardware or override `beta` sign (advanced).

**ADC nonlinearity:** ESP32 ADC has poor linearity near rails. Choose
`r_series` so that NTC voltage falls у the middle 60% of ADC range at
your typical operating temperature.

**Self-heating:** at <1 kΩ NTC resistance + 3.3 V supply, current
exceeds 3 mA — self-heating becomes significant. Use 10 kΩ NTC or
higher series resistor.

**B-coefficient drift:** B is а log-fit і loses accuracy >40 °C from
25 °C. For wide-range applications, consider DS18B20 instead.

## UI surface

The shipped manifest doesn't declare ui cards (terse manifest). Add
а settings card у your fork if your operators need to tune B
у the field.

## Why це а good driver to read after `ds18b20`

- Contrast із digital sensor: NTC needs math, ADC config awareness.
- Demonstrates the no-address case (`requires_address: false`).
- Demonstrates no-discovery case.
- Demonstrates non-persisted settings (defaults у manifest, not NVS).

## Next steps

- **[drivers/ds18b20.md](ds18b20.md)** — digital alternative.
- **[rules.md](../rules.md)** — R0.1 (role=capability), R3.1 (match by capability).
- **[02-module-author-guide/writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md)**

## Source

- [`drivers/ntc/manifest.json`](../../../../drivers/ntc/manifest.json)
- [`drivers/ntc/include/ntc_driver.h`](../../../../drivers/ntc/include/ntc_driver.h)
- [`drivers/ntc/src/ntc_driver.cpp`](../../../../drivers/ntc/src/ntc_driver.cpp)
