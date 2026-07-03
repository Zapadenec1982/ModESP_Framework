# Architecture

> 📖 **Українською:** [documentation/uk/03-framework-reference/architecture.md](../../uk/03-framework-reference/architecture.md)

ModESP v4 is а layered C++ firmware framework for ESP32-class devices.
This page documents the top-down architecture: which components exist,
how they depend on each other, what runs у what task, і how the
manifest-driven generation pipeline ties everything together at build time.

If you're writing modules, you typically interact із а thin slice of це
architecture: BaseModule (modesp_core), state keys (SharedState), і
maybe drivers (modesp_hal). This page is для understanding the substrate
beneath those APIs.

## Layered overview

```
┌──────────────────────────────────────────────────────────────────┐
│                        YOUR PRODUCT                              │
│   modules/<your_module>/   modules/<your_recipe>/                │
│   (manifest.json + C++)    (manifest.json only)                  │
├──────────────────────────────────────────────────────────────────┤
│                     ModESP FRAMEWORK                             │
│                                                                  │
│   modules/equipment    modules/datalogger    modules/simple_thermo│
│        ↑                                                         │
│   ┌─────────────────────────────────────────────────────────┐    │
│   │ modesp_scenario  (engine, FSM, actions, continuous)     │    │
│   │ modesp_services  (Config, Persist, Error, Watchdog, Log)│    │
│   │ modesp_hal       (HAL, DriverManager, IDriver)          │    │
│   │ modesp_net       (WiFi, HTTP server, WebSocket)         │    │
│   │ modesp_mqtt      (MQTT client, TLS, HA discovery)       │    │
│   │ modesp_aws       (AWS IoT alternative cloud backend)    │    │
│   │ modesp_core      (App, ModuleManager, SharedState)      │    │
│   └─────────────────────────────────────────────────────────┘    │
├──────────────────────────────────────────────────────────────────┤
│              ESP-IDF v5.5 + FreeRTOS + LittleFS                  │
└──────────────────────────────────────────────────────────────────┘
```

Higher layers depend on lower ones; lower layers know nothing about
higher. Domain modules sit at the top; the core lives at the bottom.

## Component dependency map

| Component | Depends on | Provides |
|---|---|---|
| `modesp_core` | (ETL, FreeRTOS) | `App`, `ModuleManager`, `SharedState`, `BaseModule`, types |
| `modesp_services` | core | Error, Watchdog, Config, Persist, Logger, SystemMonitor, nvs_helper |
| `modesp_hal` | core | HAL, DriverManager, IDriver interfaces |
| `modesp_net` | core, services, hal | WiFiService, HttpService, WsService |
| `modesp_mqtt` | core, services, net | MqttService, TLS, HA discovery |
| `modesp_aws` | core, services, net | AwsIotService (alternative to mqtt) |
| `modesp_scenario` | core, services | Engine, ActionRegistry, ContinuousRegistry, IStateBackend |
| `modules/equipment` | core, hal, services | Equipment Manager (sensors → state, state → actuators) |
| `modules/datalogger` | core, services | Channel logging, event logging |
| `modules/simple_thermo` | core | Reference business module |

The build system enforces these via `idf_component_register(REQUIRES ...)`.

## The application object (`App`)

`modesp_core` exposes one application-level singleton — `modesp::App`.
Created once у `main.cpp`:

```cpp
auto& app = modesp::App::instance();
app.init();             // construct SharedState, ModuleManager
// ... register modules ...
app.modules().init_all(app.state());     // calls on_init on registered modules
// ... later ...
app.modules().update_all(dt_ms);         // calls on_update on every tick
```

App owns:
- `SharedState state_` — the typed key-value store ([shared-state.md](../02-module-author-guide/shared-state.md)).
- `ModuleManager modules_` — registry of BaseModule instances.

`app.state()` і `app.modules()` give references that survive program
lifetime.

## ModuleManager — registration і tick driving

`ModuleManager` holds а fixed-capacity array of `BaseModule*` references
(no ownership — modules are static у main.cpp). Lifecycle methods drive
all registered modules:

```cpp
class ModuleManager {
public:
    bool register_module(BaseModule& m);     // adds to registry
    bool init_all(SharedState& state);       // calls on_init() on each CREATED module
    void update_all(uint32_t dt_ms);         // calls on_update() on each INITIALISED module
    void on_message(const etl::imessage& m); // dispatches to addressed module
    void stop_all();                         // calls on_stop()
    // ...
};
```

### Three-phase init

`init_all` is called THREE times у main.cpp:

```cpp
// Phase 1 — register CRITICAL modules (error, watchdog, config, persist, monitor)
app.modules().register_module(error_service);
// ... more CRITICAL ...
app.modules().init_all(app.state());           // initialises CRITICAL only

// Phase 2 — register HIGH and NORMAL (wifi, hal, drivers, scenario, business modules)
app.modules().register_module(wifi_service);
// ... more HIGH/NORMAL ...
app.modules().init_all(app.state());           // initialises HIGH і NORMAL

// Phase 3 — register LOW (http, ws, datalogger)
app.modules().register_module(http_service);
app.modules().init_all(app.state());           // initialises LOW
```

`init_all` skips modules already у `INITIALISED` state, so multiple calls
work as expected. Modules return `false` from `on_init` if they couldn't
initialise — they go into `FAILED` state і don't tick.

Order within а phase is **registration order**. `update_all` ticks
modules у the same order each call.

### The 100 Hz tick loop

After init, main.cpp runs:

```cpp
while (true) {
    uint32_t now = millis();
    uint32_t dt_ms = now - last_tick_;
    last_tick_ = now;
    app.modules().update_all(dt_ms);
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(10));
}
```

10 ms tick = 100 Hz. Every module's `on_update(dt_ms)` runs each tick у
registration order. Total time у one tick must stay < ~5 ms across all
modules to avoid watchdog resets і WS broadcast jitter.

## SharedState — the data backbone

One process-wide `SharedState` lives у App. It's а typed,
mutex-protected key-value store (ETL `unordered_map<StateKey, StateValue>`
із bounded capacity from `state_meta.h`). Modules read і write keys; no
direct module-to-module pointers.

Three accessor styles:

1. **Through BaseModule helpers** (most common):
   ```cpp
   float t = read_float("equipment.air_temp", 0.0f);
   state_set("my_module.output", true);
   ```
2. **Through `app.state()` directly** (для HTTP handlers, recipe actions):
   ```cpp
   modesp::StateValue v;
   if (state.get("key", out)) { ... }
   ```
3. **Through `IStateBackend`** (scenario engine):
   ```cpp
   class SharedStateBackend : public IStateBackend { ... };
   ```

Full reference: [shared-state.md](../02-module-author-guide/shared-state.md).

## Generated headers і build pipeline

The framework is **manifest-driven**: `tools/generate_ui.py` runs as а
pre-build CMake step і generates C++ headers from manifests:

| Generated file | Content | Used by |
|---|---|---|
| `state_meta.h` | Constexpr table of declared state keys + types + max count | SharedState, PersistService, MQTT topics |
| `mqtt_topics.h` | Topic string constants per state key | MqttService |
| `module_includes.h` | `#include "<module>.h"` per project.json | main.cpp |
| `module_instances.h` | Static `<Module> name;` declarations | main.cpp |
| `module_register.h` | `manager.register_module(<name>)` calls | main.cpp's `modesp_register_modules(app)` |
| `modules.cmake` | List of module components for CMake REQUIRES | main/CMakeLists.txt |
| `display_screens.h` | LCD widget configs | display drivers (if present) |
| `datalogger_channels.h` | Channel ID mapping | datalogger |
| `datalogger_events.h` | Event ID mapping | datalogger |
| `features_config.h` | Compile-time feature flags | various |

LittleFS-bundled artifacts (`data/`):

| File | Content |
|---|---|
| `data/ui.json` | Merged WebUI schema served at `/api/ui` |
| `data/board.json` | Selected board's hardware capabilities |
| `data/bindings.json` | Selected board's driver bindings |
| `data/scenarios/*.modr` | Compiled recipe binaries |
| `data/www/*` | Pre-built Svelte SPA |
| `data/www/i18n/*.json` | Translation packs (UK/EN/DE/PL) |

`compile_scenario.py` runs as а separate step for recipe modules,
producing the `.modr` blobs.

## FreeRTOS task topology

| Task | Priority | Stack | Purpose |
|---|---|---|---|
| `main` | low (idle equivalent після boot) | 8 KB | The 100 Hz update loop. Most modules tick here. |
| `app_main` | (initial) | 4 KB | Boot setup, then transfers до `main`. |
| WiFi tasks | various (ESP-IDF) | 4-8 KB | WiFi stack, lwIP, network packet handling. |
| httpd task | medium | 8 KB | HTTP request handlers run here, NOT on main task. |
| WebSocket worker | medium | 4 KB | WS frames |
| MQTT task | medium | 6 KB | esp-mqtt client |
| ESP-IDF system tasks | various | various | IPC, timers, ESP timer, etc. |

Modules tick on the main task — therefore single-threaded від their own
perspective. HTTP handlers і MQTT callbacks run on different tasks, so
they MUST go through SharedState (mutex-protected) — not poke module state
directly.

## State persistence і recovery

Two levels:

1. **PersistService** (modesp_services) — wires state keys із `persist: true`
   до NVS. Transparent. 5-second debounce. Restore happens before any
   module's `on_init`.
2. **Scenario engine NvsObserver** (modesp_scenario) — token-based
   per-instance recovery for scenario runtime state. Magic `SCTK`,
   throttled writes, immediate save on main-track phase changes.

NVS partitions:
- `nvs` (24 KB) — settings, WiFi credentials, ROM-protected.
- `otadata` — OTA selector.

Larger blobs (LittleFS / `data/`) — read-only after flash by default; OTA
updates the whole partition atomically.

## OTA flow (top-level)

Dual-image scheme із automatic rollback:

1. Active firmware running у `ota_0`.
2. New firmware uploaded via HTTP `/api/ota/upload` → goes to `ota_1`.
3. Reboot із `ota_1` як active.
4. `app_main` checks "pending-verify" state, gives the new firmware
   60 seconds to mark itself stable.
5. Stable mark = HTTP `/api/ota/confirm` або automatic on watchdog
   non-trigger.
6. Otherwise rollback до `ota_0`.

Full deployment workflow у [04-hardware/ota.md](../04-hardware/ota.md)
*(planned)*.

## Network і external API

`modesp_net` ships:

- **WiFiService:** STA mode із AP fallback (no SSID found → device opens
  an AP for credential entry). mDNS hostname `modesp-<deviceid>.local`.
- **HttpService:** esp_http_server із ~30 REST endpoints (state, settings,
  WiFi config, OTA, scenarios, modules info, board, bindings, etc.).
- **WsService:** WebSocket що broadcasts state changes to connected clients
  every ~500 ms (or full snapshot on overflow).

Optional cloud backends (mutually exclusive, Kconfig choice):
- `modesp_mqtt`: generic MQTT, optionally TLS, із HA discovery.
- `modesp_aws`: AWS IoT Core із cert-based auth.

Both implement the same publish/subscribe contract from а module author's
view: declare у manifest, framework wires the rest.

## Scenario engine pipeline

```
modules/<recipe>/manifest.json
            │
            ▼ compile_scenario.py (build-time)
            │
data/scenarios/<recipe>.modr (binary)
            │
            ▼ engine.load_path (runtime)
            │
modesp::scenario::Engine ticks at 100 Hz
            │
            ├── ActionRegistry (lookups action handlers by hash)
            ├── ContinuousRegistry (factories для PID/hysteresis/ramp)
            ├── ResourceArbiter (ISA-88 §5.3 atomic claims)
            └── IEngineObserver (mirror writes, NVS persist)
            │
            ▼
SharedState mirror keys updated
WebUI / MQTT see changes
```

Engine is а regular BaseModule registered у Phase 2. See
[scenario-engine/](scenario-engine/) for deep dives.

## What you typically don't touch directly

- `app_main`, ESP-IDF tasks — pure boot scaffolding у main.cpp.
- `modesp_core::SharedState` directly — use BaseModule helpers.
- HTTP / WebSocket handlers — generate UI through manifests.
- MQTT client — declare у manifest.
- Driver instances — bindings.json wires them; you write modules що read
  `equipment.<role>`.

## What you customise often

- Module manifests — declare state keys, UI, MQTT.
- Module C++ classes — business logic у `on_update`.
- Recipe scenario sections — phase-driven processes.
- `bindings.json` per deployment — match hardware until you have proper
  board variant.
- `project.json` — which modules build into this firmware.

## Next steps

- **[components/modesp_core.md](components/modesp_core.md)** —
  detailed core API reference: SharedState, BaseModule, ModuleManager.
- **[components/modesp_services.md](components/modesp_services.md)**
  *(planned)* — services internals.
- **[components/modesp_hal.md](components/modesp_hal.md)** *(planned)* —
  HAL і DriverManager.
- **[scenario-engine/](scenario-engine/)** — scenario engine deep dive.
- **[02-module-author-guide/overview.md](../02-module-author-guide/overview.md)**
  — back to the module author's perspective.

## Source roots

- [`components/modesp_core/`](../../../components/modesp_core/) — core types і App.
- [`main/main.cpp`](../../../main/main.cpp) — boot sequence, init phases,
  tick loop.
- [`project.json`](../../../project.json) — module manifest.
- [`tools/generate_ui.py`](../../../tools/generate_ui.py) — build-time
  generator.
