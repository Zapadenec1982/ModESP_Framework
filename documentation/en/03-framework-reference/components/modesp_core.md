# `modesp_core` — App, ModuleManager, SharedState, BaseModule

> 📖 **Українською:** [documentation/uk/03-framework-reference/components/modesp_core.md](../../../uk/03-framework-reference/components/modesp_core.md)

`modesp_core` is the foundation на якому ModESP stands. It has no
framework dependencies above ETL і FreeRTOS, and every other component
depends on it. The five public types — `App`, `ModuleManager`,
`SharedState`, `BaseModule`, і the type definitions у `types.h` —
collectively define the entire framework programming model.

This page documents each in detail with full API surface, lifetime
guarantees, і thread-safety contracts. If you're writing modules,
you'll touch `BaseModule` і `SharedState` constantly; `App` і
`ModuleManager` are wired у main.cpp and rarely revisited.

## Public headers

```
components/modesp_core/include/modesp/
├── app.h                    ← App singleton, lifecycle root
├── base_module.h            ← BaseModule — your module's base class
├── module_manager.h         ← ModuleManager — registry і tick driver
├── shared_state.h           ← SharedState — thread-safe key-value store
├── types.h                  ← StateKey, StateValue, ModulePriority, etc.
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
| `StateKey` | Up to 32 characters. Long names rejected at `state_set` time. |
| `StringValue` | Up to 32 characters. Strings larger than це require raw NVS or LittleFS. |
| `StateValue` | Variant із 4 cases. Each key locks to its first-set type. |

Configurable through Kconfig (`CONFIG_MODESP_MAX_KEY_LENGTH`,
`CONFIG_MODESP_MAX_STRING_VALUE_LENGTH`), but defaults are battle-tested.
Raising the limits costs RAM linearly у `SharedState`'s map.

### `ModulePriority`

```cpp
enum class ModulePriority : uint8_t {
    CRITICAL = 0,   // error_service, watchdog
    HIGH     = 1,   // wifi, hal, drivers, scenario engine
    NORMAL   = 2,   // business modules (default)
    LOW      = 3,   // http, ws, datalogger
};
```

Controls init phase і update order. Modules within а priority bucket
init/update у **registration order**. Lower priority → earlier у phase.

### Message ID ranges

`message.h` divides the uint16 message ID space:

| Range | Owner |
|---|---|
| 0-49 | System (`MSG_SYS_*`) |
| 50-99 | Services (`MSG_SVC_*`) |
| 100-109 | HAL |
| 110-149 | Drivers |
| 150-199 | (reserved) |
| 200+ | Domain modules |

Use distinct IDs whenever you define а new typed message — collisions
break ETL's message dispatch.

## `BaseModule` — `modesp/base_module.h`

The class that all service modules inherit from. Lifetime: static у
main.cpp (constructed once at program start, destroyed never).

### Construction

```cpp
class BaseModule {
public:
    BaseModule(const char* name, ModulePriority priority);
    virtual ~BaseModule() = default;
};
```

`name` is matched against the `"module"` field у your manifest. Length
limit: `MODESP_MAX_MODULE_NAME_LENGTH` (default 16 chars).

### Lifecycle hooks

```cpp
virtual bool on_init()                            { return true; }
virtual void on_update(uint32_t dt_ms)            {}
virtual void on_message(const etl::imessage& msg) {}
virtual void on_stop()                            {}
```

| Hook | Called | Return / blocking |
|---|---|---|
| `on_init` | Once at startup у the phase matching `priority`. | Return `false` to mark module FAILED. |
| `on_update` | Every 10 ms (100 Hz) when у INITIALISED state. | Must not block (< 1 ms typical). |
| `on_message` | When `ModuleManager::send_message` targets це module name. | Should not block; chain to internal queues if needed. |
| `on_stop` | Once at shutdown OR if `stop_all()` called. | Cleanup; rare у practice. |

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
| `CREATED` | Constructed but `on_init` not yet called. |
| `INITIALISED` | `on_init` returned `true`. Module receives `on_update` calls. |
| `FAILED` | `on_init` returned `false`. Module is registered but inactive. |
| `STOPPED` | `on_stop` ran. Module no longer ticks. |

Visible via `BaseModule::state() const`.

### Convenience accessors (built on SharedState)

```cpp
// Write — typed overloads, all delegate до SharedState::set.
bool state_set(const char* key, int32_t value, bool track_change = true);
bool state_set(const char* key, float value,   bool track_change = true);
bool state_set(const char* key, bool value,    bool track_change = true);
bool state_set(const char* key, const char* value, bool track_change = true);

// Read — typed із default fallback.
float   read_float(const char* key, float def = 0.0f) const;
int32_t read_int(const char* key, int32_t def = 0) const;
bool    read_bool(const char* key, bool def = false) const;

// Generic — etl::optional<StateValue>.
etl::optional<StateValue> state_get(const char* key) const;
```

See [shared-state.md](../../02-module-author-guide/shared-state.md) для
full semantics і common pitfalls.

### Identity і diagnostics

```cpp
const char*    name() const;
ModulePriority priority() const;
ModuleState    state() const;
```

`/api/modules` HTTP endpoint reports `name()` і `state()` для each
registered module.

## `ModuleManager` — `modesp/module_manager.h`

Holds а fixed-capacity array of `BaseModule*` references і drives their
lifecycles. One instance lives inside `App`.

### Capacity

Default capacity is set у Kconfig (`CONFIG_MODESP_MAX_MODULES`, typically
32). Exceeding це silently drops the registration (registrations return
`false`).

### API

```cpp
class ModuleManager {
public:
    bool register_module(BaseModule& m);          // false if already registered or full
    bool init_all(SharedState& state);            // calls on_init() on each CREATED module
    void update_all(uint32_t dt_ms);              // calls on_update() on each INITIALISED module
    void on_message(const etl::imessage& msg);    // dispatch to addressed module's on_message
    void stop_all();                              // calls on_stop() on all initialised modules
    bool send_message(const char* target, const etl::imessage& msg);

    // Diagnostic
    size_t count() const;
    BaseModule* find(const char* name) const;
    void for_each(std::function<void(BaseModule&)> fn) const;
};
```

### Three-phase init pattern

`init_all` is а **gate** — it only initialises modules currently у
CREATED state. Calling it once at boot initialises every registered
module; calling it three times із registrations interleaved gives the
phased init described у [architecture.md](../architecture.md#three-phase-init).

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

`update_all` iterates registered modules у insertion order і calls
`on_update(dt_ms)` on each `INITIALISED` module. Single-threaded — no
parallel module execution.

### Messages

Module sends а typed `etl::imessage` to another module by name:

```cpp
struct OtaProgressMsg : public etl::message<200> {
    uint32_t bytes_transferred;
    uint32_t total_bytes;
};

OtaProgressMsg msg{12345, 67890};
app.modules().send_message("datalogger", msg);
```

The target's `on_message` runs synchronously on the sending task. Use
sparingly — most things should flow through SharedState instead.

## `SharedState` — `modesp/shared_state.h`

Thread-safe typed key-value store. One instance lives inside `App`.

### Storage

```cpp
using Map = etl::unordered_map<StateKey, StateValue, MODESP_MAX_STATE_ENTRIES>;
```

`MODESP_MAX_STATE_ENTRIES` defaults to 96, auto-generated у `state_meta.h`
from manifests. Bump via Kconfig if needed (cost: ~80 bytes RAM per entry).

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

    // Iterate ALL keys (под mutex — callback must be fast).
    void for_each(IterCallback cb, void* user_data) const;

    // Iterate changed keys (since last flush) AND clear list.
    bool for_each_changed_and_clear(IterCallback cb, void* user_data);

    bool has_changes() const;
    bool needs_full_broadcast() const;   // true if changed_keys_ overflowed
    uint32_t version() const;             // monotonic counter on every tracked set
    uint32_t set_failures() const;        // diagnostic — set() rejection count
};
```

WebSocket service uses these to broadcast deltas every ~500 ms.
PersistService і MqttService also observe state changes through this API.

### Persistence hook

```cpp
using PersistCallback = void(*)(const StateKey&, const StateValue&, void* user_data);
void set_persist_callback(PersistCallback cb, void* user_data);
```

PersistService registers а callback that fires on every tracked write —
що's how `persist: true` works (see
[persistence.md](../../02-module-author-guide/persistence.md)).

### Thread safety

All methods acquire а FreeRTOS mutex internally. Safe from:
- Module tick task (default).
- HTTP request task.
- MQTT subscribe callback task.
- Recipe scenario engine.
- **NOT ISR** (mutex blocks).

Mutex timeout is 100 ms. If `set` returns `false`, `set_failures()`
increments — а non-zero value hints at contention.

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

Lifetime: lazily constructed on first `App::instance()` call. Lives until
program exit (never, on embedded). Both accessors return references that
outlive any module.

## Memory і size budget

| Item | RAM cost |
|---|---|
| SharedState із 96 entries | ~8 KB (key + variant + bookkeeping) |
| ModuleManager із 32 slots | ~512 bytes (pointer array + status) |
| One BaseModule subclass | depends on your members; aim for < 256 bytes |
| Mutex (FreeRTOS) | ~88 bytes |

Total core overhead: ~10 KB RAM. Heap usage: zero (all ETL static).

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

### Diagnostic snapshot from HTTP handler

```cpp
// runs on httpd task
auto& state = HttpService::app_ref().state();
state.for_each([](const auto& k, const auto& v, void* ctx) {
    auto* json = static_cast<JsonWriter*>(ctx);
    // emit key=value into JSON
}, &json_writer);
```

(HTTP handlers don't typically `register_module`; they use `app.state()`
directly through dependency injection.)

### Static-init of modules

Modules instantiated як file-scope statics у `main.cpp` (generated
`module_instances.h`):

```cpp
static modesp::ErrorService error_service;
static MyModule             my_module;
// ... constructed before app_main runs.
```

Constructor bodies should be **trivial** — only set defaults. Real init
goes у `on_init` після SharedState exists.

## Common pitfalls

**Calling `state_set` from constructor:** SharedState doesn't exist yet
at static-init time. Always defer to `on_init`.

**Re-entrant `SharedState` calls from inside `for_each` callback:** the
mutex is acquired for the iteration; calling `set` / `get` from your
callback deadlocks. Buffer changes externally and apply after iteration.

**Heavy work у `on_message`:** runs on whatever task sent the message.
Long handlers block that task. Queue work to your own state machine for
processing on `on_update`.

**Missing `override` on virtual methods:** silent typo (`on_initt`)
leaves the default no-op у place. Always use `override`.

## Next steps

- **[components/modesp_services.md](modesp_services.md)** *(planned)* —
  Error/Watchdog/Config/Persist/Logger services що build on modesp_core.
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
