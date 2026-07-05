# Writing а service module

> 📖 **Українською:** [documentation/uk/02-module-author-guide/writing-a-module.md](../../uk/02-module-author-guide/writing-a-module.md)

This page is а complete walkthrough of building а C++ service module — а
class that derives from `modesp::BaseModule`, registers itself із the build,
and runs business logic on the 100 Hz update loop. After reading this you'll
be able to create а new module folder, write the manifest + C++, see it
boot, і interact із its state via WebUI і HTTP API.

Recipe modules (manifest only, no C++) are covered у
[recipe-authoring.md](recipe-authoring.md) *(planned)*. Drivers are у
[writing-a-driver.md](writing-a-driver.md) *(planned)*.

## What а module is

A service module is а `modesp::BaseModule` subclass that:

- Lives у `modules/<name>/` із its own `CMakeLists.txt` і `manifest.json`.
- Gets constructed at static-storage init time (no heap, no `new`).
- Receives three lifecycle hooks driven by `ModuleManager`: `on_init()` once,
  `on_update(dt_ms)` every 10 ms, `on_stop()` on shutdown.
- Optionally receives messages через `on_message(const etl::imessage&)`.
- Reads і writes state through `SharedState` helpers.

The framework's auto-generation system makes the boilerplate disappear:
manifest declares what the module does; generated `module_includes.h` /
`module_instances.h` / `module_register.h` headers wire the instance into
`main.cpp` без manual edits.

## Folder layout

```
modules/your_module/
├── manifest.json          ← REQUIRED — manifest contract
├── CMakeLists.txt         ← REQUIRED — single line per file
├── include/
│   └── your_module.h      ← Module class declaration
└── src/
    └── your_module.cpp    ← Implementation
```

> ℹ️ **Note:** the header і source filenames must match the C++ class name
> conventions used by the generator. Use `<name>_module.h` / `<name>_module.cpp`
> patterns from existing modules (е.g. `simple_thermo_module.cpp`).

## Step 1 — Write the manifest

Cover the basics: top-level fields, state keys, optionally `ui` і `mqtt`.
Full reference у [manifest.md](manifest.md).

Minimal example (`modules/my_counter/manifest.json`):

```json
{
  "manifest_version": 1,
  "module": "my_counter",
  "version": "0.1.0",
  "description": "Counts seconds since boot — proof-of-life demo",
  "priority": 2,

  "state": {
    "my_counter.seconds": {
      "type": "int",
      "access": "read",
      "description": "Seconds since module init"
    }
  },

  "ui": {
    "page": "Counter",
    "icon": "clock",
    "cards": [{
      "title": "Uptime counter",
      "widgets": [
        {"key": "my_counter.seconds", "widget": "value"}
      ]
    }]
  }
}
```

The build picks це up automatically through `project.json` (next step).

## Step 2 — Register у project.json

Add the module name to the project's module list:

```json
// project.json (root)
{
  "modules": ["equipment", "datalogger", "simple_thermo", "my_counter"]
}
```

The generator only processes manifests for modules listed here. This lets
you keep multiple modules у the repo and pick which ones ship у а given
firmware build.

This is the only registration step. The generator writes
`generated/modules.cmake` (`PRODUCT_MODULES`), which `main/CMakeLists.txt` includes
automatically — the CMake dependency appears without manual edits. Editing
`project.json` or any manifest automatically re-runs configure and generation
(`CMAKE_CONFIGURE_DEPENDS`).

## Step 3 — Write CMakeLists.txt

```cmake
# modules/my_counter/CMakeLists.txt
idf_component_register(
    SRCS "src/my_counter_module.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core
)
```

`REQUIRES modesp_core` gives access to `BaseModule`, `SharedState`,
`ModulePriority`. Add other components (`modesp_services`, `modesp_hal`,
etc.) if your module needs them.

## Step 4 — Write the C++ class

Header:

```cpp
// modules/my_counter/include/my_counter_module.h
#pragma once
#include "modesp/base_module.h"

class MyCounterModule : public modesp::BaseModule {
public:
    MyCounterModule();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

private:
    uint32_t elapsed_ms_ = 0;
    int32_t  seconds_ = 0;
};
```

Source:

```cpp
// modules/my_counter/src/my_counter_module.cpp
#include "my_counter_module.h"
#include "esp_log.h"

static const char* TAG = "MyCounter";

MyCounterModule::MyCounterModule()
    : BaseModule("my_counter", modesp::ModulePriority::NORMAL)
{}

bool MyCounterModule::on_init() {
    state_set("my_counter.seconds", static_cast<int32_t>(0));
    ESP_LOGI(TAG, "Counter started");
    return true;
}

void MyCounterModule::on_update(uint32_t dt_ms) {
    elapsed_ms_ += dt_ms;
    while (elapsed_ms_ >= 1000) {
        elapsed_ms_ -= 1000;
        seconds_++;
        state_set("my_counter.seconds", seconds_);
    }
}
```

That's the entire module. The constructor passes the module name і priority
to `BaseModule`; lifecycle hooks do the work; `state_set` writes through
SharedState.

## Step 5 — Build і flash

```bash
idf.py build
idf.py -p COM15 flash monitor
```

You should see у boot log:

```
I (12345) ModuleManager: Registering my_counter (priority=NORMAL)
I (12350) MyCounter: Counter started
```

In the WebUI navigate to the **Counter** page (auto-generated з your `ui`
section). The "seconds" widget updates once per second.

## Step 6 — Document the module

A finished module does not "exist" for anyone else until it is described.
Add a **bilingual** reference page — both languages land in the same PR:

```
documentation/uk/03-framework-reference/modules/<name>.md
documentation/en/03-framework-reference/modules/<name>.md
```

Follow the page anatomy in
[docs-style.md](../06-contributing/docs-style.md). The typical skeleton
for a module (see [`simple_thermo.md`](../03-framework-reference/modules/simple_thermo.md)
and [`presence.md`](../03-framework-reference/modules/presence.md) as samples):

```markdown
# `<name>` — <one-line summary>

> 📖 **Українською:** [twin link]

<2-4 paragraphs: what is it? why does it exist? who should read this?>

## Behaviour        — what the module does (data flow)
## State keys       — table of the manifest state keys (type / access / description)
## WebUI / MQTT     — how it is configured (if it has a ui / mqtt section)
## Common pitfalls  — bugs/confusions the reader will hit
## Next steps       — 3-5 links the reader is likely to need next
## Source           — links to manifest.json, .cpp, tests
```

Then **register** the page in the index —
`documentation/{uk,en}/README.md`, the "03 — Framework reference"
table, with status ✅:

```markdown
| [modules/<name>.md](03-framework-reference/modules/<name>.md) | ✅ | <one-line purpose>. |
```

> Bilingual parity and a **Source** section are mandatory; broken
> cross-links fail review. Full rules — [docs-style.md](../06-contributing/docs-style.md).

## BaseModule API reference

### Constructor

```cpp
BaseModule(const char* name, modesp::ModulePriority priority);
```

`name` must match `"module"` field у `manifest.json`. `priority` selects the
init phase:

| Priority | Value | Phase | Use for |
|---|---|---|---|
| `CRITICAL` | 0 | 1 (first) | Error service, watchdog. |
| `HIGH` | 1 | 2 | WiFi, HAL, drivers, scenario engine. |
| `NORMAL` | 2 | 2 | Business logic (default). |
| `LOW` | 3 | 3 (last) | HTTP, WebSocket, datalogger. |

### Lifecycle hooks

All return their default if not overridden.

| Hook | Signature | Called | Notes |
|---|---|---|---|
| `on_init` | `virtual bool on_init()` | Once at startup | Return `false` to abort registration. |
| `on_update` | `virtual void on_update(uint32_t dt_ms)` | Every 10 ms | Hot path — must be non-blocking, < 1 ms typical. |
| `on_message` | `virtual void on_message(const etl::imessage& msg)` | When а message addressed to this module dispatches | Use sparingly — most communication через SharedState. |
| `on_stop` | `virtual void on_stop()` | Once at shutdown | Free non-trivial resources, flush queues. |

### State access

```cpp
// Write — typed overloads, all delegate до SharedState::set.
bool state_set(const char* key, int32_t value, bool track_change = true);
bool state_set(const char* key, float value, bool track_change = true);
bool state_set(const char* key, bool value, bool track_change = true);
bool state_set(const char* key, const char* value, bool track_change = true);

// Read — typed convenience із default fallback.
float   read_float(const char* key, float def = 0.0f) const;
int32_t read_int(const char* key, int32_t def = 0) const;
bool    read_bool(const char* key, bool def = false) const;

// Generic — returns std::optional із а variant. Use коли type unknown або polymorphic.
etl::optional<modesp::StateValue> state_get(const char* key) const;
```

**`track_change` flag:** default `true` triggers WebSocket delta-broadcast.
Set to `false` for silent updates (counters, fast-changing values that
spam WS).

Full SharedState semantics: [shared-state.md](shared-state.md) *(planned)*.

## Three-phase init ordering — what actually runs when

`ModuleManager::init_all` is called three times у `main.cpp`:

```cpp
// Phase 1 — CRITICAL priority modules
ESP_LOGI(TAG, "Phase 1: Initializing system services...");
app.modules().init_all(app.state());

// ... Wi-Fi, drivers, scenario engine registered здесь ...

// Phase 2 — HIGH + NORMAL priority modules
ESP_LOGI(TAG, "Phase 2: Initializing WiFi + business modules...");
app.modules().init_all(app.state());

// ... HTTP, WS registered здесь ...

// Phase 3 — LOW priority modules
ESP_LOGI(TAG, "Phase 3: Initializing HTTP + WebSocket...");
app.modules().init_all(app.state());
```

Each call iterates the registered modules and calls `on_init()` ONLY on
those still у `CREATED` state. That's how priority maps to phase: priority
`0`/CRITICAL gets initialised у the first `init_all` call (because nothing
higher exists і it's у CREATED state); priority `1`/HIGH і `2`/NORMAL у the
second call; priority `3`/LOW у the third.

**Practical rule:** if your module depends on something else being already
initialised, choose а higher priority value (later phase). If it provides
foundational service for other modules, choose lower.

## What goes у on_update vs on_init

**`on_init`:**
- Set initial state values.
- Read persisted settings (PersistService should have already restored them
  if your `state` keys had `persist: true`).
- Cache references / lookup tables що don't change.
- Print one ESP_LOGI line saying "initialised із <key parameters>".

**`on_update`:**
- The actual business logic.
- Read inputs (state keys written by other modules / drivers).
- Compute next state.
- Write outputs.

**Anti-patterns у `on_update`:**

- ❌ `vTaskDelay` / `sleep` — blocks the 100 Hz update loop, starves other modules.
- ❌ Heap allocation (`new`, `std::vector::push_back`, `std::string`) — на 100 Hz це leaks bytes per tick.
- ❌ NVS writes — synchronous I/O, ~5-50 ms each. Defer to module-level event handler або use `state_set` із `persist: true` (PersistService throttles).
- ❌ Heavy logging (>1 ESP_LOG per second per module) — UART floods, monitor lags.
- ❌ Reading complex JSON / parsing strings — pre-compute у `on_init`.

## Cross-module communication

Modules don't hold pointers до each other. Instead:

1. **Pure data flow** — module А writes `keyA`, module В reads `keyA`.
   Module В runs after А on the same tick because of declaration order
   (modules created у `module_instances.h` register у the order defined
   у `project.json`, which determines update order within а phase).

2. **Events / commands** — published by writing а state key, observed by
   reading it next tick. Edge detection через your own `prev_value_` member.

3. **Messages** (rare) — `ModuleManager::send_message(target, msg)` reaches
   `on_message`. Use коли а message has typed payload that doesn't fit
   а single state key.

4. **HTTP API** — external clients write keys через `POST /api/settings`,
   which `set_state` actions can too. Same mechanism, different actor.

> 💡 **Tip:** for а new project, default to pure data flow through SharedState.
> Add events / messages only коли cross-tick coordination requires it. Most
> module pairs need no explicit signaling.

## Reading sensor / actuator state

The `equipment` module owns HAL drivers. Sensor values land у keys like
`equipment.air_temp`, `equipment.evap_temp`. Actuator requests are written
to `equipment.req_compressor`, `equipment.req_fan` etc., і equipment maps
those to physical relays based on `bindings.json`.

Your business module reads sensor keys, writes actuator request keys.
Hardware is decoupled.

```cpp
void MyModule::on_update(uint32_t dt_ms) {
    float temp = read_float("equipment.air_temp", 0.0f);
    bool need_cooling = (temp > setpoint_);
    state_set("equipment.req_compressor", need_cooling);
}
```

Full HAL details: [components/modesp_hal.md](../03-framework-reference/components/modesp_hal.md)
*(planned)* і [hardware/bindings.md](../04-hardware/bindings.md) *(planned)*.

### A driver-owning module: `requires` declares a capability

A module like `equipment` doesn't read ready-made keys — it **owns** the
drivers. Such a module declares the peripherals it needs у the `requires`
array of its manifest. Each entry declares a **role** and a **capability** —
never a concrete driver (**R0.1**):

```json
// modules/equipment/manifest.json
"requires": [
  {"role": "air_temp",   "type": "sensor",   "capability": "temperature", "label": "Air temperature"},
  {"role": "room_temp",  "type": "sensor",   "capability": "temperature", "label": "Room temperature", "optional": true},
  {"role": "actuator_1", "type": "actuator", "capability": "relay_out",    "label": "Actuator 1",       "optional": true}
]
```

The `air_temp` role says "I need `temperature`" — and does not know who
supplies it: `ds18b20`, `ntc`, a BLE channel, or a future LoRa one. The
driver is picked у `bindings.json` (a `role` → `driver` → device binding),
not у module code. A role is declared only by the **owning** module that
consumes it (**R1.2**).

**On-device resolution by role name.** `DriverManager` resolves the
role↔driver binding; the module obtains a driver by **role name**, never by
driver type:

```cpp
modesp::ISensorDriver*   s = dm.find_sensor("air_temp");      // capability temperature
modesp::IActuatorDriver* a = dm.find_actuator("actuator_1");  // capability relay_out
```

`find_sensor(role)` / `find_actuator(role)` (у
[`driver_manager.h`](../../../components/modesp_hal/include/modesp/hal/driver_manager.h))
look up by the role string and return the abstract `ISensorDriver` /
`IActuatorDriver` interface. The module calls through that interface — it
never touches GPIO (**R3.3**). `EquipmentBase` does this у `bind_drivers()`
and thereafter works with roles only; the `capability` field is the SSOT
for role↔channel matching, and `optional: true` allows an absent binding.

> 💡 The capability is swappable: if a thermostat needs "temperature," the
> source (a wired sensor today, a BLE channel tomorrow) changes у
> `bindings.json` без editing module code. Don't hardcode a driver у a role
> or in a module — it defeats the whole abstraction. See
> [rules.md](../03-framework-reference/rules.md) (R0.1–R0.3) and
> [bindings.md](../04-hardware/bindings.md) *(planned)*.

## Generated headers — what's automatic

After `idf.py build`, `generated/` contains:

| File | Content |
|---|---|
| `module_includes.h` | `#include "your_module.h"` для each module у project.json. |
| `module_instances.h` | `static YourModule your_module;` declarations. |
| `module_register.h` | `manager.register_module(your_module)` calls у `modesp_register_modules(app)`. |
| `state_meta.h` | Constexpr table of all declared state keys, types, max-length. |
| `mqtt_topics.h` | String constants для each MQTT topic. |
| `features_config.h` | `#define` для each feature flag. |

You don't touch these by hand. The generator overwrites them every build з
the latest manifests.

## Testing your module

**Host build (preferred for fast iteration):**

```bash
cd tests/host
make MODULE=my_counter
./build/test_my_counter
```

Pattern: small `test_<name>.cpp` instantiates the module із а stub
SharedState, calls `on_init` і `on_update` repeatedly, asserts state values.
See [testing.md](../06-contributing/testing.md) *(planned)* for fixtures і
how-tos.

**On-target HIL:**

`tools/tests/test_hil.py` exercises the running firmware через HTTP API.
Add tests using `pytest` and `requests` ([test_hil.py reference](../../../tools/tests/test_hil.py)).

## Next steps

- **[shared-state.md](shared-state.md)** *(planned)* — deeper SharedState
  patterns (change tracking, optional reads, type validation).
- **[ui-widgets.md](ui-widgets.md)** *(planned)* — full widget reference із
  visual examples.
- **[mqtt.md](mqtt.md)** *(planned)* — wiring `mqtt.subscribe` keys і
  publish patterns.
- **[persistence.md](persistence.md)** *(planned)* — `persist: true` flag і
  PersistService.
- **[debugging.md](debugging.md)** *(planned)* — log inspection, state
  inspection via HTTP, common runtime issues.

## Existing modules to study source-first

- [`modules/simple_thermo/`](../../../modules/simple_thermo/) — ~55 LOC C++,
  shows hysteresis pattern, multi-key read/write. Best first read.
- [`modules/datalogger/`](../../../modules/datalogger/) — bigger module із
  features, multiple state keys, NVS-backed buffers. Read after basics.
- [`modules/equipment/`](../../../modules/equipment/) — bridges manifest до
  HAL drivers. Most coupled module — read once you understand SharedState
  і drivers.
