# `modesp_hal` — Hardware Abstraction Layer і DriverManager

> 📖 **Українською:** [documentation/uk/03-framework-reference/components/modesp_hal.md](../../../uk/03-framework-reference/components/modesp_hal.md)

`modesp_hal` provides hardware abstractions that decouple business logic
from physical I/O. It owns: the HAL itself (GPIO/ADC/I²C/OneWire setup
based на board.json), driver interfaces (`ISensorDriver`,
`IActuatorDriver`), і `DriverManager` that instantiates і drives driver
instances from `bindings.json`.

Business modules don't interact із це layer directly — they read
`equipment.<role>` state keys що Equipment Manager produces. Drivers
implement the interfaces; HAL gives them access to peripherals. Це page
documents how the layer is structured і what extension points exist.

REQUIRES: `modesp_core`, `modesp_services` (config_service для reading
board.json/bindings.json), ESP-IDF peripheral drivers (`driver`,
`esp_adc`).

## Component layout

```
components/modesp_hal/include/modesp/hal/
├── hal.h                   ← HAL class — peripheral setup
├── hal_types.h             ← BoardConfig, BindingTable parsed structs
├── driver_interfaces.h     ← ISensorDriver, IActuatorDriver
└── driver_manager.h        ← DriverManager — factory + lifecycle owner
```

## `HAL` — peripheral initialisation

```cpp
#include "modesp/hal/hal.h"

class HAL {
public:
    HAL();

    bool init(const BoardConfig& board);

    // OneWire bus access (used by DS18B20 driver)
    OneWireBus* onewire(const char* id);

    // I2C bus access (used by PCF8574, sensors)
    I2cBus* i2c(const char* id);

    // ADC unit access (used by NTC, generic ADC)
    AdcUnit* adc();
};
```

`init(board)` walks the parsed `BoardConfig` (з ConfigService) і:

1. Configures GPIO outputs (relays, LEDs).
2. Configures GPIO inputs із pull-up/down.
3. Sets up OneWire buses (GPIO mode, pull-up, bus driver).
4. Initialises I²C bus controllers (clock, pins).
5. Probes I²C expanders (PCF8574 etc.) і registers them.
6. Configures ADC units і channels із calibrated attenuation.

Drivers don't call ESP-IDF directly — вони access these peripherals
через HAL accessors. Це centralises pin / clock / power-domain
management і lets HAL handle conflicts (е.g., GPIO 25 can't be both relay
і ADC).

State keys (debugging):

| Key | Notes |
|---|---|
| `hal.initialised` | true після успішного `init()`. |
| `hal.onewire_count` | Number of OneWire buses active. |
| `hal.i2c_count` | Number of I²C buses active. |
| `hal.adc_channels` | Number of ADC channels configured. |

## Driver interfaces

```cpp
#include "modesp/hal/driver_interfaces.h"
namespace modesp {

class ISensorDriver {
public:
    virtual ~ISensorDriver() = default;
    virtual bool init() = 0;
    virtual void update(uint32_t dt_ms) = 0;
    virtual bool read(float& value) = 0;
    virtual bool is_healthy() const = 0;
    virtual const char* role() const = 0;
    virtual const char* type() const = 0;
    virtual uint32_t error_count() const { return 0; }
};

class IActuatorDriver {
public:
    virtual ~IActuatorDriver() = default;
    virtual bool init() = 0;
    virtual void update(uint32_t dt_ms) = 0;
    virtual bool set(bool state) = 0;
    virtual bool get_state() const = 0;
    virtual bool set_value(float v) { return set(v > 0.5f); }
    virtual float get_value() const { return get_state() ? 1.0f : 0.0f; }
    virtual bool supports_analog() const { return false; }
    virtual const char* role() const = 0;
    virtual const char* type() const = 0;
    virtual bool is_healthy() const = 0;
    virtual void emergency_stop() { set(false); }
    virtual uint32_t switch_count() const { return 0; }
};

}
```

Two-flavor approach because sensors і actuators have fundamentally
different shapes — а sensor is "read а value", an actuator is "command а
state із safety і feedback".

Authors implement these to add new hardware support. See
[writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md)
for the author-side walkthrough.

## `DriverManager` — driver factory і lifecycle

```cpp
#include "modesp/hal/driver_manager.h"

class DriverManager {
public:
    DriverManager();

    // Read bindings.json, instantiate drivers, call init() on each.
    bool init(const BindingTable& bindings, HAL& hal);

    // Drive update() on all drivers every tick.
    void update_all(uint32_t dt_ms);

    // Accessors for Equipment Manager.
    ISensorDriver* find_sensor(const char* role);
    IActuatorDriver* find_actuator(const char* role);

    size_t sensor_count() const;
    size_t actuator_count() const;

    void for_each_sensor(std::function<void(ISensorDriver&)> fn);
    void for_each_actuator(std::function<void(IActuatorDriver&)> fn);
};
```

Workflow:

1. Equipment Manager calls `driver_manager.init(bindings, hal)` у its
   own `on_init`.
2. `init` iterates `bindings.bindings` array. For each entry:
   - Look up the `type` string (е.g., "ds18b20") у the factory table.
   - Call the factory: `new MySensorDriver()`.
   - Call `driver->configure(role, hardware_params...)` із values з
     binding і board.json.
   - Call `driver->init()`. On failure, log і continue (other drivers
     should still try).
3. After init, Equipment Manager queries `find_sensor`/`find_actuator`
   by role і wires them into `equipment.*` SharedState keys.
4. Every tick, Equipment Manager calls `driver_manager.update_all(dt_ms)`,
   which fans out to each driver's `update(dt_ms)`.

## Factory dispatch (current Stage 1)

The factory table is hand-coded у `driver_manager.cpp`:

```cpp
// driver_manager.cpp (simplified)
ISensorDriver* DriverManager::create_sensor(const char* type, ...) {
    if (strcmp(type, "ds18b20") == 0) {
        auto* drv = new DS18B20Driver();
        drv->configure(role, bus_id, address);
        return drv;
    }
    if (strcmp(type, "ntc") == 0) {
        auto* drv = new NTCDriver();
        drv->configure(role, adc_pin, ...);
        return drv;
    }
    // ... more types ...
    return nullptr;   // unknown type
}
```

Adding а driver requires editing this dispatch — one place where
manifest-driven generation hasn't reached yet. Stage 1.5 plans
auto-registration через а compile-time-generated factory table from
driver manifests. Track [tools/generate_ui.md](../../05-tools/generate_ui.md)
*(planned)*.

## BoardConfig і BindingTable structures

```cpp
struct GpioOutputCfg {
    etl::string<16> id;
    int gpio;
    bool active_high;
};

struct OneWireBusCfg {
    etl::string<16> id;
    int gpio;
};

struct I2cBusCfg {
    etl::string<16> id;
    int sda, scl;
    uint32_t freq_hz;
};

struct I2cExpanderCfg {
    etl::string<16> id;
    etl::string<16> bus;
    etl::string<16> chip;
    uint8_t address;
    uint8_t pins;
};

struct BoardConfig {
    etl::string<32> board_name;
    etl::vector<GpioOutputCfg, 16> gpio_outputs;
    etl::vector<GpioInputCfg, 8> gpio_inputs;
    etl::vector<OneWireBusCfg, 4> onewire_buses;
    etl::vector<I2cBusCfg, 2> i2c_buses;
    etl::vector<I2cExpanderCfg, 4> i2c_expanders;
    etl::vector<ExpanderOutputCfg, 32> expander_outputs;
    etl::vector<ExpanderInputCfg, 32> expander_inputs;
    etl::vector<AdcChannelCfg, 8> adc_channels;
};

struct BindingEntry {
    etl::string<16> hardware;        // matches board.json's id
    etl::string<16> driver;          // driver type
    etl::string<16> role;            // logical role
    etl::string<16> module;          // owner module
    etl::string<24> address;         // optional (OneWire ROM, I²C addr)
    // plus driver-specific fields...
};

struct BindingTable {
    etl::vector<BindingEntry, 32> bindings;
};
```

All ETL strings — no heap, deterministic capacity.

## Initialisation timing

`HAL` і `DriverManager` are NOT `BaseModule` subclasses. They're owned
directly by main.cpp і initialised explicitly:

```cpp
// main.cpp (simplified)
static modesp::HAL hal;
static modesp::DriverManager driver_manager;
// ... and modules що use them ...

// In app_main, after ConfigService loaded board/bindings:
hal.init(config_service.board());                       // step 5
driver_manager.init(config_service.bindings(), hal);    // step 6

// Then equipment.bind_drivers(driver_manager) (step 7)
equipment.bind_drivers(driver_manager);

// Then register equipment і other modules with manager;
// equipment's on_init reads driver_manager's discovered drivers і wires
// them into SharedState.
```

This sits between Phase 1 (ConfigService gives us board/bindings) і
Phase 2 (Equipment registered, business modules ticking).

## Equipment Manager bridge

Equipment Manager (modules/equipment) consumes DriverManager's discovered
drivers і exposes them as state keys. From the business module's
perspective:

```
   bindings.json says: role=air_temp uses driver=ds18b20 on ow_1, addr=28:...
            │
            ▼ DriverManager.init
   DS18B20Driver instance created, configured, init'd
            │
            ▼ Equipment.bind_drivers
   Driver registered under role "air_temp"
            │
            ▼ Equipment.on_update (every tick)
   driver->read(value);
   state_set("equipment.air_temp", value);
   state_set("equipment.air_temp_ok", driver->is_healthy());
```

Business modules read `equipment.air_temp` — they never see the
DS18B20Driver class.

## Health і failure modes

Drivers expose `is_healthy()` і `error_count()`. Equipment Manager:

- Reads `is_healthy()` each tick.
- After 3 consecutive failed reads, sets `equipment.<role>_ok = false`.
- Logs ESP_LOGW for each healthy↔unhealthy transition.

DriverManager doesn't deactivate failed drivers — they keep ticking
(retry on next `update`). Most sensor failures are transient (а
DS18B20 momentary CRC error etc.) і recover within а few ticks.

If а driver's `init()` returns false (hardware not detected), Equipment
Manager logs а fatal error AND sets the binding's role keys to defaults
forever. Application can still boot — just без that role's functionality.

## Memory і allocation

Drivers are **heap-allocated** by DriverManager's factory. They live
until the firmware reboots — no per-tick allocation, no destruction.

Heap budget on ESP32: ~65 KB free after framework init. Driver instances
average ~200 bytes each (state, role string, hardware refs). 20 drivers
= 4 KB. Comfortable headroom.

If you ever need а driver із significantly larger state (е.g., calibration
tables, FIFO buffers), document it і pre-allocate у `configure` so
init/runtime don't surprise.

## Memory і size budget

| Item | Cost |
|---|---|
| HAL instance | ~400 bytes (peripheral pointers + GPIO bookkeeping) |
| DriverManager | ~600 bytes (driver pointer arrays + factory table) |
| Per-driver instance | 100-300 bytes typical |
| OneWire bus (із libonewire) | ~150 bytes per bus |
| I²C bus (ESP-IDF driver) | ~200 bytes per bus |

Total HAL overhead із typical KC868-A6 binding (~10 drivers): ~5 KB RAM.

## Common patterns

### Add support for а new sensor

1. Write а driver class implementing `ISensorDriver`. See
   [writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md).
2. Add driver manifest у `drivers/<name>/manifest.json`.
3. Edit `driver_manager.cpp`'s `create_sensor` factory to dispatch the
   new `type` string.
4. Update `bindings.json` для test board із а binding using the new
   driver.
5. Rebuild і flash.

### Inspect what's bound

```bash
curl -u admin:modesp http://192.168.1.85/api/state | grep equipment.
```

`equipment.<role>` keys list every successfully-instantiated binding. If
а role's key is missing, the driver init failed.

### Manual driver reload (Stage 1.5)

Currently bindings are read once at boot. Stage 1.5 plans
`POST /api/bindings/reload` to reinitialise the driver layer without
reboot — useful for swapping sensor types or rewiring у the field.

## Common pitfalls

**GPIO conflicts:** binding declares а role using GPIO 14, але board.json
already maps GPIO 14 to а relay. HAL `init` aborts with а conflict
message. Edit bindings or board.json.

**Address mismatches на OneWire:** binding says address
`28:8C:5E:45:D4:08:44:09`, but the physical sensor has а different ROM.
Driver reports `not_found` after retries; sensor's `_ok` key never goes
true. Use the discovery endpoint to get correct addresses.

**ADC2 used for а sensor:** GPIO 0, 2, 4, 12-15, 25-27 are ADC2 channels
що conflict із WiFi. board.json validator should catch це; if it doesn't,
your sensor reads jitter wildly when WiFi is active.

**Driver factory not updated:** added а new driver type у manifest but
forgot to add the dispatch case у `driver_manager.cpp`. DriverManager
silently skips it; symptoms look like а missing binding (`equipment.<role>`
key doesn't appear).

**Heap allocation у `update()`:** drivers tick at 100 Hz. `new` у
`update` leaks per-tick. Use stack buffers і ETL.

## Next steps

- **[writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md)**
  — author-side walkthrough.
- **[components/modesp_services.md](modesp_services.md)** — ConfigService
  provides BoardConfig і BindingTable.
- **[modules/equipment.md](../modules/equipment.md)** — Equipment Manager
  consumes driver_manager's drivers.
- **[04-hardware/board-config.md](../../04-hardware/board-config.md)** —
  `board.json` schema.
- **[04-hardware/bindings.md](../../04-hardware/bindings.md)** —
  `bindings.json` schema.

## Source

- [`components/modesp_hal/include/modesp/hal/`](../../../../components/modesp_hal/include/modesp/hal/)
  — public headers.
- [`components/modesp_hal/src/`](../../../../components/modesp_hal/src/)
  — HAL і DriverManager implementations.
- [`drivers/`](../../../../drivers/) — driver implementations що use these
  interfaces.
