# Драйвери — використання та створення

> 📖 **In English:** [documentation/en/02-module-author-guide/writing-a-driver.md](../../en/02-module-author-guide/writing-a-driver.md)

Драйвер — це вузький адаптер між одним периферійним пристроєм (DS18B20, NTC,
GPIO-реле, I2C-розширювач) і HAL фреймворку. Він реалізує `ISensorDriver`
(вхід тільки на читання) або `IActuatorDriver` (керований вихід) — і нічого
більше. Бізнес-логіка ніколи не торкається GPIO: вона читає значення сенсорів
і пише запити на актуатори через SharedState, а драйвер конвертує між
SharedState і реальним I/O.

Сторінка має дві частини:

1. **[Використання існуючих драйверів](#використання-існуючих-драйверів)** —
   прив'язати готовий драйвер до заліза й вмикати/вимикати його в menuconfig.
2. **[Створення нового драйвера](#створення-нового-драйвера)** — додати
   підтримку нового сенсора чи актуатора. Фреймворк підключає його
   автоматично (registry + згенерований menuconfig-toggle); `driver_manager.cpp`
   редагувати НЕ потрібно.

Готові драйвери: `ds18b20`, `ntc`, `digital_input`, `relay`, `pcf8574_relay`,
`pcf8574_input` (`drivers/*/`).

---

## Використання існуючих драйверів

### 1. Опиши залізо в `board.json`

`boards/<board>/board.json` перелічує фізичні ресурси плати — GPIO-виходи,
OneWire-шини, ADC-канали, I2C-розширювачі — кожен зі своїм `id`. Це задача
автора плати; драйвери посилаються на ці `id`, а не на сирі піни. Див.
[04-hardware/board-config.md](../04-hardware/board-config.md).

### 2. Прив'яжи драйвер → роль у `bindings.json`

`boards/<board>/bindings.json` мапить апаратний `id` на тип драйвера і логічну
`role`:

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

| Поле       | Значення |
|------------|----------|
| `hardware` | `id` із `board.json` (фізичний ресурс). |
| `driver`   | Поле `driver` з маніфесту драйвера (його тип-рядок). |
| `role`     | Логічна назва ролі, яку оголосив модуль-власник (з `capability`, не з драйвером — R0.1). Стає ключем SharedState `equipment.<role>`. Binding валідний лише коли `capability` драйвера збігається зі здатністю ролі (R3.1). |
| `module`   | Модуль-власник (зазвичай `equipment`). |
| `address`  | Опційно. ROM-адреса для мультипристроєвих шин (кілька DS18B20 на одному OneWire-піні). Для не-дротових драйверів ідентичність (BLE MAC тощо) живе на рядку пристрою, НЕ тут (R0.3). Пропусти для одно-пристроєвих шин. |

`DriverManager::init()` проходить bindings, питає в реєстру фабрику за рядком
`driver`, і фабрика будує інстанс із HAL-ресурсу, названого в `hardware`.
Ключі стану з'являються автоматично:

- `equipment.air_temp` (float) — значення сенсора, оновлюється щоінтервал.
- `equipment.compressor` (bool) — фактичний стан актуатора.
- `equipment.air_temp_ok` (bool) — здоров'я сенсора.

### 3. Вмикання/вимикання в menuconfig

Кожен драйвер **опційний**. `idf.py menuconfig` → **ModESP Drivers**:

```
[*] ds18b20 driver
[*] ntc driver
[ ] pcf8574_input driver     ← вимкнено: не компілюється, менший бінарник
...
```

Вимкнений драйвер не компілюється взагалі (менший flash). Якщо bindings активної
плати **використовують** драйвер, який ти вимкнув, **білд падає** з чіткою
помилкою (`components/modesp_hal/CMakeLists.txt`) — мертва прив'язка не потрапить
у прошивку тихо. Узгодити menuconfig із платою автоматично:

```bash
python tools/drivers_sync.py --fix      # увімкнути драйвери, які плата прив'язує
python tools/drivers_sync.py --prune    # вимкнути драйвери, яких плата не використовує
python tools/drivers_sync.py --dry-run  # лише попередній перегляд
```

Тобто: вимкни драйвери, яких плата не використовує, щоб зменшити образ (або
`--prune`); або лиши дефолти (всі увімкнені), якщо це неважливо. Toggle для
кожного драйвера генерується автоматично з `drivers/*/manifest.json` — див. нижче.

---

## Створення нового драйвера

Додавання драйвера **не потребує змін у `driver_manager.cpp`**. Ти пишеш
драйвер, реєструєш його фабрику одним макросом і використовуєш CMake-хелпер;
збірка генерує menuconfig-toggle, список залежностей і виклик реєстрації за тебе.

### Структура теки

```
drivers/my_sensor/
├── manifest.json                  ← ОБОВ'ЯЗКОВО — контракт драйвера
├── CMakeLists.txt                 ← ОБОВ'ЯЗКОВО — через modesp_driver_component()
├── include/
│   └── my_sensor_driver.h         ← клас : ISensorDriver / IActuatorDriver
└── src/
    └── my_sensor_driver.cpp       ← реалізація + фабрика + реєстрація
```

Назва теки, поле `driver` в маніфесті та ім'я в макросі реєстрації **мусять
збігатися** (`my_sensor`), нижній_снейк-кейс (`^[a-z][a-z0-9_]*$`).

### Крок 1 — маніфест

`drivers/my_sensor/manifest.json` (приклад сенсора):

```json
{
  "manifest_version": 1,
  "driver": "my_sensor",
  "description": "Demo analog sensor",
  "category": "sensor",
  "hardware_type": "adc",
  "transport": "wired",
  "requires_address": false,
  "multiple_per_bus": false,
  "provides": {"capability": "temperature", "type": "float", "unit": "°C", "range": [-40, 150]},
  "settings": [
    {"key": "read_interval_ms", "type": "int",   "default": 1000, "min": 100, "max": 60000, "unit": "мс", "persist": true},
    {"key": "offset",           "type": "float", "default": 0.0,  "min": -10.0, "max": 10.0, "step": 0.1, "unit": "°C", "persist": true}
  ]
}
```

`category` (`sensor`/`actuator`) і назва `driver` — усе, що потрібно
**генератору** (ім'я C++-класу йому не потрібне). Повний опис полів:
[manifest.md](manifest.md#driver-only-sections).

**Драйвер оголошує CAPABILITY, а не тип-до-типу** (R0.1, R3.1). Роль
модуля просить *здатність* (`temperature`), а не твій драйвер; генератор
зводить роль↔канал **лише за рівністю `capability`** (+ напрям in/out) — ніколи
за назвою драйвера, `hardware_type` чи транспортом. Тому `provides` мусить
назвати здатність:

- **Одноканальний драйвер** — `provides.capability` (один рядок ∈
  [`tools/capabilities.json`](../../../tools/capabilities.json), напр.
  `"temperature"`, `"relay_out"`). Поля `type`/`unit`/`range` лишаються
  метаданими значення.
- **Багатоканальний драйвер** — не став `provides.capability`; замість цього
  `provides.channels: [{"channel": ..., "capability": ...}]` (рекомендовано) або
  per-адресні `address_channels[].capability` для пристроїв, де `binding.address`
  обирає величину (один device живить кілька ролей). Приклади — BLE-обзервери
  `ble_xiaomi_th` (temperature/humidity/battery) і `ble_nrf_tilt`
  (angle/tilted/battery/осі accel).

**`transport`** — окреме top-level поле (`wired`/`ble`/`lora`/`mqtt`/`espnow`,
R4.1). Дефолт виводиться з `hardware_type`, тож для дротового драйвера його
можна опустити; став явно для не-дротового (напр. `"ble"`). Воно ортогональне
шині — LoRa/MQTT/ESP-NOW не перевантажують `hardware_type`. Невідома capability
ламає білд (R8.3).

### Крок 2 — реалізуй інтерфейс

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

Самі інтерфейси (`modesp::ISensorDriver` / `IActuatorDriver`) — у
[`driver_interfaces.h`](../../../components/modesp_hal/include/modesp/hal/driver_interfaces.h):

| `ISensorDriver` | `IActuatorDriver` |
|-----------------|-------------------|
| `init()`, `update(dt_ms)` | `init()`, `update(dt_ms)` |
| `read(float&) → bool` | `set(bool) → bool`, `get_state() → bool` |
| `is_healthy()`, `role()`, `type()`, `error_count()` | `set_value(0..1)`, `supports_analog()` (аналог, опційно) |
| | `role()`, `type()`, `is_healthy()`, `emergency_stop()`, `switch_count()` |

`configure()` — твій власний метод; сигнатура — яка треба твоїй фабриці
(Крок 3). Патерни реалізації (періодичне семплювання, здоров'я, offset,
блокування перемикань, аналог) — у [Патерни](#патерни) нижче.

### Крок 3 — фабрика + реєстрація (єдина точка підключення)

У кінці `.cpp` додай **фабрику**, що перетворює `Binding` + HAL на
сконфігурований інстанс, і зареєструй її одним макросом. Це єдине місце, де
драйвер вмикається у фреймворк — `driver_manager.cpp` лишається недоторканим.

```cpp
// drivers/my_sensor/src/my_sensor_driver.cpp  (кінець файлу)
#include "modesp/hal/driver_registry.h"
#include "modesp/hal/hal.h"
#include "etl/string_view.h"

namespace {
// Статичний пул (zero-heap) — по слоту на кожен можливий інстанс на цій платі.
MySensorDriver s_pool[modesp::MAX_SENSORS];
size_t         s_n = 0;

modesp::ISensorDriver* my_sensor_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_n >= modesp::MAX_SENSORS) { ESP_LOGE(TAG, "pool exhausted"); return nullptr; }

    // Знайти фізичний ресурс за binding.hardware_id через HAL.
    auto* adc = hal.find_adc_channel(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!adc) { ESP_LOGE(TAG, "ADC '%s' not found", b.hardware_id.c_str()); return nullptr; }

    auto& drv = s_pool[s_n++];
    drv.configure(b.role.c_str(), adc->gpio, /*offset=*/0.0f);
    return &drv;
}
} // namespace

MODESP_REGISTER_SENSOR(my_sensor, &my_sensor_factory)   // ім'я == поле "driver" з маніфесту
```

Для актуатора: повертай `modesp::IActuatorDriver*` і використай
`MODESP_REGISTER_ACTUATOR(my_relay, &my_relay_factory)`.

**Фіндери HAL-ресурсів** (обери за своїм `hardware_type`):

| Фіндер | Повертає (поля, які ти використовуєш) |
|--------|---------------------------------------|
| `find_onewire_bus(id)`     | `->gpio` |
| `find_gpio_output(id)`     | `->gpio`, `->active_high` |
| `find_gpio_input(id)`      | `->gpio`, `->pull_up` |
| `find_adc_channel(id)`     | `->gpio`, `->atten` |
| `find_expander_output(id)` | `->expander_id`, `->pin`, `->active_high` |
| `find_expander_input(id)`  | `->expander_id`, `->invert`, `->pin` |
| `find_i2c_expander(id)`    | спільний `I2CExpanderResource` (для read/write) |

Фабрика повертає `nullptr` при будь-якій невдачі (пул повний, ресурс відсутній) —
тоді binding пропускається з попередженням, решта продовжують. Ліміти пулів
`MAX_SENSORS` / `MAX_ACTUATORS` — у
[`hal_types.h`](../../../components/modesp_hal/include/modesp/hal/hal_types.h).

> Чому фабрика, а не просто клас, як у модулів? Модулі — це
> default-сконструйовані синглтони; драйвери **мультиінстансні й прив'язані до
> HAL-ресурсу** (8 сенсорів DS18B20, кожен на своїй ROM-адресі). Фабрика — це та
> невелика частина типозалежного wiring, яку генератор не може вивести з маніфесту.

### Крок 4 — CMakeLists.txt

Через хелпер — він робить драйвер опційним автоматично:

```cmake
# drivers/my_sensor/CMakeLists.txt
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/my_sensor_driver.cpp"
    PRIV_REQUIRES esp_adc          # додаткові залежності: "driver" для GPIO, "esp_adc" для ADC, тощо
)
```

`modesp_driver_component()` сам додає `REQUIRES modesp_hal` і компілює `SRCS`
лише коли встановлено `CONFIG_MODESP_DRIVER_MY_SENSOR`. **Не** викликай
`idf_component_register` напряму і **не** хардкодь `${CMAKE_SOURCE_DIR}` (під
час requirements-expansion він вказує на `build/` — хелпер бере правильний шлях).

### Крок 5 — збірка

```bash
idf.py build
```

Генератор (`tools/generate_ui.py`) сканує `drivers/*/manifest.json` і пише в
`generated/` та `components/modesp_hal/Kconfig` (усе DO-NOT-EDIT):

- toggle `MODESP_DRIVER_MY_SENSOR` (default `y`) під **ModESP Drivers**;
- список залежностей `modesp_hal`;
- guarded-виклик `modesp_register_all_drivers()`.

Драйвер тепер опційний у `idf.py menuconfig` без жодної додаткової роботи. Далі
прив'яжи його ([Використання існуючих драйверів](#використання-існуючих-драйверів))
і прошивай.

> **Перша збірка:** новий драйвер додає *новий* component-Kconfig файл. Якщо
> його toggle не з'явився в menuconfig — раз виконай `idf.py fullclean`: ESP-IDF
> кешує список component-Kconfig'ів і пересканує лише при чистій реконфігурації.

---

## Патерни

**Періодичне семплювання** (драйвери тікають на 100 Гц — гейтуй повільну роботу):

```cpp
void update(uint32_t dt_ms) override {
    elapsed_ms_ += dt_ms;
    if (elapsed_ms_ < read_interval_ms_) return;
    elapsed_ms_ = 0;
    // ... читання ...
}
```

**Насичувальний лічильник помилок / здоров'я** (нездоровий сенсор має
*лишатися* нездоровим — `uint8_t` обертається 255→0 і хибно «одужає»):

```cpp
if (read_failed) {
    if (errors_ < MAX_CONSECUTIVE_ERRORS) errors_++;   // насичення, без обертання
} else {
    errors_ = 0;                                        // одужав
}
// is_healthy() => errors_ < MAX_CONSECUTIVE_ERRORS
```

**Калібрування / offset при читанні** (щоб зміна налаштування діяла без
перезавантаження):

```cpp
bool read(float& value) override {
    if (!has_reading_) return false;
    value = raw_value_ + offset_;   // застосовується тут, не при збереженні
    return true;
}
```

**Мінімальний інтервал перемикання (захист компресора)** — відхиляй надто
часті `set()`; компресорам/насосам треба ~60–180 с між перемиканнями:

```cpp
bool set(bool state) override {
    if (state == on_) return true;
    if (min_switch_ms_ > 0 && since_switch_ms_ < min_switch_ms_) return false;
    apply_gpio(state); on_ = state; since_switch_ms_ = 0; if (state) cycles_++;
    return true;
}
```

**Спільний стан шини (I2C-розширювач)** — коли кілька реле ділять один
вихідний байт PCF8574, зроби snapshot перед записом і **відкоти при невдачі**,
інакше залиплий біт протече в запис наступного реле:

```cpp
uint8_t saved = expander_->output_state;
expander_->output_state |= (1 << pin_);
if (!expander_->write_state()) { expander_->output_state = saved; return false; }
```

**Аналоговий актуатор** — перевизнач `set_value`/`supports_analog` для PWM /
змінного виходу; дефолт мапить `set_value(>0.5)` на `set(true)`.

---

## Типові помилки

- **Heap в `update()`** — драйвери тікають на 100 Гц; `new`/`std::string`/`std::vector`
  у hot-path течуть на кожному тіку. Тільки статичні пули та ETL-типи.
- **Блокуючий I/O в `update()`** — I2C-транзакція може стояти ~10 мс; конверсія
  DS18B20 — 750 мс. Розбивай повільні протоколи на кілька викликів `update()`
  (start → poll-ready → read); ніколи не роби `vTaskDelay` у retry-циклі на
  спільній 100 Гц-задачі — це стопить усі інші драйвери й модулі.
- **Розбіжність назв** — тека, поле `driver` в маніфесті та ім'я в макросі
  `MODESP_REGISTER_*` мусять бути ідентичні. Інакше згенерований символ
  `modesp_register_driver_<name>()` не злінкується.
- **Забути безпечний стан при init** — повертай `false` з `init()` (і лиши
  `healthy_ = false`), якщо проба заліза провалилась; драйвери стартують у OFF.
- **Прямий виклик `idf_component_register`** — ламає опційність; використовуй
  `modesp_driver_component()`.

---

## Існуючі драйвери для вивчення, від найпростішого

- [`drivers/relay/`](../../../drivers/relay/) — найпростіший актуатор (GPIO + блокування перемикань).
- [`drivers/digital_input/`](../../../drivers/digital_input/) — найпростіший сенсор (GPIO-контакт + дебаунс).
- [`drivers/ntc/`](../../../drivers/ntc/) — ADC-сенсор з B-параметром і мапінгом атенюації.
- [`drivers/ds18b20/`](../../../drivers/ds18b20/) — OneWire-сенсор, MATCH_ROM/SKIP_ROM, асинхронна конверсія, scan API.
- [`drivers/pcf8574_relay/`](../../../drivers/pcf8574_relay/) — актуатор на I2C-розширювачі, що ділить шину із сусідами.

Кожен закінчується блоком `factory + MODESP_REGISTER_*` — скопіюй цю форму.

## Наступні кроки

- [manifest.md](manifest.md#driver-only-sections) — опис полів маніфесту драйвера.
- [04-hardware/bindings.md](../04-hardware/bindings.md) — схема `bindings.json`.
- [04-hardware/board-config.md](../04-hardware/board-config.md) — апаратні ресурси `board.json`.
- [03-framework-reference/modules/equipment.md](../03-framework-reference/modules/equipment.md) — Equipment Manager, який володіє драйверами і виставляє `equipment.*`.
