# Drivers — using and writing

> 📖 **Українською:** [documentation/uk/02-module-author-guide/writing-a-driver.md](../../uk/02-module-author-guide/writing-a-driver.md)

A driver is a narrow adapter that bridges one peripheral (DS18B20, NTC
thermistor, GPIO relay, I2C IO expander) to the framework's HAL. It implements
`ISensorDriver` (read-only input) or `IActuatorDriver` (controllable output),
nothing more. Business logic never touches GPIO — it reads sensor values and
writes actuator requests through SharedState; drivers convert between
SharedState and real I/O.

This page has two halves:

1. **[Using existing drivers](#using-existing-drivers)** — bind a shipped driver
   to your hardware and enable/disable it in menuconfig.
2. **[Writing a new driver](#writing-a-new-driver)** — add support for a new
   sensor or actuator. The framework wires it in automatically (registry +
   generated menuconfig toggle); you never edit `driver_manager.cpp`.

Shipped drivers: `ds18b20`, `ntc`, `digital_input`, `relay`, `pcf8574_relay`,
`pcf8574_input` (`drivers/*/`).

---

## Using existing drivers

### 1. Declare the hardware in `board.json`

`boards/<board>/board.json` lists the physical resources of the board — GPIO
outputs, OneWire buses, ADC channels, I2C expanders — each with an `id`. This is
the board author's job; drivers reference these ids, never raw pins. See
[04-hardware/board-config.md](../04-hardware/board-config.md).

### 2. Bind driver → role in `bindings.json`

`boards/<board>/bindings.json` maps a hardware `id` to a driver type and a
logical `role`:

```json
{
  "manifest_version": 1,
  "bindings": [
    {"hardware": "ow_1",  "driver": "ds18b20",       "role": "air_temp",  "module": "equipment",
     "address": "28:8C:5E:45:D4:08:44:09"},
    {"hardware": "din_1", "driver": "pcf8574_input",  "role": "door_contact", "module": "equipment"},
    {"hardware": "relay_1","driver": "pcf8574_relay", "role": "compressor", "module": "equipment"}
  ]
}
```

| Field      | Meaning |
|------------|---------|
| `hardware` | An `id` from `board.json` (the physical resource). |
| `driver`   | The driver's manifest `driver` field (its type string). |
| `role`     | Logical name. Becomes the `equipment.<role>` SharedState key. |
| `module`   | Owning module (usually `equipment`). |
| `address`  | Optional. ROM address for multi-device buses (e.g. several DS18B20 on one OneWire pin). Omit for single-device buses. |

`DriverManager::init()` walks the bindings, asks the registry for a factory
matching each `driver` string, and the factory builds the instance from the HAL
resource named by `hardware`. Resulting state keys appear automatically:

- `equipment.air_temp` (float) — sensor value, updated each read interval.
- `equipment.compressor` (bool) — actuator's actual state.
- `equipment.air_temp_ok` (bool) — sensor health.

### 3. Enable/disable in menuconfig

Every driver is **optional**. `idf.py menuconfig` → **ModESP Drivers**:

```
[*] ds18b20 driver
[*] ntc driver
[ ] pcf8574_input driver     ← disabled: not compiled, smaller binary
...
```

A disabled driver is not compiled at all (smaller flash). If the active board's
bindings **use** a driver you disabled, the **build fails** with a clear error
(`components/modesp_hal/CMakeLists.txt`) — it won't silently ship a dead binding.
Reconcile menuconfig with the board automatically:

```bash
python tools/drivers_sync.py --fix      # enable the drivers this board binds
python tools/drivers_sync.py --prune    # disable drivers this board doesn't use
python tools/drivers_sync.py --dry-run  # preview only
```

So: disable the drivers your board doesn't use to shrink the image (or run
`--prune`); keep the defaults (all enabled) if you don't care. The toggle for
every driver is generated automatically from `drivers/*/manifest.json` — see below.

---

## Writing a new driver

Adding a driver requires **no change to `driver_manager.cpp`**. You write the
driver, register its factory with a one-line macro, and use the CMake helper;
the build generates the menuconfig toggle, the dependency list, and the
registration call for you.

### Folder layout

```
drivers/my_sensor/
├── manifest.json                  ← REQUIRED — driver contract
├── CMakeLists.txt                 ← REQUIRED — uses modesp_driver_component()
├── include/
│   └── my_sensor_driver.h         ← class : ISensorDriver / IActuatorDriver
└── src/
    └── my_sensor_driver.cpp       ← implementation + factory + registration
```

The folder name, the manifest `driver` field, and the registration macro name
**must all match** (`my_sensor`), lower-snake-case (`^[a-z][a-z0-9_]*$`).

### Step 1 — manifest

`drivers/my_sensor/manifest.json` (sensor example):

```json
{
  "manifest_version": 1,
  "driver": "my_sensor",
  "description": "Demo analog sensor",
  "category": "sensor",
  "hardware_type": "adc",
  "requires_address": false,
  "multiple_per_bus": false,
  "provides": {"type": "float", "unit": "°C", "range": [-40, 150]},
  "settings": [
    {"key": "read_interval_ms", "type": "int",   "default": 1000, "min": 100, "max": 60000, "unit": "ms", "persist": true},
    {"key": "offset",           "type": "float", "default": 0.0,  "min": -10.0, "max": 10.0, "step": 0.1, "unit": "°C", "persist": true}
  ]
}
```

`category` (`sensor`/`actuator`) and the `driver` name are all the **generator**
needs — it does not need your C++ class name. Full field reference:
[manifest.md](manifest.md#driver-only-sections).

### Step 2 — implement the interface

```cpp
// drivers/my_sensor/include/my_sensor_driver.h
#pragma once
#include "modesp/hal/driver_interfaces.h"
#include "etl/string.h"
#include "driver/gpio.h"

class MySensorDriver : public modesp::ISensorDriver {
public:
    void configure(const char* role, gpio_num_t adc_pin, float offset);

    bool init() override;
    void update(uint32_t dt_ms) override;
    bool read(float& value) override;
    bool is_healthy() const override { return healthy_; }
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "my_sensor"; }
    uint32_t error_count() const override { return errors_; }

private:
    etl::string<16> role_;
    gpio_num_t adc_pin_ = GPIO_NUM_NC;
    float offset_ = 0.0f, last_value_ = 0.0f;
    bool has_reading_ = false, healthy_ = false;
    uint32_t errors_ = 0, elapsed_ms_ = 0;
};
```

The interface itself (`modesp::ISensorDriver` / `IActuatorDriver`) is in
[`driver_interfaces.h`](../../../components/modesp_hal/include/modesp/hal/driver_interfaces.h):

| `ISensorDriver` | `IActuatorDriver` |
|-----------------|-------------------|
| `init()`, `update(dt_ms)` | `init()`, `update(dt_ms)` |
| `read(float&) → bool` | `set(bool) → bool`, `get_state() → bool` |
| `is_healthy()`, `role()`, `type()`, `error_count()` | `set_value(0..1)`, `supports_analog()` (analog, optional) |
| | `role()`, `type()`, `is_healthy()`, `emergency_stop()`, `switch_count()` |

`configure()` is your own method — its signature is whatever your factory needs
(Step 3). Implementation patterns (periodic sampling, health, offset, switch
lockout, analog) are in [Patterns](#patterns) below.

### Step 3 — factory + registration (the one wiring point)

At the bottom of the `.cpp`, add a **factory** that turns a `Binding` + the HAL
into a configured instance, and register it with one macro. This is the only
place a driver plugs into the framework — `driver_manager.cpp` stays untouched.

```cpp
// drivers/my_sensor/src/my_sensor_driver.cpp  (bottom of file)
#include "modesp/hal/driver_registry.h"
#include "modesp/hal/hal.h"
#include "etl/string_view.h"

namespace {
// Zero-heap static pool — one slot per possible instance on this board.
MySensorDriver s_pool[modesp::MAX_SENSORS];
size_t         s_n = 0;

modesp::ISensorDriver* my_sensor_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_n >= modesp::MAX_SENSORS) { ESP_LOGE(TAG, "pool exhausted"); return nullptr; }

    // Resolve the physical resource named by binding.hardware_id via the HAL.
    auto* adc = hal.find_adc_channel(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!adc) { ESP_LOGE(TAG, "ADC '%s' not found", b.hardware_id.c_str()); return nullptr; }

    auto& drv = s_pool[s_n++];
    drv.configure(b.role.c_str(), adc->gpio, /*offset=*/0.0f);
    return &drv;
}
} // namespace

MODESP_REGISTER_SENSOR(my_sensor, &my_sensor_factory)   // name == manifest "driver"
```

For an actuator: return `modesp::IActuatorDriver*` and use
`MODESP_REGISTER_ACTUATOR(my_relay, &my_relay_factory)`.

**HAL resource finders** (pick the one matching your `hardware_type`):

| Finder | Returns (fields you use) |
|--------|--------------------------|
| `find_onewire_bus(id)`     | `->gpio` |
| `find_gpio_output(id)`     | `->gpio`, `->active_high` |
| `find_gpio_input(id)`      | `->gpio`, `->pull_up` |
| `find_adc_channel(id)`     | `->gpio`, `->atten` |
| `find_expander_output(id)` | `->expander_id`, `->pin`, `->active_high` |
| `find_expander_input(id)`  | `->expander_id`, `->invert`, `->pin` |
| `find_i2c_expander(id)`    | the shared `I2CExpanderResource` (for read/write) |

The factory returns `nullptr` on any failure (pool full, resource missing) — the
binding is then skipped with a warning, the rest keep going. The
`MAX_SENSORS` / `MAX_ACTUATORS` pool caps are in
[`hal_types.h`](../../../components/modesp_hal/include/modesp/hal/hal_types.h).

> Why a factory and not just a class, like modules? Modules are
> default-constructed singletons; drivers are **multi-instance and bound to a
> HAL resource** (8 DS18B20 sensors, each on a different ROM address). The
> factory is the small amount of per-type wiring the generator cannot infer from
> the manifest.

### Step 4 — CMakeLists.txt

Use the helper — it makes the driver optional automatically:

```cmake
# drivers/my_sensor/CMakeLists.txt
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/my_sensor_driver.cpp"
    PRIV_REQUIRES esp_adc          # extra deps; "driver" for GPIO, "esp_adc" for ADC, etc.
)
```

`modesp_driver_component()` adds `REQUIRES modesp_hal` for you and compiles
`SRCS` only when `CONFIG_MODESP_DRIVER_MY_SENSOR` is set. Do **not** call
`idf_component_register` directly and do **not** hardcode `${CMAKE_SOURCE_DIR}`
(it points at `build/` during requirements expansion — the helper uses the
right path).

### Step 5 — build

```bash
idf.py build
```

The generator (`tools/generate_ui.py`) scans `drivers/*/manifest.json` and emits,
into `generated/` and `components/modesp_hal/Kconfig` (all DO-NOT-EDIT):

- `MODESP_DRIVER_MY_SENSOR` menuconfig toggle (default `y`) under **ModESP Drivers**;
- the `modesp_hal` dependency list;
- the guarded `modesp_register_all_drivers()` call.

Your driver is now optional in `idf.py menuconfig` with zero extra work. Then
bind it ([Using existing drivers](#using-existing-drivers)) and flash.

> **First-build note:** a brand-new driver adds a *new* component Kconfig file.
> If its toggle doesn't appear in menuconfig, run `idf.py fullclean` once —
> ESP-IDF caches the component-Kconfig list and only rescans on a clean
> configure.

---

## Patterns

**Periodic sampling** (drivers tick at 100 Hz — gate slow work):

```cpp
void update(uint32_t dt_ms) override {
    elapsed_ms_ += dt_ms;
    if (elapsed_ms_ < read_interval_ms_) return;
    elapsed_ms_ = 0;
    // ... do the read ...
}
```

**Saturating error counter / health** (an unhealthy sensor must *stay*
unhealthy — `uint8_t` wraps 255→0 and would falsely recover):

```cpp
if (read_failed) {
    if (errors_ < MAX_CONSECUTIVE_ERRORS) errors_++;   // saturate, don't wrap
} else {
    errors_ = 0;                                        // recovered
}
// is_healthy() => errors_ < MAX_CONSECUTIVE_ERRORS
```

**Calibration / offset at read time** (so changing the setting takes effect
without a restart):

```cpp
bool read(float& value) override {
    if (!has_reading_) return false;
    value = raw_value_ + offset_;   // applied here, not at store time
    return true;
}
```

**Minimum switch interval (compressor safety)** — reject `set()` calls that come
too fast; compressors/pumps need ~60–180 s between switches:

```cpp
bool set(bool state) override {
    if (state == on_) return true;
    if (min_switch_ms_ > 0 && since_switch_ms_ < min_switch_ms_) return false;
    apply_gpio(state); on_ = state; since_switch_ms_ = 0; if (state) cycles_++;
    return true;
}
```

**Shared bus state (I2C expander)** — when several relays share one PCF8574
output byte, snapshot it before the write and **revert on failure**, or a stale
bit leaks into the next relay's write:

```cpp
uint8_t saved = expander_->output_state;
expander_->output_state |= (1 << pin_);
if (!expander_->write_state()) { expander_->output_state = saved; return false; }
```

**Analog actuator** — override `set_value`/`supports_analog` for PWM/variable
output; the default maps `set_value(>0.5)` to `set(true)`.

---

## Common mistakes

- **Heap in `update()`** — drivers tick at 100 Hz; `new`/`std::string`/`std::vector`
  in the hot path leak per tick. Use static pools and ETL types only.
- **Blocking I/O in `update()`** — an I2C transaction can stall ~10 ms; a DS18B20
  conversion is 750 ms. Split slow protocols across multiple `update()` calls
  (start → poll-ready → read); never `vTaskDelay` in a retry loop on the shared
  100 Hz task — it stalls every other driver and module.
- **Name mismatch** — folder, manifest `driver`, and the `MODESP_REGISTER_*`
  macro name must be identical. A mismatch means the generated
  `modesp_register_driver_<name>()` symbol won't link.
- **Forgetting the safe state on init** — return `false` from `init()` (and keep
  `healthy_ = false`) if the hardware probe fails; drivers must power up OFF.
- **Calling `idf_component_register` directly** — breaks optionality; use
  `modesp_driver_component()`.

---

## Existing drivers to study, simplest first

- [`drivers/relay/`](../../../drivers/relay/) — simplest actuator (GPIO + switch lockout).
- [`drivers/digital_input/`](../../../drivers/digital_input/) — simplest sensor (GPIO contact + debounce).
- [`drivers/ntc/`](../../../drivers/ntc/) — ADC sensor with B-parameter math + attenuation mapping.
- [`drivers/ds18b20/`](../../../drivers/ds18b20/) — OneWire sensor, MATCH_ROM/SKIP_ROM, async conversion, scan API.
- [`drivers/pcf8574_relay/`](../../../drivers/pcf8574_relay/) — I2C-expander actuator sharing a bus with siblings.

Each ends with the `factory + MODESP_REGISTER_*` block — copy that shape.

## Next steps

- [manifest.md](manifest.md#driver-only-sections) — driver manifest field reference.
- [04-hardware/bindings.md](../04-hardware/bindings.md) — `bindings.json` schema.
- [04-hardware/board-config.md](../04-hardware/board-config.md) — `board.json` hardware resources.
- [03-framework-reference/modules/equipment.md](../03-framework-reference/modules/equipment.md) — Equipment Manager, which owns drivers and exposes `equipment.*`.
