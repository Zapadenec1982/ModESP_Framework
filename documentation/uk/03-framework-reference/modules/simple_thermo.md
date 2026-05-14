# `simple_thermo` — reference ON/OFF термостат

> 📖 **In English:** [documentation/en/03-framework-reference/modules/simple_thermo.md](../../../en/03-framework-reference/modules/simple_thermo.md)

`simple_thermo` — reference business module фреймворку — мінімальний
hysteresis thermostat що reads temperature з SharedState, applies
setpoint+differential логіку, і drives binary output. Ships з фреймворком
primarily як **перший module що ви повинні study** при learning як
типовий business module structured.

~150 LOC C++ + ~100-line manifest. Найкраща перша річ для читання
після `quickstart.md` і `02-module-author-guide/overview.md`.

REQUIRES: `modesp_core`. Reads `equipment.air_temp`; writes
`simple_thermo.output` (binary heating request).

## Поведінка

ON/OFF з symmetric hysteresis (deadband):

```
Output := ON  якщо temp < (setpoint - differential)
Output := OFF якщо temp >= setpoint
Output := unchanged otherwise (у deadband)
```

Initial state OFF. Setpoint і differential — runtime-configurable і
persist across reboots.

## State keys

| Key | Type | Notes |
|---|---|---|
| `simple_thermo.temperature` | float | Current reading (mirrors equipment.air_temp). |
| `simple_thermo.setpoint` | float | User setpoint (5-40 °C, default 22). Persisted. |
| `simple_thermo.differential` | float | Hysteresis (0.5-5 °C, default 1). Persisted. |
| `simple_thermo.state` | string | `"off"` / `"heating"` / `"idle"`. |
| `simple_thermo.output` | bool | Heating request — connect до actuator. |

`setpoint` і `differential` accept MQTT writes (`mqtt_subscribe: true`).

## Як це wired

Reads `equipment.air_temp` (provided Equipment Manager + temperature
driver), writes `simple_thermo.output`. Щоб actually drive relay, route
`simple_thermo.output` до actuator role через вашу business логіку АБО
additional binding-aware module.

Typical loop:

1. Equipment Manager reads DS18B20 sensor → writes `equipment.air_temp`.
2. simple_thermo reads `equipment.air_temp`, applies hysteresis,
   updates `simple_thermo.output`.
3. (Ви wire якийсь link між `simple_thermo.output` і
   `equipment.req_<heater_role>` — або simple module АБО Equipment
   Manager configured щоб mirror ці keys.)

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
directly — це найпростіший BaseModule subclass у codebase.

## UI surface (auto-generated)

Manifest declares дві cards:

1. **State** (read-only): temperature value, state string, output indicator.
2. **Settings**: setpoint slider, differential number input.

WebUI page **"Thermostat"** з тими cards.

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

## Чому це good first read

- Manifest covers state, mqtt, loggable, ui — усі common sections.
- C++ class **trivial** (~50 lines) — easy to follow.
- Demonstrates hysteresis pattern reusable у many domains.
- Shows `equipment.* → simple_thermo.* → equipment.req_*` data flow.
- Persisted setpoint pattern.

Після understanding цього module, write власний variant із different
control логіку АБО multi-zone support. Structure transfers.

## Що далі

- **[02-module-author-guide/writing-a-module.md](../../02-module-author-guide/writing-a-module.md)** —
  walkthrough using це pattern.
- **[modules/equipment.md](equipment.md)** — upstream sensor provider.
- **[modules/datalogger.md](datalogger.md)** — downstream data consumer.

## Source

- [`modules/simple_thermo/manifest.json`](../../../../modules/simple_thermo/manifest.json)
- [`modules/simple_thermo/include/simple_thermo_module.h`](../../../../modules/simple_thermo/include/simple_thermo_module.h)
- [`modules/simple_thermo/src/simple_thermo_module.cpp`](../../../../modules/simple_thermo/src/simple_thermo_module.cpp)
