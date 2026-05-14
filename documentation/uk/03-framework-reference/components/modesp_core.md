# `modesp_core` — App, ModuleManager, SharedState, BaseModule

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_core.md](../../../en/03-framework-reference/components/modesp_core.md)

`modesp_core` — foundation на якому ModESP stands. Не має framework
залежностей вище ETL і FreeRTOS, і кожен інший компонент залежить від
нього. Пʼять публічних типів — `App`, `ModuleManager`, `SharedState`,
`BaseModule`, і type definitions у `types.h` — collectively визначають
всю framework programming model.

Ця сторінка документує кожен у деталях з повним API surface, lifetime
guarantees, і thread-safety contracts. Якщо пишете modules, ви будете
torch-ити `BaseModule` і `SharedState` constantly; `App` і
`ModuleManager` wire-ються у main.cpp і rarely revisited.

## Публічні headers

```
components/modesp_core/include/modesp/
├── app.h                    ← App singleton, lifecycle root
├── base_module.h            ← BaseModule — base class вашого module
├── module_manager.h         ← ModuleManager — registry і tick driver
├── shared_state.h           ← SharedState — thread-safe key-value store
├── types.h                  ← StateKey, StateValue, ModulePriority, тощо.
├── message.h                ← etl::imessage wrapper + message ID ranges
└── platform/timing.h        ← millis() helper (FreeRTOS-aware)
```

REQUIRES: `marcel-cd__etlcpp` (ETL templates), `freertos` (mutex, tasks).

## Types — `modesp/types.h`

### `StateKey` і `StateValue`

```cpp
namespace modesp {

using StateKey   = etl::string<MODESP_MAX_KEY_LENGTH>;       // default 32 chars
using StringValue = etl::string<MODESP_MAX_STRING_VALUE_LENGTH>; // default 32 chars

using StateValue = etl::variant<int32_t, float, bool, StringValue>;

}
```

| Type | Notes |
|---|---|
| `StateKey` | До 32 символів. Long names rejected при `state_set` time. |
| `StringValue` | До 32 символів. Strings larger вимагають raw NVS або LittleFS. |
| `StateValue` | Variant з 4 cases. Кожен key locks до first-set type. |

Configurable через Kconfig (`CONFIG_MODESP_MAX_KEY_LENGTH`,
`CONFIG_MODESP_MAX_STRING_VALUE_LENGTH`), але defaults battle-tested.
Raising limits costs RAM linearly у map SharedState.

### `ModulePriority`

```cpp
enum class ModulePriority : uint8_t {
    CRITICAL = 0,   // error_service, watchdog
    HIGH     = 1,   // wifi, hal, drivers, scenario engine
    NORMAL   = 2,   // business modules (default)
    LOW      = 3,   // http, ws, datalogger
};
```

Controls init phase і update order. Modules у межах priority bucket
init/update у **registration order**. Lower priority → earlier у phase.

### Message ID ranges

`message.h` divides uint16 message ID space:

| Range | Owner |
|---|---|
| 0-49 | System (`MSG_SYS_*`) |
| 50-99 | Services (`MSG_SVC_*`) |
| 100-109 | HAL |
| 110-149 | Drivers |
| 150-199 | (reserved) |
| 200+ | Domain modules |

Use distinct IDs щоразу як визначаєте новий typed message — collisions
break ETL message dispatch.

## `BaseModule` — `modesp/base_module.h`

Клас від якого усі service modules inherit. Lifetime: static у main.cpp
(constructed once при program start, destroyed ніколи).

### Construction

```cpp
class BaseModule {
public:
    BaseModule(const char* name, ModulePriority priority);
    virtual ~BaseModule() = default;
};
```

`name` matched проти `"module"` field у вашому manifest. Length limit:
`MODESP_MAX_MODULE_NAME_LENGTH` (default 16 chars).

### Lifecycle hooks

```cpp
virtual bool on_init()                            { return true; }
virtual void on_update(uint32_t dt_ms)            {}
virtual void on_message(const etl::imessage& msg) {}
virtual void on_stop()                            {}
```

| Hook | Called | Return / blocking |
|---|---|---|
| `on_init` | Один раз при startup у phase що matches `priority`. | Return `false` щоб mark module FAILED. |
| `on_update` | Кожні 10 мс (100 Hz) коли у INITIALISED state. | Must не block (< 1 мс типово). |
| `on_message` | Коли `ModuleManager::send_message` targets ім'я цього module. | Should не block; chain до internal queues якщо треба. |
| `on_stop` | Один раз при shutdown OR якщо `stop_all()` called. | Cleanup; рідко на practice. |

### Module state machine

```
   CREATED ──init→ INITIALISED ──tick→ INITIALISED  (steady state)
       │              │
       │              └──stop→ STOPPED
       │
       └──init failed→ FAILED
```

| State | Meaning |
|---|---|
| `CREATED` | Constructed але `on_init` ще не called. |
| `INITIALISED` | `on_init` повернув `true`. Module receives `on_update` calls. |
| `FAILED` | `on_init` повернув `false`. Module registered але inactive. |
| `STOPPED` | `on_stop` ran. Module no longer ticks. |

Visible через `BaseModule::state() const`.

### Convenience accessors (built на SharedState)

```cpp
// Write — typed overloads, всі delegate до SharedState::set.
bool state_set(const char* key, int32_t value, bool track_change = true);
bool state_set(const char* key, float value,   bool track_change = true);
bool state_set(const char* key, bool value,    bool track_change = true);
bool state_set(const char* key, const char* value, bool track_change = true);

// Read — typed з default fallback.
float   read_float(const char* key, float def = 0.0f) const;
int32_t read_int(const char* key, int32_t def = 0) const;
bool    read_bool(const char* key, bool def = false) const;

// Generic — etl::optional<StateValue>.
etl::optional<StateValue> state_get(const char* key) const;
```

Див. [shared-state.md](../../02-module-author-guide/shared-state.md) для
повних semantics і common pitfalls.

### Identity і diagnostics

```cpp
const char*    name() const;
ModulePriority priority() const;
ModuleState    state() const;
```

`/api/modules` HTTP endpoint reports `name()` і `state()` для кожного
registered module.

## `ModuleManager` — `modesp/module_manager.h`

Holds fixed-capacity array `BaseModule*` references і drives їхні
lifecycles. Один instance живе всередині `App`.

### Capacity

Default capacity set у Kconfig (`CONFIG_MODESP_MAX_MODULES`, типово 32).
Exceeding це silently drops registration (registrations return `false`).

### API

```cpp
class ModuleManager {
public:
    bool register_module(BaseModule& m);          // false якщо already registered або full
    bool init_all(SharedState& state);            // calls on_init() на кожному CREATED module
    void update_all(uint32_t dt_ms);              // calls on_update() на кожному INITIALISED module
    void on_message(const etl::imessage& msg);    // dispatch до addressed module's on_message
    void stop_all();                              // calls on_stop() на всіх initialised modules
    bool send_message(const char* target, const etl::imessage& msg);

    // Diagnostic
    size_t count() const;
    BaseModule* find(const char* name) const;
    void for_each(std::function<void(BaseModule&)> fn) const;
};
```

### Three-phase init pattern

`init_all` — **gate** — it only initialises modules currently у CREATED
state. Calling it once at boot initialises every registered module;
calling it three times із registrations interleaved gives phased init
описаний у [architecture.md](../architecture.md#three-phase-init).

```cpp
// Phase 1
app.modules().register_module(error_service);
app.modules().register_module(watchdog);
app.modules().init_all(app.state());            // these inited

// Phase 2
app.modules().register_module(wifi);
app.modules().register_module(hal);
app.modules().init_all(app.state());            // now these inited; phase 1 modules skipped

// Phase 3
app.modules().register_module(http);
app.modules().init_all(app.state());            // only http inited тут
```

### Update tick

`update_all` iterates registered modules у insertion order і викликає
`on_update(dt_ms)` на кожному `INITIALISED` module. Single-threaded — no
parallel module execution.

### Messages

Module sends typed `etl::imessage` до іншого module по імені:

```cpp
struct OtaProgressMsg : public etl::message<200> {
    uint32_t bytes_transferred;
    uint32_t total_bytes;
};

OtaProgressMsg msg{12345, 67890};
app.modules().send_message("datalogger", msg);
```

Target's `on_message` runs synchronously на sending task. Use sparingly —
більшість речей повинні flow через SharedState натомість.

## `SharedState` — `modesp/shared_state.h`

Thread-safe typed key-value store. Один instance живе всередині `App`.

### Storage

```cpp
using Map = etl::unordered_map<StateKey, StateValue, MODESP_MAX_STATE_ENTRIES>;
```

`MODESP_MAX_STATE_ENTRIES` defaults до 96, auto-generated у `state_meta.h`
з маніфестів. Bump через Kconfig якщо треба (cost: ~80 bytes RAM per
entry).

### Read/write API

```cpp
class SharedState {
public:
    // Write
    bool set(const StateKey& key, const StateValue& value, bool track_change = true);
    bool set(const char* key, int32_t value, bool track_change = true);
    bool set(const char* key, float value, bool track_change = true);
    bool set(const char* key, bool value, bool track_change = true);
    bool set(const char* key, const char* value, bool track_change = true);

    // Read
    etl::optional<StateValue> get(const StateKey& key) const;
    etl::optional<StateValue> get(const char* key) const;
    bool has(const StateKey& key) const;
    bool remove(const StateKey& key);

    // Maintenance
    size_t size() const;
    void clear();
};
```

### Change tracking і WebSocket integration

```cpp
class SharedState {
public:
    using IterCallback = void(*)(const StateKey&, const StateValue&, void* user_data);

    // Iterate ALL keys (під mutex — callback повинен бути fast).
    void for_each(IterCallback cb, void* user_data) const;

    // Iterate changed keys (since last flush) AND clear list.
    bool for_each_changed_and_clear(IterCallback cb, void* user_data);

    bool has_changes() const;
    bool needs_full_broadcast() const;   // true якщо changed_keys_ overflowed
    uint32_t version() const;             // monotonic counter при кожному tracked set
    uint32_t set_failures() const;        // diagnostic — set() rejection count
};
```

WebSocket service використовує це щоб broadcast deltas кожні ~500 мс.
PersistService і MqttService також observe state changes через цей API.

### Persistence hook

```cpp
using PersistCallback = void(*)(const StateKey&, const StateValue&, void* user_data);
void set_persist_callback(PersistCallback cb, void* user_data);
```

PersistService registers callback що fires при кожному tracked write —
ось як `persist: true` працює (див.
[persistence.md](../../02-module-author-guide/persistence.md)).

### Thread safety

Всі методи acquire FreeRTOS mutex internally. Safe з:
- Module tick task (default).
- HTTP request task.
- MQTT subscribe callback task.
- Recipe scenario engine.
- **НЕ ISR** (mutex blocks).

Mutex timeout — 100 мс. Якщо `set` returns `false`, `set_failures()`
increments — non-zero value натякає на contention.

## `App` — `modesp/app.h`

Application singleton що owns SharedState і ModuleManager.

```cpp
class App {
public:
    static App& instance();

    bool init();                          // creates state, manager. Idempotent.
    ModuleManager& modules();             // mutable accessor
    SharedState&   state();               // mutable accessor

private:
    App() = default;                      // private — use instance()
    SharedState   state_;
    ModuleManager modules_;
};
```

Lifetime: lazily constructed при першому `App::instance()` call. Живе
до program exit (ніколи, на embedded). Обидва accessors return references
що outlive будь-який module.

## Memory і size budget

| Item | RAM cost |
|---|---|
| SharedState із 96 entries | ~8 KB (key + variant + bookkeeping) |
| ModuleManager із 32 slots | ~512 bytes (pointer array + status) |
| Один BaseModule subclass | depends на ваші members; aim для < 256 bytes |
| Mutex (FreeRTOS) | ~88 bytes |

Total core overhead: ~10 KB RAM. Heap usage: zero (усе ETL static).

## Common usage patterns

### Typical module skeleton

```cpp
// my_module/include/my_module.h
#pragma once
#include "modesp/base_module.h"

class MyModule : public modesp::BaseModule {
public:
    MyModule() : BaseModule("my_module", modesp::ModulePriority::NORMAL) {}

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

private:
    uint32_t elapsed_ = 0;
};

// my_module/src/my_module.cpp
bool MyModule::on_init() {
    state_set("my_module.counter", static_cast<int32_t>(0));
    return true;
}

void MyModule::on_update(uint32_t dt_ms) {
    elapsed_ += dt_ms;
    if (elapsed_ < 1000) return;
    elapsed_ = 0;

    int32_t n = read_int("my_module.counter", 0);
    state_set("my_module.counter", n + 1);
}
```

### Diagnostic snapshot з HTTP handler

```cpp
// runs на httpd task
auto& state = HttpService::app_ref().state();
state.for_each([](const auto& k, const auto& v, void* ctx) {
    auto* json = static_cast<JsonWriter*>(ctx);
    // emit key=value до JSON
}, &json_writer);
```

(HTTP handlers зазвичай не `register_module`; вони використовують
`app.state()` напряму через dependency injection.)

### Static-init modules

Modules instantiated як file-scope statics у `main.cpp` (згенерований
`module_instances.h`):

```cpp
static modesp::ErrorService error_service;
static MyModule             my_module;
// ... constructed before app_main runs.
```

Constructor bodies повинні бути **trivial** — лише set defaults. Real
init йде у `on_init` after SharedState exists.

## Common pitfalls

**Calling `state_set` з constructor:** SharedState ще не існує при
static-init time. Завжди defer до `on_init`.

**Re-entrant `SharedState` calls з всередині `for_each` callback:** mutex
acquired для iteration; calling `set` / `get` з вашого callback
deadlocks. Buffer changes externally і apply after iteration.

**Heavy work у `on_message`:** runs на whatever task що sent message.
Long handlers block that task. Queue work до власного state machine для
processing у `on_update`.

**Missing `override` на virtual methods:** silent typo (`on_initt`)
leaves default no-op у place. Завжди use `override`.

## Що далі

- **[components/modesp_services.md](modesp_services.md)** *(planned)* —
  Error/Watchdog/Config/Persist/Logger services що build на modesp_core.
- **[components/modesp_hal.md](modesp_hal.md)** *(planned)* — HAL і
  DriverManager.
- **[02-module-author-guide/writing-a-module.md](../../02-module-author-guide/writing-a-module.md)**
  — author-side walkthrough using це API.
- **[02-module-author-guide/shared-state.md](../../02-module-author-guide/shared-state.md)**
  — SharedState patterns і type rules.

## Source

- [`components/modesp_core/include/modesp/`](../../../../components/modesp_core/include/modesp/)
  — public headers.
- [`components/modesp_core/src/`](../../../../components/modesp_core/src/)
  — implementations.
