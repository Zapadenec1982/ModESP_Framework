# `ntc` — NTC термістор на ADC channel

> 📖 **In English:** [documentation/en/03-framework-reference/drivers/ntc.md](../../../en/03-framework-reference/drivers/ntc.md)

NTC термістор через ESP32 ADC channel з B-parameter equation. Дешевий,
simple, common у refrigeration і HVAC де ±0.5 °C accuracy enough.
Range typically -40…+125 °C depending на resistor і thermistor choice.

Driver registers як `sensor` з `hardware_type: adc_channel` і
`requires_address: false`. Single sensor per bound ADC channel.

REQUIRES: ESP-IDF ADC driver, `modesp_hal`.

## Bindings

```json
{
  "id": "ambient",
  "driver": "ntc",
  "hardware_id": "ntc_ambient",
  "role": "ambient_temp",
  "channel": "adc_channel_3"
}
```

`channel` references board-defined ADC channel (з attenuation і
bit-width). Driver не configures ADC сам — це board's responsibility.

## Settings (default не persisted — adjust manifest)

| Key | Type | Default | Notes |
|---|---|---|---|
| `beta` | int 2000-5000 | 3950 | B-coefficient термістора. |
| `r_series` | int 1k-100kΩ | 10000 | Series resistor (pull-up). |
| `r_nominal` | int 1k-100kΩ | 10000 | NTC resistance at 25 °C. |
| `read_interval_ms` | int 100-60000 | 1000 | Інтервал опитування. |
| `offset` | float -5…+5 °C | 0.0 | Корекція показань. |

Most NTCs ship із datasheet values для B і R25. Common: B = 3950, R25 = 10 kΩ.

## Provides

`{"type": "float", "unit": "°C", "range": [-40, 125]}` — publishes до
`equipment.<role>`.

NaN publishes якщо ADC reading is rail (0 або full-scale → sensor open
або short).

## B-parameter equation

Standard Steinhart-simplified form:

```
1/T = 1/T0 + (1/B) * ln(R / R0)
T   = T - 273.15  (Celsius)
```

Де `R` — measured NTC resistance computed з ADC voltage і
series resistor у voltage divider.

Driver applies formula на кожному sample. No table lookup,
no interpolation. Compute cost ~5 µs.

## Discovery

NTC немає discovery — нічого scan-ити. Bindings must be
declared manually.

## Common pitfalls

**Wrong divider direction:** typical schematic puts NTC до GND і series
resistor до Vcc. Driver assumes це. Якщо schematic inverted, swap
у hardware або override `beta` sign (advanced).

**ADC nonlinearity:** ESP32 ADC має poor linearity near rails. Choose
`r_series` так щоб NTC voltage fell у middle 60% ADC range при
typical operating temperature.

**Self-heating:** при <1 kΩ NTC resistance + 3.3 V supply, current
exceeds 3 mA — self-heating стає significant. Use 10 kΩ NTC або
higher series resistor.

**B-coefficient drift:** B — log-fit і loses accuracy >40 °C від
25 °C. Для wide-range applications, consider DS18B20 instead.

## UI surface

Shipped manifest не declares ui cards (terse manifest). Add
settings card у вашому fork якщо operators need tune B
у field.

## Чому це good driver to read після `ds18b20`

- Contrast із digital sensor: NTC needs math, ADC config awareness.
- Demonstrates no-address case (`requires_address: false`).
- Demonstrates no-discovery case.
- Demonstrates non-persisted settings (defaults у manifest, not NVS).

## Що далі

- **[drivers/ds18b20.md](ds18b20.md)** — digital alternative.
- **[02-module-author-guide/writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md)**

## Source

- [`drivers/ntc/manifest.json`](../../../../drivers/ntc/manifest.json)
- [`drivers/ntc/include/ntc_driver.h`](../../../../drivers/ntc/include/ntc_driver.h)
- [`drivers/ntc/src/ntc_driver.cpp`](../../../../drivers/ntc/src/ntc_driver.cpp)
