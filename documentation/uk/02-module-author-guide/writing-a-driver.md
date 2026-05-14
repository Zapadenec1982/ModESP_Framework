# Написання драйвера

> 📖 **In English:** [documentation/en/02-module-author-guide/writing-a-driver.md](../../en/02-module-author-guide/writing-a-driver.md)

Ця сторінка — повне покрокове проходження побудови апаратного драйвера —
C++-класу, що реалізує `ISensorDriver` або `IActuatorDriver` і
з'єднує конкретну периферію (DS18B20, NTC-термістор, GPIO-реле, I2C
розширювач) з HAL-абстракцією фреймворку. Прочитавши, ви зможете додати
підтримку нового сенсора або актуатора, зареєструвати його через
прив'язки, і він автоматично з'явиться у ключах SharedState
`equipment.<role>`.

Драйвери — НЕ бізнес-модулі. Бізнес-логіка читає значення сенсорів і
пише запити до актуаторів через SharedState. Драйвери перетворюють між
SharedState та реальним I/O.

## Що таке драйвер

Драйвер — це вузький адаптер, який:

- Реалізує один з двох інтерфейсів: `ISensorDriver` (лише читання входів)
  або `IActuatorDriver` (керовані виходи).
- Володіє апаратно-залежною конфігурацією: вивід GPIO, адреса на шині,
  калібрувальні константи.
- Має маніфест, що декларує `category` (`sensor`/`actuator`),
  `hardware_type` (`gpio`/`onewire_bus`/`adc`/`i2c`/...), `provides`
  (тип виходу), а також `settings` на кожен екземпляр.
- **Створюється `DriverManager`-ом** із записів `bindings.json` — не
  реєструється вручну в `main.cpp`.
- Живе у `drivers/<name>/`, а не в `modules/`.

Прив'язка з'єднує тип драйвера + фізичну адресу (вивід GPIO, OneWire ROM,
I2C-адреса) з логічною роллю (`air_temp`, `compressor` тощо). Equipment
Manager читає драйвери сенсорів і пише значення у ключі стану
`equipment.<role>`; читає ключі запитів `equipment.req_<role>` і
пересилає їх до драйверів актуаторів.

## Структура папки

```
drivers/your_sensor/
├── manifest.json                   ← ОБОВ'ЯЗКОВО — контракт драйвера
├── CMakeLists.txt                  ← ОБОВ'ЯЗКОВО
├── include/
│   └── your_sensor_driver.h        ← Декларація класу драйвера
└── src/
    └── your_sensor_driver.cpp      ← Реалізація
```

## Крок 1 — Написати маніфест

Маніфест драйвера відрізняється від маніфесту модуля. Поля верхнього
рівня специфічні для драйвера. Повний довідник у
[manifest.md](manifest.md#driver-only-sections).

Мінімальний приклад сенсора (`drivers/my_sensor/manifest.json`):

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

Мінімальний приклад актуатора:

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
      "description": "Мінімальний інтервал між перемиканнями (захист компресора)",
      "persist": true
    }
  ]
}
```

## Крок 2 — Реалізувати інтерфейс

### Драйвер сенсора — `ISensorDriver`

```cpp
// drivers/my_sensor/include/my_sensor_driver.h
#pragma once
#include "modesp/hal/driver_interfaces.h"
#include "etl/string.h"
#include "driver/gpio.h"

class MySensorDriver : public modesp::ISensorDriver {
public:
    MySensorDriver() = default;

    /// Викликається DriverManager-ом перед init().
    void configure(const char* role, gpio_num_t adc_pin, float offset);

    // ── Інтерфейс ISensorDriver ──
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

Джерело:

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
    // Налаштувати ADC, виділити канал тощо.
    healthy_ = true;
    ESP_LOGI(TAG, "Initialized %s on ADC GPIO%d", role_.c_str(), (int)adc_pin_);
    return true;
}

void MySensorDriver::update(uint32_t dt_ms) {
    // Періодично семпл-овуємо ADC. Патерн з акумулятором і інтервалом.
    static uint32_t elapsed = 0;
    elapsed += dt_ms;
    if (elapsed < 1000) return;  // settings.read_interval_ms обмежував би це
    elapsed = 0;

    int raw = 0;
    // ... читання ADC ...
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

### Драйвер актуатора — `IActuatorDriver`

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

Джерело (ключові методи):

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
    // Насичувальний акумулятор для блокування min_switch_ms.
    if (UINT32_MAX - dt_ms < since_switch_ms_) {
        since_switch_ms_ = UINT32_MAX;
    } else {
        since_switch_ms_ += dt_ms;
    }
}

bool MyRelayDriver::set(bool state) {
    if (state == on_) return true;  // no-op
    if (min_switch_ms_ > 0 && since_switch_ms_ < min_switch_ms_) {
        return false;  // заблоковано (захист компресора)
    }
    gpio_set_level(gpio_, state ? 1 : 0);
    on_ = state;
    since_switch_ms_ = 0;
    cycles_++;
    return true;
}
```

## Крок 3 — Зареєструвати у DriverManager

`components/modesp_hal/src/driver_manager.cpp` має фабричний диспетч на
основі рядка `type` з `bindings.json`. Додайте випадок для вашого
драйвера:

```cpp
// driver_manager.cpp
#include "my_sensor_driver.h"  // ← додати include

// Всередині switch у create_sensor()...
if (strcmp(type, "my_sensor") == 0) {
    auto* drv = new MySensorDriver();
    drv->configure(role, parse_gpio(binding["pin"]), parse_float(binding["offset"]));
    return drv;
}
```

Для актуаторів — аналогічна фабрика `create_actuator()`.

> ⚠️ **Примітка:** цей крок ручного редагування — єдина частина
> написання драйвера, що поки що не керується маніфестом. Stage 1.5
> планує механізм авто-реєстрації драйверів. Слідкуйте за
> [tools/generate_ui.md](../05-tools/generate_ui.md) *(заплановано)*
> для статусу.

## Крок 4 — CMakeLists.txt

```cmake
# drivers/my_sensor/CMakeLists.txt
idf_component_register(
    SRCS "src/my_sensor_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_hal modesp_core
)
```

Драйвери залежать від `modesp_hal` (заголовок інтерфейсу) і
`modesp_core` (логування, типи). Драйвери сенсорів, що використовують
ADC, також потребують `esp_adc`; драйвери I2C потребують `driver`;
тощо.

## Крок 5 — Прив'язати у `bindings.json`

Користувач (той, хто розгортає) з'єднує драйвер з фізичним обладнанням
у `bindings.json` плати:

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

`DriverManager::init()` ітерує прив'язки, інстанціює клас драйвера для
кожного рядка `type`, викликає його `configure()` зі значеннями
прив'язки, потім `init()`.

Ключі стану, що з'являються автоматично:
- `equipment.air_temp` (float) — читається бізнес-модулями.
- `equipment.compressor` (bool) — поточний стан актуатора.
- `equipment.req_compressor` (bool) — ключ запиту, який пишуть
  бізнес-модулі.

## Крок 6 — Збірка, прошивка, перевірка

```bash
idf.py build
idf.py -p COM15 flash monitor
```

Очікуваний журнал:

```
I (12345) DriverManager: Created sensor 'air_temp' (type=my_sensor, gpio=34)
I (12346) DriverManager: Created actuator 'compressor' (type=my_relay, gpio=13)
I (12350) MySensor: Initialized air_temp on ADC GPIO34
I (12352) MyRelay: Initialized compressor on GPIO13
```

У WebUI / через `/api/state`:
- `equipment.air_temp` оновлюється показаннями сенсора кожні
  `read_interval_ms`.
- `equipment.req_compressor` можна перемикати (вашим бізнес-модулем або
  через HTTP `POST /api/settings`).
- `equipment.compressor` віддзеркалює фактичний стан актуатора.

## Патерни сенсорів

**Періодичне семплування:**

```cpp
void update(uint32_t dt_ms) override {
    elapsed_ms_ += dt_ms;
    if (elapsed_ms_ < read_interval_ms_) return;
    elapsed_ms_ = 0;
    // ... виконати читання ...
}
```

**Підрахунок помилок / здоров'я:**

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

**Калібрування / зсув:**

Застосовуйте під час читання, а не під час збереження — тоді зміна
налаштування `offset` відображається в наступному читанні без
перезавантаження:

```cpp
bool read(float& value) override {
    if (!has_reading_) return false;
    value = raw_value_ + offset_;  // зсув застосовується тут
    return true;
}
```

## Патерни актуаторів

**Мінімальний інтервал перемикання (захист компресора):**

Відхиляйте виклики `set()`, що приходять занадто швидко. Компресори та
великі насоси потребують ~60 с між перемиканнями, щоб уникнути
механічних пошкоджень / міграції холодоагенту.

**Аварійна зупинка:**

Стандартна реалізація викликає `set(false)`. Перевизначте, якщо ваше
обладнання потребує іншого безпечного стану (наприклад, клапан у
положення «повністю відкритий» замість «закритий»).

```cpp
void emergency_stop() override {
    set_value(1.0f);  // повністю відкрити клапан при аварійній зупинці
}
```

**Підтримка аналогу:**

Якщо ваш актуатор підтримує PWM / змінний вихід, перевизначте
`set_value` / `get_value` / `supports_analog`:

```cpp
bool set_value(float value_0_1) override {
    uint8_t duty = static_cast<uint8_t>(value_0_1 * 255.0f);
    pwm_set_duty(channel_, duty);
    return true;
}
bool supports_analog() const override { return true; }
```

## Типові помилки

**Забутий виклик `configure()`:** якщо ви пропустите фабричне зв'язування
у DriverManager, ваш драйвер компілюється, але посилання з
`bindings.json` ніколи не створюють екземпляр. Симптом: ключ
`equipment.<role>` ніколи не з'являється у SharedState.

**Heap-алокація в update():** драйвери тікають з частотою 100 Hz. `new`
/ `std::string` на гарячому шляху втрачає байти на такт. Використовуйте
лише стекові масиви та типи ETL.

**Блокувальний I/O у update():** транзакції I2C-шини можуть зависати
~10 мс; OneWire може займати 750 мс (перетворення DS18B20). Якщо ваш
протокол повільний, або:
- Реалізуйте машину станів через кілька викликів `update()` (почати
  семплування, опитати готовність, прочитати результат).
- Використайте асинхронні API ESP-IDF (I2C-драйвер на ISR, шина OneWire,
  що опитується окремо).

**Забутий стан healthy при невдачі ініціалізації:** бізнес-модулі
перевіряють `is_healthy()`, щоб вирішити, чи переходити до безпечних
значень за замовчуванням. Поверніть `false` з `init()` І тримайте
`healthy_ = false`, якщо проба обладнання не вдалася.

**Нехтування `min_switch_ms`:** компресори та насоси механічно
ламаються, якщо їх перемикати занадто швидко. Завжди реалізуйте
блокування у драйверах актуаторів, навіть якщо не всі прив'язки
налаштовують це.

## Що далі

- **[components/modesp_hal.md](../03-framework-reference/components/modesp_hal.md)**
  *(заплановано)* — повний довідник HAL і DriverManager.
- **[hardware/bindings.md](../04-hardware/bindings.md)** *(заплановано)* —
  схема та приклади `bindings.json`.
- **[hardware/board-config.md](../04-hardware/board-config.md)**
  *(заплановано)* — `board.json` (можливості обладнання) і як драйвери
  на нього накладаються.
- **[modules/equipment.md](../03-framework-reference/modules/equipment.md)**
  *(заплановано)* — Equipment Manager — модуль, що володіє драйверами
  і експонує ключі стану `equipment.*`.

## Наявні драйвери для вивчення з джерельного коду

- [`drivers/relay/`](../../../drivers/relay/) — найпростіший актуатор
  (GPIO + мінімальний інтервал перемикання). Найкраще перше читання.
- [`drivers/digital_input/`](../../../drivers/digital_input/) —
  найпростіший сенсор (GPIO-контакт, антидребезг).
- [`drivers/ntc/`](../../../drivers/ntc/) — сенсор на основі ADC з
  калібрувальною таблицею.
- [`drivers/ds18b20/`](../../../drivers/ds18b20/) — OneWire-сенсор з
  асинхронним перетворенням, API виявлення.
- [`drivers/pcf8574_relay/`](../../../drivers/pcf8574_relay/) — актуатор
  на I2C-розширювачі (ділить шину з сусідами).
