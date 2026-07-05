# `modesp_hal` — шар апаратної абстракції та DriverManager

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_hal.md](../../../en/03-framework-reference/components/modesp_hal.md)

`modesp_hal` надає апаратні абстракції, що відокремлюють бізнес-логіку
від фізичного вводу-виводу. Він володіє: самим HAL (налаштування
GPIO/ADC/I²C/OneWire на основі board.json), інтерфейсами драйверів
(`ISensorDriver`, `IActuatorDriver`) і `DriverManager`, який створює та
керує екземплярами драйверів з `bindings.json`.

Бізнес-модулі не взаємодіють з цим шаром напряму — вони читають ключі
стану `equipment.<role>`, які створює Equipment Manager. Драйвери
реалізують інтерфейси; HAL дає їм доступ до периферії. Ця сторінка
описує, як структурований шар і які точки розширення доступні.

Роль оголошує **capability** (temperature/relay_out/…), а не драйвер —
модуль ніколи не знає, який драйвер його обслуговує (ds18b20 / NTC /
віддалений BLE-канал / майбутній LoRa). `DriverManager` резолвить драйвери
за роллю; прив'язка роль↔канал відбувається лише за capability (R0.1,
R3.1). Периферія за межами плати (віддалені сенсори/актуатори) описується
транспорт-генерично як `RemoteDeviceConfig`, а ідентичність (MAC/adv-name/
topic) живе на рядку пристрою, ніколи на ролі (R0.3, R4.1).

REQUIRES: `modesp_core`, `modesp_services` (config_service для читання
board.json/bindings.json), периферійні драйвери ESP-IDF (`driver`,
`esp_adc`).

## Розташування компонента

```
components/modesp_hal/include/modesp/hal/
├── hal.h                   ← HAL class — peripheral setup
├── hal_types.h             ← BoardConfig, BindingTable parsed structs
├── driver_interfaces.h     ← ISensorDriver, IActuatorDriver
└── driver_manager.h        ← DriverManager — factory + lifecycle owner
```

## `HAL` — ініціалізація периферії

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

    // Remote-device lookup by hardware id (any transport). Legacy alias
    // find_ble_device() forwards here.
    RemoteDeviceConfig* find_remote_device(etl::string_view id);
};
```

`init(board)` проходить розібрану структуру `BoardConfig` (з
ConfigService) і:

1. Налаштовує GPIO-виходи (реле, світлодіоди).
2. Налаштовує GPIO-входи з підтяжкою вгору/вниз.
3. Готує шини OneWire (режим GPIO, підтяжка, драйвер шини).
4. Ініціалізує контролери шин I²C (частота, виводи).
5. Опитує I²C-розширювачі (PCF8574 та подібні) і реєструє їх.
6. Налаштовує блоки та канали ADC з відкаліброваним ослабленням.

Драйвери не викликають ESP-IDF напряму — вони отримують доступ до цієї
периферії через методи доступу HAL. Це централізує керування виводами,
тактуванням і доменами живлення, та дозволяє HAL обробляти конфлікти
(наприклад, GPIO 25 не може бути одночасно реле та ADC).

Ключі стану (для діагностики):

| Ключ | Примітки |
|---|---|
| `hal.initialised` | true після успішного `init()`. |
| `hal.onewire_count` | Кількість активних шин OneWire. |
| `hal.i2c_count` | Кількість активних шин I²C. |
| `hal.adc_channels` | Кількість налаштованих каналів ADC. |

## Інтерфейси драйверів

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

Підхід з двома різновидами, тому що сенсори й актуатори мають принципово
різні форми: сенсор — це "прочитати значення", актуатор — це "наказати
стан із безпекою та зворотним зв'язком".

Автори реалізують ці інтерфейси, щоб додати підтримку нового обладнання.
Див.
[writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md)
для покрокового проходження з боку автора.

## `DriverManager` — фабрика і життєвий цикл драйверів

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

Робочий процес:

1. Equipment Manager викликає `driver_manager.init(bindings, hal)` у
   власному `on_init`.
2. `init` ітерує масив `bindings.bindings`. Для кожного запису:
   - Шукає рядок `type` (наприклад, "ds18b20") у таблиці фабрик.
   - Викликає фабрику: `new MySensorDriver()`.
   - Викликає `driver->configure(role, hardware_params...)` зі значеннями
     з прив'язки та board.json.
   - Викликає `driver->init()`. При невдачі — записує в журнал і
     продовжує (інші драйвери все одно мають спробувати).
3. Після ініціалізації Equipment Manager опитує `find_sensor`/
   `find_actuator` за роллю і підключає їх до ключів SharedState
   `equipment.*`.
4. Щотакту Equipment Manager викликає
   `driver_manager.update_all(dt_ms)`, який віялом викликає `update(dt_ms)`
   кожного драйвера.

## Диспетчер фабрик (поточна Stage 1)

Таблиця фабрик закодована вручну у `driver_manager.cpp`:

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

Додавання драйвера вимагає редагування цього диспетчера — це єдине місце,
куди ще не сягнула генерація з маніфестів. Stage 1.5 планує
автоматичну реєстрацію через таблицю фабрик, згенеровану на етапі
складання з маніфестів драйверів. Слідкуйте за
[tools/generate_ui.md](../../05-tools/generate_ui.md) *(планується)*.

## Структури BoardConfig і BindingTable

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

// Remote device (off-board sensor/actuator reached over a transport).
// Config-only: HAL зберігає transport-тег + непрозорий identity-блоб;
// декод/кеш живе у транспортному компоненті (напр. modesp_ble). R4.1.
struct RemoteDeviceConfig {
    HalId           id;                 // "ble_xiaomi_bthome" (hardware_id для bindings)
    etl::string<8>  transport = "ble";  // "ble" сьогодні; "lora"/"mqtt"/"espnow" далі
    etl::string<40> identity;           // transport identity blob (BLE MAC); empty for connect
    etl::string<24> name;               // connect-пристрій (panel) adv-name prefix; empty for observers
};
using BleDeviceConfig = RemoteDeviceConfig;   // legacy alias (transitional)

struct BoardConfig {
    etl::string<24> board_name;
    etl::string<8>  board_version;
    etl::vector<GpioOutputConfig, MAX_RELAYS>        gpio_outputs;
    etl::vector<OneWireBusConfig, MAX_ONEWIRE_BUSES> onewire_buses;
    etl::vector<GpioInputConfig, MAX_ADC_CHANNELS>   gpio_inputs;
    etl::vector<AdcChannelConfig, MAX_ADC_CHANNELS>  adc_channels;
    // ... encoders, i2c_buses/expanders, expander I/O, displays, uart, i2s ...
    etl::vector<RemoteDeviceConfig, MAX_REMOTE_DEVICES> remote_devices;
};

// One per-binding numeric driver setting (e.g. "beta" → 3900).
struct BindingSetting {
    etl::string<16> key;
    float           value = 0.0f;
};

struct Binding {
    HalId         hardware_id;   // matches board.json's id (or remote device id)
    Role          role;          // logical role (capability owner)
    DriverType    driver_type;   // driver that services this role
    ModuleName    module_name;   // owner module (routing) — R3.4
    SensorAddress address;       // optional (OneWire ROM, I²C addr, channel)
    etl::vector<BindingSetting, MAX_BINDING_SETTINGS> settings;

    float setting_or(const char* key, float def) const; // per-binding config lookup
};

struct BindingTable {
    etl::vector<Binding, MAX_BINDINGS> bindings;
};
```

Усі рядки ETL — без купи, з детермінованою місткістю. Ліміти —
`MAX_BINDINGS=24`, `MAX_REMOTE_DEVICES=16` (R7.1). `Binding` посилається
на device `id`; ідентичність (MAC/adv-name/topic) НЕ на біндінгу ролі —
`find_remote_device(id)` резолвить id→identity/name (R0.3, R4.1).

## Часування ініціалізації

`HAL` і `DriverManager` НЕ є підкласами `BaseModule`. Ними напряму
володіє main.cpp, ініціалізація виконується явно:

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

Це знаходиться між Фазою 1 (ConfigService дає плату й прив'язки) та
Фазою 2 (Equipment зареєстровано, бізнес-модулі тактують).

## Місток до Equipment Manager

Equipment Manager (modules/equipment) використовує драйвери, виявлені
DriverManager, і подає їх як ключі стану. З погляду бізнес-модуля:

```
   bindings.json says: role=air_temp uses driver=ds18b20 on ow_1, addr=28:...
            │
            ▼ DriverManager.init
   DS18B20Driver instance created, configured, init'd
            │
            ▼ Equipment.bind_drivers
   Driver registered під role "air_temp"
            │
            ▼ Equipment.on_update (every tick)
   driver->read(value);
   state_set("equipment.air_temp", value);
   state_set("equipment.air_temp_ok", driver->is_healthy());
```

Бізнес-модулі читають `equipment.air_temp` — вони ніколи не бачать клас
DS18B20Driver. Модуль оголошує лише capability (`temperature`); який
драйвер (ds18b20 / NTC / віддалений BLE-канал) її дає — вирішує прив'язка
за capability, не модуль (R0.1, R3.1).

### Віддалені пристрої (транспорт-генерично)

Периферія за межами плати описується як `RemoteDeviceConfig` у секції
`remote_devices` (factory-seed з board.json, змерджений з runtime
`/data/devices.json`). HAL зберігає лише `transport`-тег + непрозорий
`identity`-блоб — жодного HW-init; декод/кеш живе у транспортному
компоненті (напр. `modesp_ble` `BleCentral`), а драйвер читає його через
свій `hardware_type`. Прив'язка вказує device `id`; `find_remote_device(id)`
резолвить його до identity/name. Ідентичність (MAC) ніколи не лежить на
ролі — роль лишається транспорт-агностичною (R0.3, R4.1). Новий транспорт
(LoRa/MQTT/ESP-NOW) = новий компонент + драйвер-міст; HAL не чіпається
(R4.2).

## Стан здоров'я і режими відмов

Драйвери надають `is_healthy()` і `error_count()`. Equipment Manager:

- Зчитує `is_healthy()` щотакту.
- Після 3 невдалих читань поспіль встановлює
  `equipment.<role>_ok = false`.
- Журналює ESP_LOGW при кожному переході здоровий↔нездоровий.

DriverManager не деактивує драйвери, що відмовили — вони продовжують
тактувати (повторна спроба при наступному `update`). Більшість відмов
сенсорів — тимчасові (миттєва помилка CRC у DS18B20 тощо) і відновлюються
за кілька тактів.

Якщо `init()` драйвера повертає false (обладнання не виявлено),
Equipment Manager записує фатальну помилку ТА встановлює ключі ролі
прив'язки у значення за замовчуванням назавжди. Застосунок все одно може
завантажитись — просто без функціоналу цієї ролі.

## Пам'ять і виділення

Драйвери — **виділяються у купі** фабрикою DriverManager. Вони живуть до
перезавантаження прошивки — без виділень на такт, без знищення.

Бюджет купи на ESP32: ~65 КБ вільно після ініціалізації фреймворка.
Екземпляри драйверів у середньому займають ~200 байт кожен (стан, рядок
ролі, посилання на обладнання). 20 драйверів = 4 КБ. Комфортний запас.

Якщо колись знадобиться драйвер зі значно більшим станом (наприклад,
таблиці калібрування, буфери FIFO), задокументуйте це і виділіть пам'ять
заздалегідь у `configure`, щоб ініціалізація та виконання не дивували.

## Пам'ять і бюджет розміру

| Стаття | Вартість |
|---|---|
| Екземпляр HAL | ~400 байт (вказівники на периферію + облік GPIO) |
| DriverManager | ~600 байт (масиви вказівників на драйвери + таблиця фабрик) |
| Екземпляр одного драйвера | зазвичай 100-300 байт |
| Шина OneWire (через libonewire) | ~150 байт на шину |
| Шина I²C (драйвер ESP-IDF) | ~200 байт на шину |

Загальні накладні витрати HAL із типовою прив'язкою KC868-A6 (~10
драйверів): ~5 КБ RAM.

## Типові шаблони

### Додавання підтримки нового сенсора

1. Напишіть клас драйвера, що реалізує `ISensorDriver`. Див.
   [writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md).
2. Додайте маніфест драйвера у `drivers/<name>/manifest.json`.
3. Відредагуйте фабрику `create_sensor` у `driver_manager.cpp`, щоб
   диспетчер обробляв новий рядок `type`.
4. Оновіть `bindings.json` тестової плати, додавши прив'язку з новим
   драйвером.
5. Перезберіть і прошийте.

### Перевірити, що прив'язано

```bash
curl -u admin:modesp http://192.168.1.85/api/state | grep equipment.
```

Ключі `equipment.<role>` перелічують кожну успішно створену прив'язку.
Якщо ключ ролі відсутній — ініціалізація драйвера завершилась невдачею.

### Ручне перезавантаження драйверів (Stage 1.5)

Наразі прив'язки читаються один раз при завантаженні. Stage 1.5
планує `POST /api/bindings/reload`, щоб перезапустити шар драйверів без
перезавантаження — корисно для заміни типів сенсорів або перепідключення
"в полі".

## Типові помилки

**Конфлікти GPIO:** прив'язка оголошує роль на GPIO 14, але board.json
вже відображає GPIO 14 на реле. `init` HAL припиняє роботу з повідомленням
про конфлікт. Відредагуйте прив'язки або board.json.

**Невідповідність адрес на OneWire:** прив'язка вказує адресу
`28:8C:5E:45:D4:08:44:09`, але фізичний сенсор має інший ROM. Драйвер
повідомляє `not_found` після повторних спроб; ключ `_ok` сенсора ніколи
не стає true. Скористайтесь точкою доступу для виявлення, щоб отримати
правильні адреси.

**ADC2 використовується для сенсора:** GPIO 0, 2, 4, 12-15, 25-27 — це
канали ADC2, що конфліктують з Wi-Fi. Валідатор board.json має це
впіймати; якщо не впіймає, ваші читання сенсора сильно стрибатимуть,
коли Wi-Fi активний.

**Фабрика драйверів не оновлена:** додали новий тип драйвера у маніфест,
але забули додати випадок диспетчера у `driver_manager.cpp`. DriverManager
тихо пропускає його; симптоми схожі на відсутню прив'язку (ключ
`equipment.<role>` не з'являється).

**Виділення купи у `update()`:** драйвери тактують на 100 Гц. `new` у
`update` тече щотакту. Використовуйте буфери на стеку та ETL.

## Що далі

- **[writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md)**
  — покрокове проходження з боку автора.
- **[components/modesp_services.md](modesp_services.md)** — ConfigService
  надає BoardConfig і BindingTable.
- **[modules/equipment.md](../modules/equipment.md)** — Equipment Manager
  використовує драйвери driver_manager.
- **[04-hardware/board-config.md](../../04-hardware/board-config.md)** —
  схема `board.json`.
- **[04-hardware/bindings.md](../../04-hardware/bindings.md)** — схема
  `bindings.json`.

## Джерела

- [`components/modesp_hal/include/modesp/hal/`](../../../../components/modesp_hal/include/modesp/hal/)
  — публічні заголовки.
- [`components/modesp_hal/src/`](../../../../components/modesp_hal/src/)
  — реалізації HAL і DriverManager.
- [`drivers/`](../../../../drivers/) — реалізації драйверів, що
  використовують ці інтерфейси.
