# `simple_thermo` — reference ON/OFF thermostat

> 📖 **Українською:** [documentation/uk/03-framework-reference/modules/simple_thermo.md](../../../uk/03-framework-reference/modules/simple_thermo.md)

`simple_thermo` is the framework's reference business module — а minimal
hysteresis thermostat що reads temperature з SharedState, applies
setpoint+differential logic, і drives а binary output. Ships із the
framework primarily as **the first module you should study** when
learning how а typical business module is structured.

~150 LOC C++ + а ~100-line manifest. Best first read after `quickstart.md`
і `02-module-author-guide/overview.md`.

REQUIRES: `modesp_core`. Reads `equipment.air_temp`; writes
`simple_thermo.output` (binary heating request).

## Behavior

ON/OFF із symmetric hysteresis (deadband):

```
Output := ON  if temp < (setpoint - differential)
Output := OFF if temp >= setpoint
Output := unchanged otherwise (within deadband)
```

Initial state OFF. Setpoint і differential are runtime-configurable і
persist across reboots.

## State keys

| Key | Type | Notes |
|---|---|---|
| `simple_thermo.temperature` | float | Current reading (mirrors equipment.air_temp). |
| `simple_thermo.setpoint` | float | User setpoint (5-40 °C, default 22). Persisted. |
| `simple_thermo.differential` | float | Hysteresis (0.5-5 °C, default 1). Persisted. |
| `simple_thermo.state` | string | `"off"` / `"heating"` / `"idle"`. |
| `simple_thermo.output` | bool | Heating request — connect to actuator. |

`setpoint` і `differential` accept MQTT writes (`mqtt_subscribe: true`).

## How it's wired

Reads `equipment.air_temp` (provided by Equipment Manager + temperature
driver), writes `simple_thermo.output`. To actually drive а relay, route
`simple_thermo.output` to an actuator role through your business
logic OR an additional binding-aware module.

Typical loop:

1. Equipment Manager reads DS18B20 sensor → writes `equipment.air_temp`.
2. simple_thermo reads `equipment.air_temp`, applies hysteresis,
   updates `simple_thermo.output`.
3. (You wire some link between `simple_thermo.output` і
   `equipment.req_<heater_role>` — either а simple module OR Equipment
   Manager configured to mirror these keys.)

## C++ source overview

Header (`modules/simple_thermo/include/simple_thermo_module.h`, ~28 lines):

```cpp
class SimpleThermoModule : public modesp::BaseModule {
public:
    SimpleThermoModule();
    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
private:
    bool heating_ = false;
};
```

Source (~55 lines): straightforward hysteresis у `on_update`. Read code
directly — it's the simplest BaseModule subclass у the codebase.

## UI surface (auto-generated)

Manifest declares two cards:

1. **State** (read-only): temperature value, state string, output indicator.
2. **Settings**: setpoint slider, differential number input.

WebUI page **"Thermostat"** із those cards.

## MQTT topics

Publishes:
- `<base>/simple_thermo/temperature`
- `<base>/simple_thermo/state`
- `<base>/simple_thermo/output`

Accepts:
- `<base>/cmd/simple_thermo.setpoint`
- `<base>/cmd/simple_thermo.differential`

## DataLogger integration

Auto-logged:
- Channel `simple_thermo.temperature` (temperature, default-on).
- Event `simple_thermo.output` (id 30, both edges, "Heating ON" / "Heating OFF").

## Why це а good first read

- Manifest covers state, mqtt, loggable, ui — all the common sections.
- C++ class is **trivial** (~50 lines) — easy to follow.
- Demonstrates hysteresis pattern reusable у many domains.
- Shows the `equipment.* → simple_thermo.* → equipment.req_*` data flow.
- Persisted setpoint pattern.

After understanding це module, write your own variant із different
control logic OR multi-zone support. The structure transfers.

## Next steps

- **[02-module-author-guide/writing-a-module.md](../../02-module-author-guide/writing-a-module.md)** —
  walkthrough using це pattern.
- **[modules/equipment.md](equipment.md)** — the upstream sensor provider.
- **[modules/datalogger.md](datalogger.md)** — downstream data consumer.

## Source

- [`modules/simple_thermo/manifest.json`](../../../../modules/simple_thermo/manifest.json)
- [`modules/simple_thermo/include/simple_thermo_module.h`](../../../../modules/simple_thermo/include/simple_thermo_module.h)
- [`modules/simple_thermo/src/simple_thermo_module.cpp`](../../../../modules/simple_thermo/src/simple_thermo_module.cpp)
