# ModESP v4 — Core (modesp_core)

Ядро системи. Не залежить від нічого крім ETL та FreeRTOS.
Не знає про конкретні модулі, драйвери чи бізнес-логіку.

## types.h — Базові типи

```cpp
#pragma once
#include "etl/string.h"
#include "etl/variant.h"
#include "etl/optional.h"
#include "etl/message.h"

namespace modesp {

// ── Версія прошивки ──
constexpr const char* FIRMWARE_VERSION = "4.0.0";
constexpr const char* BUILD_DATE = __DATE__ " " __TIME__;

// ── Compile-time розміри (можна перевизначити через Kconfig) ──
#ifndef MODESP_MAX_KEY_LENGTH
#define MODESP_MAX_KEY_LENGTH 32
#endif

#ifndef MODESP_MAX_STRING_VALUE_LENGTH
#define MODESP_MAX_STRING_VALUE_LENGTH 32
#endif

#ifndef MODESP_MAX_MODULE_NAME_LENGTH
#define MODESP_MAX_MODULE_NAME_LENGTH 16
#endif

// ── StateKey — ключ для SharedState ──
using StateKey = etl::string<MODESP_MAX_KEY_LENGTH>;

// ── StateValue — значення в SharedState ──
// Підтримувані типи:
//   int32_t         — лічильники, коди, enum
//   float           — температура, тиск, напруга
//   bool            — стани (on/off, open/closed)
//   etl::string<32> — короткі текстові значення
using StringValue = etl::string<MODESP_MAX_STRING_VALUE_LENGTH>;
using StateValue  = etl::variant<int32_t, float, bool, StringValue>;

// ── ModulePriority — визначає порядок init/update/shutdown ──
enum class ModulePriority : uint8_t {
    CRITICAL = 0,   // error_service, watchdog, config, persist, equipment
    HIGH     = 1,   // wifi, protection
    NORMAL   = 2,   // thermostat, defrost
    LOW      = 3,   // system_monitor, datalogger
};

// ── Message ID діапазони ──
// Системні:    0-49
// Сервіси:    50-99
// HAL:       100-109
// Драйвери:  110-149
// Модулі:    150-249
namespace msg_id {
    // Системні (core)
    constexpr etl::message_id_t SYSTEM_INIT       = 0;
    constexpr etl::message_id_t SYSTEM_SHUTDOWN    = 1;
    constexpr etl::message_id_t SYSTEM_ERROR       = 2;
    constexpr etl::message_id_t SYSTEM_HEALTH      = 3;
    constexpr etl::message_id_t STATE_CHANGED      = 4;
    constexpr etl::message_id_t CONFIG_CHANGED     = 5;
    constexpr etl::message_id_t TIMER_TICK         = 6;
    constexpr etl::message_id_t SYSTEM_SAFE_MODE   = 7;

    // Сервіси
    constexpr etl::message_id_t MODULE_TIMEOUT     = 50;
    constexpr etl::message_id_t CONFIG_LOADED      = 51;
    constexpr etl::message_id_t CONFIG_SAVED       = 52;
    constexpr etl::message_id_t CONFIG_RESET       = 53;
    constexpr etl::message_id_t LOG_ENTRY          = 54;
    constexpr etl::message_id_t PERSIST_SAVED      = 55;

    // MQTT
    constexpr etl::message_id_t MQTT_CONNECTED    = 56;
    constexpr etl::message_id_t MQTT_DISCONNECTED = 57;
    constexpr etl::message_id_t MQTT_ERROR        = 58;

    // HAL
    constexpr etl::message_id_t GPIO_CHANGED       = 100;

    // Драйвери
    constexpr etl::message_id_t SENSOR_READING     = 110;
    constexpr etl::message_id_t SENSOR_ERROR       = 111;
    constexpr etl::message_id_t ACTUATOR_COMMAND   = 120;
    constexpr etl::message_id_t ACTUATOR_FEEDBACK  = 121;

    // Модулі
    constexpr etl::message_id_t ALARM_TRIGGERED    = 150;
    constexpr etl::message_id_t ALARM_CLEARED      = 151;
    constexpr etl::message_id_t SETPOINT_CHANGED   = 160;
    constexpr etl::message_id_t DEFROST_START      = 170;
    constexpr etl::message_id_t DEFROST_END        = 171;
}

// ── Спільні константи таймерів ──
/// Початкове значення таймера — "достатньо часу пройшло, дозволяємо дію".
/// Використовується для anti-short-cycle, min_on/off, relay min_switch.
static constexpr uint32_t TIMER_SATISFIED = 999999;

} // namespace modesp
```

## base_module.h — Базовий клас модуля

Кожен модуль (драйвер, сервіс, бізнес-логіка) наслідує BaseModule.

```cpp
#pragma once
#include "modesp/types.h"
#include "features_config.h"  // згенерований файл з feature flags
#include "etl/message.h"
#include "etl/optional.h"

namespace modesp {

class ModuleManager;
class SharedState;

class BaseModule {
public:
    BaseModule(const char* name, ModulePriority priority = ModulePriority::NORMAL)
        : name_(name), priority_(priority) {}
    virtual ~BaseModule() = default;

    // ── Lifecycle (перевизначаються модулем) ──
    virtual bool on_init()                            { return true; }
    virtual void on_update(uint32_t dt_ms)            {}
    virtual void on_message(const etl::imessage& msg) {}
    virtual void on_stop()                            {}

    // ── Ідентифікація ──
    const char*    name()     const { return name_; }
    ModulePriority priority() const { return priority_; }

    // ── Стан модуля ──
    enum class State : uint8_t {
        CREATED,
        INITIALIZED,
        RUNNING,
        STOPPED,
        ERROR
    };
    State state() const { return state_; }

    // ── Heartbeat (оновлюється автоматично ModuleManager) ──
    uint32_t last_update_ms() const { return last_update_ms_; }

protected:
    // ── API для модулів (доступний після реєстрації) ──

    // Відправити повідомлення на шину (всім підписникам)
    void publish(const etl::imessage& msg);

    // Записати значення в SharedState
    bool state_set(const StateKey& key, const StateValue& value);

    // Зручні обгортки для state_set (overloads, не окремі методи)
    bool state_set(const char* key, int32_t value);
    bool state_set(const char* key, float value);
    bool state_set(const char* key, bool value);
    bool state_set(const char* key, const char* value);

    // Прочитати значення з SharedState
    etl::optional<StateValue> state_get(const StateKey& key) const;
    etl::optional<StateValue> state_get(const char* key) const;

    // ── Типізовані read helpers (BUG-013) ──
    // Повертають default value замість optional — зручно для модулів
    float   read_float(const char* key, float def = 0.0f) const;
    bool    read_bool(const char* key, bool def = false) const;
    int32_t read_int(const char* key, int32_t def = 0) const;

    // ── Features (Phase 10.5) ──
    // constexpr lookup через features_config.h
    bool has_feature(const char* feature_name) const {
        return modesp::gen::is_feature_active(name(), feature_name);
    }

private:
    friend class ModuleManager;  // ModuleManager встановлює зв'язки

    const char*    name_;
    ModulePriority priority_;
    State          state_ = State::CREATED;
    uint32_t       last_update_ms_ = 0;

    // Зворотні посилання (встановлює ModuleManager при реєстрації)
    ModuleManager* manager_ = nullptr;
    SharedState*   shared_state_ = nullptr;
};

} // namespace modesp
```

**Ключовий момент:** Модуль НЕ знає про App singleton.
Він отримує доступ до шини та стану через `manager_` та `shared_state_`,
які встановлюються при реєстрації. Це дозволяє тестувати
модулі окремо з mock ModuleManager.

**Read helpers** (`read_float`, `read_bool`, `read_int`) повертають default value
замість `etl::optional`, що спрощує код модулів. Типовий паттерн:
```cpp
float air_temp = read_float("equipment.air_temp", -999.0f);
bool sensor_ok = read_bool("equipment.sensor1_ok", false);
int32_t mode   = read_int("thermostat.fan_mode", 0);
```

## shared_state.h — Сховище стану

Thread-safe key-value store з фіксованим розміром.
O(1) average access, zero heap allocation.

Ємність генерується автоматично: `MODESP_MAX_STATE_ENTRIES` = manifest keys + 32 (runtime margin).
Визначається в `generated/state_meta.h`, включається через `shared_state.h`.

```cpp
#pragma once
#include "modesp/types.h"
#include "etl/unordered_map.h"
#include "etl/optional.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace modesp {

// Визначається в generated/state_meta.h (auto-generated)
#include "state_meta.h"  // MODESP_MAX_STATE_ENTRIES

class SharedState {
public:
    SharedState();
    ~SharedState();

    // Не копіюється
    SharedState(const SharedState&) = delete;
    SharedState& operator=(const SharedState&) = delete;

    // ── Основний API ──
    bool set(const StateKey& key, const StateValue& value);
    etl::optional<StateValue> get(const StateKey& key) const;
    bool has(const StateKey& key) const;
    bool remove(const StateKey& key);
    size_t size() const;
    void clear();

    // ── Зручні обгортки ──
    bool set(const char* key, int32_t value);
    bool set(const char* key, float value);
    bool set(const char* key, bool value);
    bool set(const char* key, const char* value);

    etl::optional<StateValue> get(const char* key) const;

    // ── Ітерація (для серіалізації, з блокуванням!) ──
    using Map = etl::unordered_map<StateKey, StateValue, MODESP_MAX_STATE_ENTRIES>;

    // Callback викликається під mutex — не робити довгих операцій!
    using IterCallback = void(*)(const StateKey& key, const StateValue& value, void* user_data);
    void for_each(IterCallback cb, void* user_data) const;

    // Version counter — інкрементується на кожен успішний set().
    // WsService використовує для виявлення змін без повного порівняння.
    uint32_t version() const;

    // Лічильник відмов set() для діагностики (BUG-018)
    uint32_t set_failures() const { return set_failures_; }

    // ── Persist callback ──
    // Викликається ПІСЛЯ set() якщо значення змінилось.
    // PersistService реєструє callback для збереження в NVS.
    using PersistCallback = void(*)(const StateKey& key, const StateValue& value, void* user_data);
    void set_persist_callback(PersistCallback cb, void* user_data = nullptr);

private:
    Map map_;
    mutable SemaphoreHandle_t mutex_;
    uint32_t version_ = 0;
    uint32_t set_failures_ = 0;
    PersistCallback persist_cb_ = nullptr;
    void* persist_user_data_ = nullptr;
};

} // namespace modesp
```

**RAM (ESP32, 32-bit):** ~17KB для 136 entries:
- Map nodes: 136 × ~88B (key 40 + value 44 + link 4) = ~12KB
- Map buckets: 256 × ~12B = ~3KB (next_power_of_2 від capacity)
- changed_keys_ vector: 32 × 40B = 1.3KB
- Метадані: ~0.2KB

**version_ counter:** інкрементується на кожен `set()` — WsService порівнює версії для delta broadcast.

**Persist callback:** `set()` перевіряє зміну значення → викликає callback ПОЗА mutex для PersistService.

**set_failures_ (BUG-018):** лічильник невдалих `set()` операцій (mutex timeout). Доступний для діагностики через `system.set_failures` state key.

### Оптимізація пам'яті (на майбутнє)

Зараз SharedState займає ~17KB з ~280KB RAM ESP32. При зростанні проекту (нові модулі/ключі)
capacity збільшується автоматично, але кожен entry = ~88 bytes + bucket overhead.

**Стратегії оптимізації (за пріоритетом):**

| Стратегія | Економія | Складність | Опис |
|-----------|----------|------------|------|
| Numeric key IDs | ~5KB | Велика | Замінити etl::string<32> ключі на uint16_t. Генератор присвоює ID, lookup table для серіалізації. Зачіпає SharedState API, WS, MQTT, HTTP. |
| Typed map split | ~6-8KB | Середня | Окремі typed maps: float_map (80% ключів, 8B/entry), int_map, bool_map, string_map (~19 ключів, 44B/entry). Зменшує variant overhead. |
| Quick wins | ~2-3KB | Мала | changed_keys_ bitset (1.2KB), зменшити StateKey до etl::string<24> (1KB), замінити string values на int enums де можливо. |

**Тригер:** моніторити `system.heap_free`. Якщо < 30KB — час оптимізувати.

## module_manager.h — Управління модулями

Реєстрація, lifecycle, message bus.

**Важливо:** ModuleManager НЕ володіє SharedState — App володіє.
MM отримує посилання на SharedState через `init_all(SharedState&)`.

```cpp
#pragma once
#include "modesp/types.h"
#include "modesp/base_module.h"
#include "modesp/shared_state.h"
#include "etl/message_bus.h"
#include "etl/message_router.h"
#include "etl/vector.h"
#include "etl/array.h"

namespace modesp {

#ifndef MODESP_MAX_MODULES
#define MODESP_MAX_MODULES 16
#endif

#ifndef MODESP_MAX_BUS_ROUTERS
#define MODESP_MAX_BUS_ROUTERS 24
#endif

// ModuleAdapter — обгортає BaseModule як etl::imessage_router
// Catch-all: приймає ВСІ повідомлення і передає в BaseModule.on_message()
class ModuleAdapter : public etl::imessage_router {
public:
    ModuleAdapter(etl::message_router_id_t id = 0);
    void bind(BaseModule* module);
    void receive(const etl::imessage& msg) override;
    bool accepts(etl::message_id_t) const override { return true; }
    // ...
};

class ModuleManager {
public:
    ModuleManager();

    // Не копіюється
    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    // ── Реєстрація ──
    bool register_module(BaseModule& module);

    // ── Lifecycle (викликається з main.cpp через App) ──
    bool init_all(SharedState& state);  // SharedState передається ззовні!
    void update_all(uint32_t dt_ms);
    void stop_all();

    // ── Перезапуск модуля (для WatchdogService) ──
    bool restart_module(BaseModule& module);

    // ── Message Bus ──
    void publish(const etl::imessage& msg);

    // ── Діагностика ──
    size_t module_count() const { return modules_.size(); }

    // Ітерація модулів (для HTTP API серіалізації)
    using ModuleCallback = void(*)(BaseModule& module, void* user_data);
    void for_each(ModuleCallback cb, void* user_data);

    using ModuleVisitor = void(*)(const BaseModule& module, void* user_data);
    void for_each_module(ModuleVisitor visitor, void* user_data) const;

private:
    etl::vector<BaseModule*, MODESP_MAX_MODULES> modules_;
    etl::array<ModuleAdapter, MODESP_MAX_MODULES> adapters_;
    size_t adapter_count_ = 0;

    etl::message_bus<MODESP_MAX_BUS_ROUTERS> bus_;

    // Зв'язок з SharedState (встановлюється в init_all)
    SharedState* shared_state_ = nullptr;

    void sort_by_priority();
};

} // namespace modesp
```

**init_all()** — ідемпотентний: пропускає модулі зі state != CREATED.
Це дозволяє three-phase init у main.cpp:
1. Phase 1: system services (error, watchdog, config, persist)
2. Phase 2: WiFi + business modules (equipment, protection, thermostat, defrost, datalogger)
3. Phase 3: HTTP + WebSocket

## app.h — Application Lifecycle

App — singleton, що зв'язує ModuleManager + SharedState.
**Модулі НЕ мають доступу до App** — вони працюють через BaseModule API.

```cpp
#pragma once
#include "modesp/module_manager.h"
#include "modesp/shared_state.h"

namespace modesp {

#ifndef MODESP_MAIN_LOOP_HZ
#define MODESP_MAIN_LOOP_HZ 100
#endif

#ifndef MODESP_UPDATE_BUDGET_MS
#define MODESP_UPDATE_BUDGET_MS 8
#endif

#ifndef MODESP_WDT_TIMEOUT_MS
#define MODESP_WDT_TIMEOUT_MS 5000   // 5 секунд HW watchdog
#endif

class App {
public:
    // Singleton (тимчасово, для Phase 1)
    static App& instance();

    // Не копіюється
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // ── Lifecycle ──
    bool init();   // Запис boot_time, логування версії
    void run();    // Блокуючий main loop (є в коді, але main.cpp має свій loop)
    void stop();   // Graceful shutdown

    // ── Доступ до підсистем (тільки для main.cpp!) ──
    ModuleManager& modules() { return modules_; }
    SharedState&   state()   { return state_; }

    // ── Стан ──
    uint32_t uptime_sec() const;
    bool     is_running() const { return running_; }

private:
    App() = default;

    ModuleManager modules_;
    SharedState   state_;
    bool          running_ = false;
    uint32_t      boot_time_us_ = 0;
};

} // namespace modesp
```

**App vs main.cpp:** App має метод `run()` з simple main loop, але реальний
`main.cpp` використовує власний main loop з додатковою логікою:
- `driver_manager.update_all(dt_ms)` перед `modules.update_all()`
- Оновлення `system.uptime` та `system.heap_largest` раз на секунду
- HW watchdog reset
- 3-phase staged boot (замість одного `init_all()` в `App::run()`)

**Hardware Watchdog:** Конфігурується в main.cpp з `MODESP_WDT_TIMEOUT_MS` (5 секунд).
Якщо main loop зависне — ESP32 panic + автоматичний reboot.
Це **другий рівень захисту** поверх software WatchdogService.

## Changelog
- 2026-03-01 — Рефакторинг: оновлено API для відповідності реальному коду, додано read helpers, виправлено App/ModuleManager, SharedState — не шаблон, MQTT msg_ids, TIMER_SATISFIED
