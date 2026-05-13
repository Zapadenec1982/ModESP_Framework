# Writing а driver

> 📖 **Українською:** [documentation/uk/02-module-author-guide/writing-a-driver.md](../../uk/02-module-author-guide/writing-a-driver.md)

This page is а complete walkthrough of building а hardware driver — а C++
class implementing `ISensorDriver` or `IActuatorDriver` that bridges а
specific peripheral (DS18B20, NTC thermistor, GPIO relay, I2C IO expander)
to the framework's HAL abstraction. After reading this you'll be able to
add support for а new sensor or actuator, register it через bindings, і
have it appear automatically у `equipment.<role>` SharedState keys.

Drivers are NOT business modules. Business logic reads sensor values і
writes actuator requests through SharedState. Drivers convert between
SharedState і real I/O.

## What а driver is

A driver is а narrow adapter that:

- Implements one of two interfaces: `ISensorDriver` (read-only inputs) or
  `IActuatorDriver` (controllable outputs).
- Owns hardware-specific configuration: GPIO pin, bus address, calibration
  constants.
- Has а manifest declaring `category` (`sensor`/`actuator`), `hardware_type`
  (`gpio`/`onewire_bus`/`adc`/`i2c`/...), `provides` (output type), і
  per-instance `settings`.
- Is **instantiated by `DriverManager`** from `bindings.json` entries — not
  registered manually у `main.cpp`.
- Lives у `drivers/<name>/`, not `modules/`.

A binding ties а driver type + а physical address (GPIO pin, OneWire ROM,
I2C address) to а logical role (`air_temp`, `compressor`, etc.). Equipment
Manager reads sensor drivers і writes the values до `equipment.<role>`
state keys; reads request keys `equipment.req_<role>` and forwards them to
actuator drivers.

## Folder layout

```
drivers/your_sensor/
├── manifest.json                   ← REQUIRED — driver contract
├── CMakeLists.txt                  ← REQUIRED
├── include/
│   └── your_sensor_driver.h        ← Driver class declaration
└── src/
    └── your_sensor_driver.cpp      ← Implementation
```

## Step 1 — Write the manifest

Driver manifest differs from module manifest. Top-level fields are
driver-specific. Full reference у [manifest.md](manifest.md#driver-only-sections).

Minimal sensor example (`drivers/my_sensor/manifest.json`):

```json
{
  "manifest_version": 1,
  "driver": "my_sensor",
  "description": "Demo analog sensor",
  "category": "sensor",
  "hardware_type": "adc",
  "requires_address": true,
  "multiple_per_bus": false,
  "provides": {"type": "float", "unit": "°C", "range": [-40, 150]},

  "settings": [
    {
      "key": "read_interval_ms",
      "type": "int",
      "default": 1000,
      "min": 100, "max": 60000,
      "unit": "мс",
      "persist": true
    },
    {
      "key": "offset",
      "type": "float",
      "default": 0.0,
      "min": -10.0, "max": 10.0, "step": 0.1,
      "unit": "°C",
      "persist": true
    }
  ]
}
```

Minimal actuator example:

```json
{
  "manifest_version": 1,
  "driver": "my_relay",
  "description": "Demo GPIO relay",
  "category": "actuator",
  "hardware_type": "gpio",
  "requires_address": true,
  "multiple_per_bus": false,
  "provides": {"type": "bool", "description": "Relay state"},

  "settings": [
    {
      "key": "min_switch_ms",
      "type": "int",
      "default": 0,
      "min": 0, "max": 600000,
      "unit": "мс",
      "description": "Minimum interval between switches (compressor safety)",
      "persist": true
    }
  ]
}
```

## Step 2 — Implement the interface

### Sensor driver — `ISensorDriver`

```cpp
// drivers/my_sensor/include/my_sensor_driver.h
#pragma once
#include "modesp/hal/driver_interfaces.h"
#include "etl/string.h"
#include "driver/gpio.h"

class MySensorDriver : public modesp::ISensorDriver {
public:
    MySensorDriver() = default;

    /// Called by DriverManager before init().
    void configure(const char* role, gpio_num_t adc_pin, float offset);

    // ── ISensorDriver interface ──
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
    float offset_ = 0.0f;
    float last_value_ = 0.0f;
    bool has_reading_ = false;
    bool healthy_ = false;
    uint32_t errors_ = 0;
};
```

Source:

```cpp
// drivers/my_sensor/src/my_sensor_driver.cpp
#include "my_sensor_driver.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

static const char* TAG = "MySensor";

void MySensorDriver::configure(const char* role, gpio_num_t adc_pin, float offset) {
    role_ = role;
    adc_pin_ = adc_pin;
    offset_ = offset;
}

bool MySensorDriver::init() {
    // Set up ADC, allocate channel, etc.
    healthy_ = true;
    ESP_LOGI(TAG, "Initialized %s on ADC GPIO%d", role_.c_str(), (int)adc_pin_);
    return true;
}

void MySensorDriver::update(uint32_t dt_ms) {
    // Sample the ADC periodically. Use accumulator + interval pattern.
    static uint32_t elapsed = 0;
    elapsed += dt_ms;
    if (elapsed < 1000) return;  // settings.read_interval_ms would gate це
    elapsed = 0;

    int raw = 0;
    // ... ADC read ...
    float celsius = (raw * 0.01f) + offset_;
    last_value_ = celsius;
    has_reading_ = true;
}

bool MySensorDriver::read(float& value) {
    if (!has_reading_) return false;
    value = last_value_;
    return true;
}
```

### Actuator driver — `IActuatorDriver`

```cpp
// drivers/my_relay/include/my_relay_driver.h
#pragma once
#include "modesp/hal/driver_interfaces.h"
#include "etl/string.h"
#include "driver/gpio.h"

class MyRelayDriver : public modesp::IActuatorDriver {
public:
    MyRelayDriver() = default;

    void configure(const char* role, gpio_num_t gpio, uint32_t min_switch_ms = 0);

    bool init() override;
    void update(uint32_t dt_ms) override;
    bool set(bool state) override;
    bool get_state() const override { return on_; }
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "my_relay"; }
    bool is_healthy() const override { return initialized_; }
    void emergency_stop() override { set(false); }
    uint32_t switch_count() const override { return cycles_; }

private:
    etl::string<16> role_;
    gpio_num_t gpio_ = GPIO_NUM_NC;
    uint32_t min_switch_ms_ = 0;
    bool on_ = false;
    bool initialized_ = false;
    uint32_t cycles_ = 0;
    uint32_t since_switch_ms_ = UINT32_MAX;  // start unlocked
};
```

Source (key methods):

```cpp
bool MyRelayDriver::init() {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio_,
        .mode = GPIO_MODE_OUTPUT,
        // ... etc.
    };
    gpio_config(&cfg);
    gpio_set_level(gpio_, 0);  // start OFF
    initialized_ = true;
    return true;
}

void MyRelayDriver::update(uint32_t dt_ms) {
    // Saturating accumulator for min_switch_ms lockout.
    if (UINT32_MAX - dt_ms < since_switch_ms_) {
        since_switch_ms_ = UINT32_MAX;
    } else {
        since_switch_ms_ += dt_ms;
    }
}

bool MyRelayDriver::set(bool state) {
    if (state == on_) return true;  // no-op
    if (min_switch_ms_ > 0 && since_switch_ms_ < min_switch_ms_) {
        return false;  // locked out (compressor safety)
    }
    gpio_set_level(gpio_, state ? 1 : 0);
    on_ = state;
    since_switch_ms_ = 0;
    cycles_++;
    return true;
}
```

## Step 3 — Register у DriverManager

`components/modesp_hal/src/driver_manager.cpp` has а factory dispatch
based on `type` string у `bindings.json`. Add а case for your driver:

```cpp
// driver_manager.cpp
#include "my_sensor_driver.h"  // ← add include

// Inside create_sensor() switch...
if (strcmp(type, "my_sensor") == 0) {
    auto* drv = new MySensorDriver();
    drv->configure(role, parse_gpio(binding["pin"]), parse_float(binding["offset"]));
    return drv;
}
```

For actuators, the analogous `create_actuator()` factory.

> ⚠️ **Note:** this hand-edit step is the one piece of driver authoring що
> isn't manifest-driven yet. Stage 1.5 plans а driver auto-registration
> mechanism. Track [tools/generate_ui.md](../05-tools/generate_ui.md)
> *(planned)* for status.

## Step 4 — CMakeLists.txt

```cmake
# drivers/my_sensor/CMakeLists.txt
idf_component_register(
    SRCS "src/my_sensor_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_hal modesp_core
)
```

Drivers depend on `modesp_hal` (interface header) і `modesp_core` (logging,
types). Sensor drivers що use ADC also need `esp_adc`; I2C drivers need
`driver`; etc.

## Step 5 — Bind у `bindings.json`

The user (deployer) wires the driver to physical hardware у the board's
`bindings.json`:

```json
{
  "sensors": [
    {
      "role": "air_temp",
      "type": "my_sensor",
      "pin": "GPIO34",
      "offset": -1.2
    }
  ],
  "actuators": [
    {
      "role": "compressor",
      "type": "my_relay",
      "pin": "GPIO13",
      "min_switch_ms": 60000
    }
  ]
}
```

`DriverManager::init()` iterates bindings, instantiates the driver class for
each `type` string, calls its `configure()` із the binding values, then
`init()`.

Resulting state keys що appear automatically:
- `equipment.air_temp` (float) — read by business modules.
- `equipment.compressor` (bool) — actuator's current state.
- `equipment.req_compressor` (bool) — request key business modules write.

## Step 6 — Build, flash, verify

```bash
idf.py build
idf.py -p COM15 flash monitor
```

Expected log:

```
I (12345) DriverManager: Created sensor 'air_temp' (type=my_sensor, gpio=34)
I (12346) DriverManager: Created actuator 'compressor' (type=my_relay, gpio=13)
I (12350) MySensor: Initialized air_temp on ADC GPIO34
I (12352) MyRelay: Initialized compressor on GPIO13
```

In WebUI / via `/api/state`:
- `equipment.air_temp` updates із sensor readings every `read_interval_ms`.
- `equipment.req_compressor` can be toggled (by your business module or HTTP
  `POST /api/settings`).
- `equipment.compressor` mirrors actuator's actual state.

## Sensor patterns

**Periodic sampling:**

```cpp
void update(uint32_t dt_ms) override {
    elapsed_ms_ += dt_ms;
    if (elapsed_ms_ < read_interval_ms_) return;
    elapsed_ms_ = 0;
    // ... do the read ...
}
```

**Error counting / health:**

```cpp
if (read_failed) {
    errors_++;
    if (errors_ > MAX_CONSECUTIVE_ERRORS) {
        healthy_ = false;
    }
} else {
    if (errors_ > 0) errors_ = 0;  // recovered
    healthy_ = true;
}
```

**Calibration / offset:**

Apply at read time, not store time — so changing `offset` setting reflects
у the next read without restart:

```cpp
bool read(float& value) override {
    if (!has_reading_) return false;
    value = raw_value_ + offset_;  // offset applied here
    return true;
}
```

## Actuator patterns

**Minimum switch interval (compressor safety):**

Reject `set()` calls що come too quickly. Compressors і large pumps need
~60s between switches to avoid mechanical damage / refrigerant migration.

**Emergency stop:**

Default implementation calls `set(false)`. Override якщо your hardware needs
а different safe state (e.g., valve to "fully open" rather than "closed").

```cpp
void emergency_stop() override {
    set_value(1.0f);  // open valve fully on е-stop
}
```

**Analog support:**

If your actuator supports PWM / variable output, override `set_value` /
`get_value` / `supports_analog`:

```cpp
bool set_value(float value_0_1) override {
    uint8_t duty = static_cast<uint8_t>(value_0_1 * 255.0f);
    pwm_set_duty(channel_, duty);
    return true;
}
bool supports_analog() const override { return true; }
```

## Common mistakes

**Forgetting `configure()` call:** if you skip `DriverManager` factory wiring,
your driver compiles but `bindings.json` references never produce an instance.
Symptom: `equipment.<role>` key never appears у SharedState.

**Heap allocation у update():** drivers tick at 100 Hz. `new` / `std::string`
у the hot path leaks bytes per tick. Use stack arrays і ETL types only.

**Blocking I/O у update():** I2C bus transactions can stall ~10 ms; OneWire
can take 750 ms (DS18B20 conversion). If your protocol is slow, either:
- Implement state machine across multiple `update()` calls (start sample,
  poll for ready, read result).
- Use ESP-IDF async APIs (I2C ISR-based driver, OneWire bus that polls
  separately).

**Forgetting healthy state on init failure:** business modules check
`is_healthy()` to decide whether to fall back to safe defaults. Return
`false` from `init()` AND keep `healthy_ = false` if hardware probe fails.

**Not respecting `min_switch_ms`:** compressors і pumps fail mechanically if
switched too fast. Always implement the lockout у actuator drivers, even
if not all bindings configure it.

## Next steps

- **[components/modesp_hal.md](../03-framework-reference/components/modesp_hal.md)**
  *(planned)* — full HAL і DriverManager reference.
- **[hardware/bindings.md](../04-hardware/bindings.md)** *(planned)* —
  `bindings.json` schema і examples.
- **[hardware/board-config.md](../04-hardware/board-config.md)** *(planned)*
  — `board.json` (hardware capabilities) і how drivers map onto it.
- **[modules/equipment.md](../03-framework-reference/modules/equipment.md)**
  *(planned)* — Equipment Manager — the module що owns drivers і exposes
  `equipment.*` state keys.

## Existing drivers to study source-first

- [`drivers/relay/`](../../../drivers/relay/) — simplest actuator (GPIO + min
  switch interval). Best first read.
- [`drivers/digital_input/`](../../../drivers/digital_input/) — simplest
  sensor (GPIO contact, debounce).
- [`drivers/ntc/`](../../../drivers/ntc/) — ADC-based sensor із calibration
  table.
- [`drivers/ds18b20/`](../../../drivers/ds18b20/) — OneWire sensor із async
  conversion, discovery API.
- [`drivers/pcf8574_relay/`](../../../drivers/pcf8574_relay/) — I2C-expanded
  actuator (shares а bus із siblings).
