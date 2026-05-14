# `modesp_hal` — Hardware Abstraction Layer і DriverManager

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_hal.md](../../../en/03-framework-reference/components/modesp_hal.md)

`modesp_hal` provides hardware абстракції що decouple business logic від
фізичного I/O. Він owns: HAL сам (GPIO/ADC/I²C/OneWire setup based на
board.json), driver interfaces (`ISensorDriver`, `IActuatorDriver`), і
`DriverManager` що instantiates і drives driver instances з
`bindings.json`.

Business modules не interact із цим layer напряму — вони читають
`equipment.<role>` state keys що Equipment Manager produces. Drivers
implement interfaces; HAL дає їм access до peripherals. Ця сторінка
документує як layer structured і які extension points exist.

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

`init(board)` walks parsed `BoardConfig` (з ConfigService) і:

1. Configures GPIO outputs (relays, LEDs).
2. Configures GPIO inputs з pull-up/down.
3. Sets up OneWire buses (GPIO mode, pull-up, bus driver).
4. Initialises I²C bus controllers (clock, pins).
5. Probes I²C expanders (PCF8574 тощо) і registers їх.
6. Configures ADC units і channels з calibrated attenuation.

Drivers не call ESP-IDF напряму — вони access ці peripherals через HAL
accessors. Це centralises pin / clock / power-domain management і lets
HAL handle conflicts (наприклад, GPIO 25 не може бути одночасно relay
і ADC).

State keys (debugging):

| Key | Notes |
|---|---|
| `hal.initialised` | true після успішного `init()`. |
| `hal.onewire_count` | Number OneWire buses active. |
| `hal.i2c_count` | Number I²C buses active. |
| `hal.adc_channels` | Number ADC channels configured. |

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

Two-flavor підхід бо sensors і actuators мають fundamentally different
shapes — sensor — це "read value", actuator — це "command state із
safety і feedback".

Authors implement це щоб add new hardware support. Див.
[writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md)
для author-side walkthrough.

## `DriverManager` — driver factory і lifecycle

```cpp
#include "modesp/hal/driver_manager.h"

class DriverManager {
public:
    DriverManager();

    // Read bindings.json, instantiate drivers, call init() на кожному.
    bool init(const BindingTable& bindings, HAL& hal);

    // Drive update() на всіх drivers кожен tick.
    void update_all(uint32_t dt_ms);

    // Accessors для Equipment Manager.
    ISensorDriver* find_sensor(const char* role);
    IActuatorDriver* find_actuator(const char* role);

    size_t sensor_count() const;
    size_t actuator_count() const;

    void for_each_sensor(std::function<void(ISensorDriver&)> fn);
    void for_each_actuator(std::function<void(IActuatorDriver&)> fn);
};
```

Workflow:

1. Equipment Manager calls `driver_manager.init(bindings, hal)` у власному
   `on_init`.
2. `init` iterates `bindings.bindings` array. Для кожного entry:
   - Look up `type` string (наприклад, "ds18b20") у factory table.
   - Call factory: `new MySensorDriver()`.
   - Call `driver->configure(role, hardware_params...)` із values з
     binding і board.json.
   - Call `driver->init()`. При failure, log і continue (інші drivers
     should still try).
3. Після init, Equipment Manager queries `find_sensor`/`find_actuator`
   by role і wires їх у `equipment.*` SharedState keys.
4. Кожен tick, Equipment Manager calls `driver_manager.update_all(dt_ms)`,
   що fans out до `update(dt_ms)` кожного driver.

## Factory dispatch (current Stage 1)

Factory table — hand-coded у `driver_manager.cpp`:

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

Додавання driver вимагає edit цей dispatch — одне місце де
manifest-driven generation ще не reach. Stage 1.5 plans
auto-registration через compile-time-generated factory table з driver
маніфестів. Track [tools/generate_ui.md](../../05-tools/generate_ui.md)
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

Усі ETL strings — no heap, deterministic capacity.

## Initialisation timing

`HAL` і `DriverManager` НЕ `BaseModule` subclasses. Вони owned напряму
main.cpp і initialised explicitly:

```cpp
// main.cpp (simplified)
static modesp::HAL hal;
static modesp::DriverManager driver_manager;
// ... і modules що use them ...

// У app_main, після ConfigService loaded board/bindings:
hal.init(config_service.board());                       // step 5
driver_manager.init(config_service.bindings(), hal);    // step 6

// Потім equipment.bind_drivers(driver_manager) (step 7)
equipment.bind_drivers(driver_manager);

// Потім register equipment і інші modules з manager;
// equipment's on_init reads driver_manager's discovered drivers і wires
// їх у SharedState.
```

Це sits між Phase 1 (ConfigService gives us board/bindings) і Phase 2
(Equipment registered, business modules ticking).

## Equipment Manager bridge

Equipment Manager (modules/equipment) consumes DriverManager's discovered
drivers і exposes їх як state keys. З business module's perspective:

```
   bindings.json says: role=air_temp uses driver=ds18b20 on ow_1, addr=28:...
            │
            ▼ DriverManager.init
   DS18B20Driver instance created, configured, init'd
            │
            ▼ Equipment.bind_drivers
   Driver registered під role "air_temp"
            │
            ▼ Equipment.on_update (кожен tick)
   driver->read(value);
   state_set("equipment.air_temp", value);
   state_set("equipment.air_temp_ok", driver->is_healthy());
```

Business modules читають `equipment.air_temp` — вони ніколи не see
DS18B20Driver class.

## Health і failure modes

Drivers expose `is_healthy()` і `error_count()`. Equipment Manager:

- Reads `is_healthy()` кожен tick.
- Після 3 consecutive failed reads, sets `equipment.<role>_ok = false`.
- Logs ESP_LOGW для кожного healthy↔unhealthy transition.

DriverManager не deactivate failed drivers — вони keep ticking (retry на
next `update`). Більшість sensor failures transient (DS18B20 momentary
CRC error тощо) і recover у межах few ticks.

Якщо driver's `init()` returns false (hardware not detected), Equipment
Manager logs fatal error AND sets binding's role keys до defaults
forever. Application може still boot — just без що role's functionality.

## Memory і allocation

Drivers — **heap-allocated** by DriverManager's factory. Вони живуть
до firmware reboots — no per-tick allocation, no destruction.

Heap budget на ESP32: ~65 KB free після framework init. Driver instances
average ~200 bytes each (state, role string, hardware refs). 20 drivers
= 4 KB. Comfortable headroom.

Якщо вам коли-небудь потрібен driver із significantly larger state
(наприклад, calibration tables, FIFO buffers), document це і pre-allocate
у `configure` щоб init/runtime не surprise.

## Memory і size budget

| Item | Cost |
|---|---|
| HAL instance | ~400 bytes (peripheral pointers + GPIO bookkeeping) |
| DriverManager | ~600 bytes (driver pointer arrays + factory table) |
| Per-driver instance | 100-300 bytes typical |
| OneWire bus (з libonewire) | ~150 bytes per bus |
| I²C bus (ESP-IDF driver) | ~200 bytes per bus |

Total HAL overhead із typical KC868-A6 binding (~10 drivers): ~5 KB RAM.

## Common patterns

### Add support для нового sensor

1. Write driver class implementing `ISensorDriver`. Див.
   [writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md).
2. Add driver manifest у `drivers/<name>/manifest.json`.
3. Edit `driver_manager.cpp`'s `create_sensor` factory щоб dispatch новий
   `type` string.
4. Update `bindings.json` для test board з binding using новий driver.
5. Rebuild і flash.

### Inspect що bound

```bash
curl -u admin:modesp http://192.168.1.85/api/state | grep equipment.
```

`equipment.<role>` keys list кожен successfully-instantiated binding.
Якщо role's key відсутній, driver init failed.

### Manual driver reload (Stage 1.5)

Зараз bindings read once при boot. Stage 1.5 plans
`POST /api/bindings/reload` щоб reinitialise driver layer без reboot —
корисно для swapping sensor types або rewiring у field.

## Common pitfalls

**GPIO conflicts:** binding declares role using GPIO 14, але board.json
already maps GPIO 14 до relay. HAL `init` aborts з conflict message.
Edit bindings або board.json.

**Address mismatches на OneWire:** binding says address
`28:8C:5E:45:D4:08:44:09`, але physical sensor has different ROM.
Driver reports `not_found` після retries; sensor's `_ok` key never goes
true. Use discovery endpoint щоб get correct addresses.

**ADC2 used для sensor:** GPIO 0, 2, 4, 12-15, 25-27 — ADC2 channels що
conflict з WiFi. board.json validator should catch це; якщо ні, ваш
sensor reads jitter wildly коли WiFi active.

**Driver factory not updated:** added новий driver type у manifest але
forgot to add dispatch case у `driver_manager.cpp`. DriverManager
silently skips його; симптоми look like missing binding
(`equipment.<role>` key не appears).

**Heap allocation у `update()`:** drivers tick at 100 Hz. `new` у
`update` leaks per-tick. Use stack buffers і ETL.

## Що далі

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
- [`drivers/`](../../../../drivers/) — driver implementations що use ці
  interfaces.
