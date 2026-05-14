# Архітектура

> 📖 **In English:** [documentation/en/03-framework-reference/architecture.md](../../en/03-framework-reference/architecture.md)

ModESP v4 — layered C++ firmware framework для пристроїв класу ESP32. Ця
сторінка документує top-down архітектуру: які компоненти існують, як вони
залежать один від одного, що runs у якому task, і як manifest-driven
generation pipeline зв'язує усе разом при build time.

Якщо ви пишете modules, ви типово interact-ите з thin slice цієї
архітектури: BaseModule (modesp_core), state keys (SharedState), і
можливо drivers (modesp_hal). Ця сторінка — для розуміння substrate під
тими APIs.

## Layered overview

```
┌──────────────────────────────────────────────────────────────────┐
│                        ВАШ ПРОДУКТ                               │
│   modules/<your_module>/   modules/<your_recipe>/                │
│   (manifest.json + C++)    (manifest.json лише)                  │
├──────────────────────────────────────────────────────────────────┤
│                     ФРЕЙМВОРК ModESP                             │
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
│   │ modesp_json      (JSON parse/serialize wrapper)         │    │
│   │ modesp_core      (App, ModuleManager, SharedState)      │    │
│   └─────────────────────────────────────────────────────────┘    │
├──────────────────────────────────────────────────────────────────┤
│              ESP-IDF v5.5 + FreeRTOS + LittleFS                  │
└──────────────────────────────────────────────────────────────────┘
```

Higher layers depend на lower; lower layers know nothing про higher.
Domain modules sit на top; core живе на bottom.

## Component dependency map

| Component | Залежить від | Provides |
|---|---|---|
| `modesp_core` | (ETL, FreeRTOS) | `App`, `ModuleManager`, `SharedState`, `BaseModule`, types |
| `modesp_services` | core | Error, Watchdog, Config, Persist, Logger, SystemMonitor, nvs_helper |
| `modesp_hal` | core | HAL, DriverManager, IDriver interfaces |
| `modesp_net` | core, services, hal | WiFiService, HttpService, WsService |
| `modesp_mqtt` | core, services, net | MqttService, TLS, HA discovery |
| `modesp_aws` | core, services, net | AwsIotService (alternative до mqtt) |
| `modesp_json` | (jsmn) | JSON parse helpers |
| `modesp_scenario` | core, services | Engine, ActionRegistry, ContinuousRegistry, IStateBackend |
| `modules/equipment` | core, hal, services | Equipment Manager (sensors → state, state → actuators) |
| `modules/datalogger` | core, services | Channel logging, event logging |
| `modules/simple_thermo` | core | Reference business module |

Build system enforces these через `idf_component_register(REQUIRES ...)`.

## Application об'єкт (`App`)

`modesp_core` exposes один application-level singleton — `modesp::App`.
Створюється раз у `main.cpp`:

```cpp
auto& app = modesp::App::instance();
app.init();             // construct SharedState, ModuleManager
// ... register modules ...
app.modules().init_all(app.state());     // calls on_init на registered modules
// ... later ...
app.modules().update_all(dt_ms);         // calls on_update на every tick
```

App owns:
- `SharedState state_` — typed key-value store
  ([shared-state.md](../02-module-author-guide/shared-state.md)).
- `ModuleManager modules_` — registry BaseModule instances.

`app.state()` і `app.modules()` дають references що survive program
lifetime.

## ModuleManager — registration і tick driving

`ModuleManager` тримає fixed-capacity array `BaseModule*` references (no
ownership — modules static у main.cpp). Lifecycle методи drive усі
registered modules:

```cpp
class ModuleManager {
public:
    bool register_module(BaseModule& m);     // adds до registry
    bool init_all(SharedState& state);       // calls on_init() на кожному CREATED module
    void update_all(uint32_t dt_ms);         // calls on_update() на кожному INITIALISED module
    void on_message(const etl::imessage& m); // dispatches до addressed module
    void stop_all();                         // calls on_stop()
    // ...
};
```

### Three-phase init

`init_all` викликається ТРИ рази у main.cpp:

```cpp
// Phase 1 — register CRITICAL modules (error, watchdog, config, persist, monitor)
app.modules().register_module(error_service);
// ... more CRITICAL ...
app.modules().init_all(app.state());           // initialises CRITICAL лише

// Phase 2 — register HIGH і NORMAL (wifi, hal, drivers, scenario, business modules)
app.modules().register_module(wifi_service);
// ... more HIGH/NORMAL ...
app.modules().init_all(app.state());           // initialises HIGH і NORMAL

// Phase 3 — register LOW (http, ws, datalogger)
app.modules().register_module(http_service);
app.modules().init_all(app.state());           // initialises LOW
```

`init_all` skips modules що already у `INITIALISED` state, тому multiple
calls work as expected. Modules повертають `false` з `on_init` якщо не
змогли initialise — вони йдуть у `FAILED` state і не tick-аються.

Order у межах phase — **registration order**. `update_all` ticks
modules у тому самому order кожного виклику.

### 100 Hz tick loop

Після init, main.cpp runs:

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

10 мс tick = 100 Hz. Кожен module's `on_update(dt_ms)` runs кожен tick у
registration order. Total time у одному tick must stay < ~5 мс across усіх
modules щоб avoid watchdog resets і WS broadcast jitter.

## SharedState — data backbone

Один process-wide `SharedState` живе у App. Це typed, mutex-protected
key-value store (ETL `unordered_map<StateKey, StateValue>` із bounded
capacity з `state_meta.h`). Modules read і write keys; жодних прямих
module-to-module pointers.

Три accessor styles:

1. **Через BaseModule helpers** (найпоширеніше):
   ```cpp
   float t = read_float("equipment.air_temp", 0.0f);
   state_set("my_module.output", true);
   ```
2. **Через `app.state()` напряму** (для HTTP handlers, recipe actions):
   ```cpp
   modesp::StateValue v;
   if (state.get("key", out)) { ... }
   ```
3. **Через `IStateBackend`** (scenario engine):
   ```cpp
   class SharedStateBackend : public IStateBackend { ... };
   ```

Повний reference: [shared-state.md](../02-module-author-guide/shared-state.md).

## Згенеровані headers і build pipeline

Фреймворк — **manifest-driven**: `tools/generate_ui.py` runs як pre-build
CMake step і generates C++ headers з маніфестів:

| Generated file | Content | Used by |
|---|---|---|
| `state_meta.h` | Constexpr table declared state keys + types + max count | SharedState, PersistService, MQTT topics |
| `mqtt_topics.h` | Topic string constants per state key | MqttService |
| `module_includes.h` | `#include "<module>.h"` per project.json | main.cpp |
| `module_instances.h` | Static `<Module> name;` declarations | main.cpp |
| `module_register.h` | `manager.register_module(<name>)` calls | main.cpp's `modesp_register_modules(app)` |
| `modules.cmake` | List module components для CMake REQUIRES | main/CMakeLists.txt |
| `display_screens.h` | LCD widget configs | display drivers (якщо present) |
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

`compile_scenario.py` runs як separate step для recipe modules,
producing `.modr` blobs.

## FreeRTOS task topology

| Task | Priority | Stack | Purpose |
|---|---|---|---|
| `main` | low (idle equivalent після boot) | 8 KB | 100 Hz update loop. Більшість modules tick тут. |
| `app_main` | (initial) | 4 KB | Boot setup, потім transfers до `main`. |
| WiFi tasks | various (ESP-IDF) | 4-8 KB | WiFi stack, lwIP, network packet handling. |
| httpd task | medium | 8 KB | HTTP request handlers run тут, НЕ на main task. |
| WebSocket worker | medium | 4 KB | WS frames |
| MQTT task | medium | 6 KB | esp-mqtt client |
| ESP-IDF system tasks | various | various | IPC, timers, ESP timer, тощо. |

Modules tick на main task — отже single-threaded з їхньої перспективи.
HTTP handlers і MQTT callbacks run на різних tasks, тому вони ПОВИННІ
йти через SharedState (mutex-protected) — не poke module state напряму.

## State persistence і recovery

Два рівні:

1. **PersistService** (modesp_services) — wires state keys з
   `persist: true` до NVS. Transparent. 5-second debounce. Restore
   happens перед any module's `on_init`.
2. **Scenario engine NvsObserver** (modesp_scenario) — token-based
   per-instance recovery для scenario runtime state. Magic `SCTK`,
   throttled writes, immediate save при main-track phase changes.

NVS partitions:
- `nvs` (24 KB) — settings, WiFi credentials, ROM-protected.
- `otadata` — OTA selector.

Larger blobs (LittleFS / `data/`) — read-only after flash за замовчуванням;
OTA updates whole partition atomically.

## OTA flow (top-level)

Dual-image scheme з automatic rollback:

1. Active firmware running у `ota_0`.
2. New firmware uploaded через HTTP `/api/ota/upload` → goes до `ota_1`.
3. Reboot із `ota_1` як active.
4. `app_main` checks "pending-verify" state, gives нову firmware 60
   секунд щоб mark себе stable.
5. Stable mark = HTTP `/api/ota/confirm` або automatic при watchdog
   non-trigger.
6. Otherwise rollback до `ota_0`.

Повний deployment workflow у [04-hardware/ota.md](../04-hardware/ota.md)
*(planned)*.

## Network і external API

`modesp_net` ships:

- **WiFiService:** STA mode із AP fallback (no SSID found → device opens
  AP для credential entry). mDNS hostname `modesp-<deviceid>.local`.
- **HttpService:** esp_http_server із ~30 REST endpoints (state, settings,
  WiFi config, OTA, scenarios, modules info, board, bindings, тощо).
- **WsService:** WebSocket що broadcasts state changes до connected
  clients кожні ~500 мс (або full snapshot при overflow).

Optional cloud backends (mutually exclusive, Kconfig choice):
- `modesp_mqtt`: generic MQTT, optionally TLS, з HA discovery.
- `modesp_aws`: AWS IoT Core з cert-based auth.

Обидва implement той самий publish/subscribe контракт з module author's
view: declare у manifest, фреймворк wires the rest.

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

Engine — regular BaseModule registered у Phase 2. Див.
[scenario-engine/](scenario-engine/) для deep dives.

## Що ви типово не торкаєтесь напряму

- `app_main`, ESP-IDF tasks — pure boot scaffolding у main.cpp.
- `modesp_core::SharedState` напряму — use BaseModule helpers.
- HTTP / WebSocket handlers — generate UI через manifests.
- MQTT client — declare у manifest.
- Driver instances — bindings.json wires them; ви пишете modules що
  читають `equipment.<role>`.

## Що ви customise часто

- Module manifests — declare state keys, UI, MQTT.
- Module C++ classes — business logic у `on_update`.
- Recipe scenario sections — phase-driven processes.
- `bindings.json` per deployment — match hardware until ви маєте proper
  board variant.
- `project.json` — які modules build-аться у це firmware.

## Що далі

- **[components/modesp_core.md](components/modesp_core.md)** — detailed
  core API reference: SharedState, BaseModule, ModuleManager.
- **[components/modesp_services.md](components/modesp_services.md)**
  *(planned)* — services internals.
- **[components/modesp_hal.md](components/modesp_hal.md)** *(planned)* —
  HAL і DriverManager.
- **[scenario-engine/](scenario-engine/)** — scenario engine deep dive.
- **[02-module-author-guide/overview.md](../02-module-author-guide/overview.md)**
  — back to module author's perspective.

## Source roots

- [`components/modesp_core/`](../../../components/modesp_core/) — core
  types і App.
- [`main/main.cpp`](../../../main/main.cpp) — boot sequence, init phases,
  tick loop.
- [`project.json`](../../../project.json) — module manifest.
- [`tools/generate_ui.py`](../../../tools/generate_ui.py) — build-time
  generator.
