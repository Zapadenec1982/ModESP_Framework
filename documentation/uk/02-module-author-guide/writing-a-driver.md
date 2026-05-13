# Написання драйвера

> 📖 **In English:** [documentation/en/02-module-author-guide/writing-a-driver.md](../../en/02-module-author-guide/writing-a-driver.md)

Ця сторінка — повний walkthrough побудови hardware driver — C++ класу що
implements `ISensorDriver` або `IActuatorDriver` і bridge-ить специфічну
peripheral (DS18B20, NTC термістор, GPIO реле, I2C IO expander) до HAL
абстракції фреймворку. Прочитавши, ви зможете додати support для нового
сенсора або актуатора, register-ти через bindings, і він з'явиться
автоматично у `equipment.<role>` SharedState keys.

Драйвери — НЕ business модулі. Business логіка читає sensor values і пише
actuator requests через SharedState. Драйвери конвертують між SharedState
і реальним I/O.

## Що таке драйвер

Driver — це вузький adapter що:

- Implements один з двох інтерфейсів: `ISensorDriver` (read-only входи) або
  `IActuatorDriver` (controllable виходи).
- Володіє hardware-specific конфігом: GPIO pin, bus address, calibration
  константи.
- Має manifest що декларує `category` (`sensor`/`actuator`), `hardware_type`
  (`gpio`/`onewire_bus`/`adc`/`i2c`/...), `provides` (output тип), і
  per-instance `settings`.
- **Instantiated by `DriverManager`** з `bindings.json` entries — не
  register-ається manually у `main.cpp`.
- Живе у `drivers/<name>/`, не `modules/`.

Binding tie-ить driver type + фізичну address (GPIO pin, OneWire ROM, I2C
address) до логічної role (`air_temp`, `compressor`, тощо). Equipment Manager
читає sensor drivers і пише values у `equipment.<role>` state keys; читає
request keys `equipment.req_<role>` і forwards їх до actuator drivers.

## Структура папки

```
drivers/your_sensor/
├── manifest.json                   ← ОБОВ'ЯЗКОВО — driver contract
├── CMakeLists.txt                  ← ОБОВ'ЯЗКОВО
├── include/
│   └── your_sensor_driver.h        ← Driver class declaration
└── src/
    └── your_sensor_driver.cpp      ← Implementation
```

## Крок 1 — Написати маніфест

Driver manifest відрізняється від module manifest. Top-level поля —
driver-specific. Повний reference у [manifest.md](manifest.md#driver-only-sections).

Мінімальний sensor приклад (`drivers/my_sensor/manifest.json`):

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

Мінімальний actuator приклад:

```json
{
  "manifest_version": 1,
  "driver": "my_relay",
  "description": "Demo GPIO relay",
  "category": "actuator",
  "hardware_type": "gpio",
  "requires_address": true,
  "multiple_per_bus": false,
  "provides": {"type": "bool", "description": "Стан реле"},

  "settings": [
    {
      "key": "min_switch_ms",
      "type": "int",
      "default": 0,
      "min": 0, "max": 600000,
      "unit": "мс",
      "description": "Min interval між перемиканнями (compressor safety)",
      "persist": true
    }
  ]
}
```

## Крок 2 — Implement інтерфейс

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

    /// Викликається DriverManager перед init().
    void configure(const char* role, gpio_num_t adc_pin, float offset);

    // ── ISensorDriver інтерфейс ──
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
    // Налаштувати ADC, allocate channel, тощо.
    healthy_ = true;
    ESP_LOGI(TAG, "Initialized %s on ADC GPIO%d", role_.c_str(), (int)adc_pin_);
    return true;
}

void MySensorDriver::update(uint32_t dt_ms) {
    // Sample ADC періодично. Pattern із accumulator + interval.
    static uint32_t elapsed = 0;
    elapsed += dt_ms;
    if (elapsed < 1000) return;  // settings.read_interval_ms gate-ило б це
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

Source (ключові методи):

```cpp
bool MyRelayDriver::init() {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio_,
        .mode = GPIO_MODE_OUTPUT,
        // ... тощо.
    };
    gpio_config(&cfg);
    gpio_set_level(gpio_, 0);  // start OFF
    initialized_ = true;
    return true;
}

void MyRelayDriver::update(uint32_t dt_ms) {
    // Saturating accumulator для min_switch_ms lockout.
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

## Крок 3 — Зареєструвати у DriverManager

`components/modesp_hal/src/driver_manager.cpp` має factory dispatch на основі
`type` string з `bindings.json`. Додайте case для вашого драйвера:

```cpp
// driver_manager.cpp
#include "my_sensor_driver.h"  // ← додати include

// Усередині create_sensor() switch...
if (strcmp(type, "my_sensor") == 0) {
    auto* drv = new MySensorDriver();
    drv->configure(role, parse_gpio(binding["pin"]), parse_float(binding["offset"]));
    return drv;
}
```

Для actuators — аналогічний `create_actuator()` factory.

> ⚠️ **Note:** цей hand-edit крок — єдина частина driver authoring що ще не
> manifest-driven. Stage 1.5 планує driver auto-registration mechanism.
> Track [tools/generate_ui.md](../05-tools/generate_ui.md) *(planned)* для
> статусу.

## Крок 4 — CMakeLists.txt

```cmake
# drivers/my_sensor/CMakeLists.txt
idf_component_register(
    SRCS "src/my_sensor_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_hal modesp_core
)
```

Драйвери залежать від `modesp_hal` (interface header) і `modesp_core`
(logging, types). Sensor drivers що використовують ADC also потребують
`esp_adc`; I2C drivers потребують `driver`; тощо.

## Крок 5 — Bind у `bindings.json`

User (deployer) wire-ить driver до фізичного hardware у board's `bindings.json`:

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

`DriverManager::init()` iterates bindings, instantiates driver class для
кожного `type` string, викликає його `configure()` із binding values, потім
`init()`.

Результуючі state keys що з'являються автоматично:
- `equipment.air_temp` (float) — читається business модулями.
- `equipment.compressor` (bool) — current state actuator-а.
- `equipment.req_compressor` (bool) — request key що business модулі пишуть.

## Крок 6 — Build, flash, verify

```bash
idf.py build
idf.py -p COM15 flash monitor
```

Очікуваний log:

```
I (12345) DriverManager: Created sensor 'air_temp' (type=my_sensor, gpio=34)
I (12346) DriverManager: Created actuator 'compressor' (type=my_relay, gpio=13)
I (12350) MySensor: Initialized air_temp on ADC GPIO34
I (12352) MyRelay: Initialized compressor on GPIO13
```

У WebUI / через `/api/state`:
- `equipment.air_temp` оновлюється із sensor readings кожні `read_interval_ms`.
- `equipment.req_compressor` може бути toggle-нутий (вашим business модулем
  або HTTP `POST /api/settings`).
- `equipment.compressor` mirror-ить actuator's actual state.

## Sensor патерни

**Періодичне sampling:**

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

**Калібрація / offset:**

Apply at read time, not store time — так зміна `offset` setting reflect-ить
у next read без перезавантаження:

```cpp
bool read(float& value) override {
    if (!has_reading_) return false;
    value = raw_value_ + offset_;  // offset apply-ється тут
    return true;
}
```

## Actuator патерни

**Minimum switch interval (compressor safety):**

Reject `set()` calls що приходять занадто швидко. Compressors і великі pumps
потребують ~60s між перемиканнями щоб уникнути механічних пошкоджень /
refrigerant migration.

**Emergency stop:**

Default implementation викликає `set(false)`. Override якщо ваш hardware
потребує іншого safe state (наприклад, valve "fully open" замість "closed").

```cpp
void emergency_stop() override {
    set_value(1.0f);  // open valve fully при е-stop
}
```

**Analog support:**

Якщо ваш actuator supports PWM / variable output, override `set_value` /
`get_value` / `supports_analog`:

```cpp
bool set_value(float value_0_1) override {
    uint8_t duty = static_cast<uint8_t>(value_0_1 * 255.0f);
    pwm_set_duty(channel_, duty);
    return true;
}
bool supports_analog() const override { return true; }
```

## Поширені помилки

**Забутий `configure()` call:** якщо пропустите DriverManager factory
wiring, ваш driver компілюється але `bindings.json` references ніколи не
виробляють instance. Симптом: `equipment.<role>` key ніколи не з'являється
у SharedState.

**Heap allocation у update():** drivers tick-ються при 100 Hz. `new` /
`std::string` у hot path leak-ає bytes per tick. Використовуйте stack
arrays і ETL types only.

**Blocking I/O у update():** I2C bus transactions можуть stall ~10 мс;
OneWire може take 750 мс (DS18B20 conversion). Якщо ваш protocol повільний,
або:
- Implement state machine across multiple `update()` calls (start sample,
  poll for ready, read result).
- Use ESP-IDF async APIs (I2C ISR-based driver, OneWire bus що polls
  separately).

**Забутий healthy state при init failure:** business модулі перевіряють
`is_healthy()` щоб вирішити чи fallback to safe defaults. Поверніть `false`
з `init()` AND тримайте `healthy_ = false` якщо hardware probe fails.

**Не respect-ите `min_switch_ms`:** compressors і pumps fail mechanically
якщо switch-аються занадто швидко. Завжди implement lockout у actuator
drivers, навіть якщо не всі bindings configure це.

## Що далі

- **[components/modesp_hal.md](../03-framework-reference/components/modesp_hal.md)**
  *(planned)* — повний HAL і DriverManager reference.
- **[hardware/bindings.md](../04-hardware/bindings.md)** *(planned)* —
  `bindings.json` schema і приклади.
- **[hardware/board-config.md](../04-hardware/board-config.md)** *(planned)*
  — `board.json` (hardware capabilities) і як drivers map на нього.
- **[modules/equipment.md](../03-framework-reference/modules/equipment.md)**
  *(planned)* — Equipment Manager — модуль що володіє drivers і exposes
  `equipment.*` state keys.

## Existing драйвери для вивчення source-first

- [`drivers/relay/`](../../../drivers/relay/) — найпростіший actuator (GPIO
  + min switch interval). Найкраща перша річ для читання.
- [`drivers/digital_input/`](../../../drivers/digital_input/) — найпростіший
  sensor (GPIO contact, debounce).
- [`drivers/ntc/`](../../../drivers/ntc/) — ADC-based sensor з calibration
  table.
- [`drivers/ds18b20/`](../../../drivers/ds18b20/) — OneWire sensor з async
  conversion, discovery API.
- [`drivers/pcf8574_relay/`](../../../drivers/pcf8574_relay/) — I2C-expanded
  actuator (shares шину з siblings).
```
