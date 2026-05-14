# `modesp_services` — Config, Persist, Error, Watchdog, Logger, SystemMonitor

> 📖 **Українською:** [documentation/uk/03-framework-reference/components/modesp_services.md](../../../uk/03-framework-reference/components/modesp_services.md)

`modesp_services` ships the system services every firmware needs regardless
of business logic: configuration loading, NVS persistence, error reporting,
watchdog, log buffering, system monitoring, і OTA handling. These run at
`CRITICAL` priority (or close to it), initialising before any business
module.

This page documents what each service provides, how they wire to
SharedState, і when business modules need to interact із them directly
(rarely — most integration is transparent).

REQUIRES: `modesp_core`, `nvs_flash`, ESP-IDF helpers.

## Services overview

| Service | Priority | Purpose |
|---|---|---|
| **ErrorService** | CRITICAL | Central error reporting, fault tracking. |
| **WatchdogService** | CRITICAL | Feeds ESP-IDF task watchdog from main task. |
| **ConfigService** | CRITICAL | Reads `board.json` і `bindings.json` from LittleFS. |
| **PersistService** | CRITICAL | Mirrors persisted state keys to NVS. |
| **LoggerService** | CRITICAL | RAM ring buffer of recent ESP_LOG lines (для `/api/log`). |
| **SystemMonitor** | CRITICAL | Heap, uptime, task stats, free-RAM tracking. |
| **OtaHandler** | (helper) | Not а BaseModule — utility called by HTTP / MQTT. |

Plus the `nvs_helper` namespace — а thin C++ wrapper over ESP-IDF's NVS
API (`nvs_*` functions). Used internally by PersistService і available
для modules що need direct NVS access.

## `ErrorService` — central fault reporting

```cpp
#include "modesp/error_service.h"

class ErrorService : public modesp::BaseModule {
public:
    ErrorService();

    // API used by other modules
    void report_fault(ErrorCode code, const char* details);
    void clear_fault(ErrorCode code);
    bool has_fault() const;
    ErrorCode last_fault() const;
};

enum class ErrorCode : uint16_t {
    // Hardware
    SENSOR_FAILED          = 100,
    ACTUATOR_FAILED        = 110,
    DRIVER_NOT_HEALTHY     = 120,

    // Network
    WIFI_DISCONNECTED      = 200,
    MQTT_BROKER_LOST       = 210,

    // Filesystem
    NVS_WRITE_FAILED       = 300,
    LITTLEFS_MOUNT_FAILED  = 310,

    // ... etc.
};
```

State keys exposed:

| Key | Type | Notes |
|---|---|---|
| `error_service.last_code` | int | Numeric code of most recent fault. |
| `error_service.last_details` | string | Free-form text supplied by reporter. |
| `error_service.active_count` | int | Number of unresolved faults. |
| `error_service.cleared_count` | int | Cumulative cleared (для diagnostics). |

Business modules call `report_fault` from their `on_update` when something
goes wrong. Module status (`equipment.<role>_ok = false`, for example)
flows through SharedState; ErrorService аккумулирует higher-level
fault summary for UI / MQTT publishing.

## `WatchdogService` — keeps the device alive

```cpp
class WatchdogService : public modesp::BaseModule {
public:
    WatchdogService();
};
```

Subscribes itself to ESP-IDF's task watchdog у `on_init` і calls
`esp_task_wdt_reset()` from `on_update`. If the main task hangs (any
module's `on_update` takes too long), watchdog triggers а system reboot
after the configured timeout (~5 seconds default).

No state keys, no API. Module exists primarily to prove the framework
is ticking properly. If you see watchdog resets у logs, run а profiler —
some module is exceeding its tick budget.

## `ConfigService` — load board і bindings

```cpp
#include "modesp/services/config_service.h"

class ConfigService : public modesp::BaseModule {
public:
    bool load_from_littlefs(const char* path = "/data");
    const BoardConfig& board() const;
    const BindingTable& bindings() const;
    bool reload();
};
```

Reads `data/board.json` і `data/bindings.json` at `on_init`. Provides
parsed structures that other components (HAL, Equipment Manager, drivers)
consume.

`reload()` re-reads files. Used after OTA file replacement или WebUI
bindings editor (planned). Currently invoked only від HTTP `/api/bindings`
POST handler.

State keys:

| Key | Notes |
|---|---|
| `config.board_name` | Board identifier from `board.json::board`. |
| `config.bindings_count` | Number of bindings entries. |

## `PersistService` — NVS state mirror

```cpp
#include "modesp/services/persist_service.h"

class PersistService : public modesp::BaseModule {
public:
    PersistService();

    static constexpr uint32_t DEBOUNCE_MS = 5000;
    static constexpr const char* NVS_NAMESPACE = "persist";

    // Internal — wired through SharedState::set_persist_callback.
    static void on_state_changed(const StateKey&, const StateValue&, void*);
};
```

Loop:
1. On `on_init`, scan `state_meta.h` для all keys із `persist: true`.
   Read each from NVS namespace `"persist"` і set the value у SharedState.
2. Register а callback із `SharedState::set_persist_callback`. Callback
   fires on every tracked write.
3. Callback identifies persisted keys і schedules а debounced NVS write
   (5 seconds of "no further change" → flush to flash).

Full reading: [persistence.md](../../02-module-author-guide/persistence.md).

API surface для modules is **just the `persist: true` flag у manifest**.
You don't call PersistService directly.

## `LoggerService` — recent log buffer

```cpp
#include "modesp/logger_service.h"

class LoggerService : public modesp::BaseModule {
public:
    LoggerService();

    // Internal — captures ESP_LOG vprintf hook.
    static int esp_log_vprintf_hook(const char* fmt, va_list args);

    // Public — retrieve recent lines for /api/log.
    void copy_recent(char* out, size_t cap, size_t* out_len);
};
```

Hooks ESP-IDF's `esp_log_set_vprintf` і captures recent log lines into а
RAM ring buffer (default ~4 KB, last ~100 lines). HTTP `/api/log` serves
це buffer для remote diagnostics — useful when UART unavailable.

State keys:

| Key | Notes |
|---|---|
| `logger.line_count` | Number of lines captured since boot. |
| `logger.buffer_used_bytes` | Current bytes у the ring buffer. |

No filtering / level controls у MVP. Stage 1.5 may add per-tag level
adjustments through `/api/log/config`.

## `SystemMonitor` — heap і uptime stats

```cpp
#include "modesp/system_monitor.h"

class SystemMonitor : public modesp::BaseModule {
public:
    SystemMonitor(ErrorService& errors);
};
```

Periodically (every ~1 second) writes diagnostic state keys:

| Key | Notes |
|---|---|
| `system.free_heap` | `esp_get_free_heap_size()` bytes. |
| `system.min_free_heap` | `esp_get_minimum_free_heap_size()`. |
| `system.uptime_s` | Seconds since boot. |
| `system.reset_reason` | One of "POWERON" / "BROWNOUT" / "WDT" / "EXTERNAL" etc. |
| `system.task_count` | Active FreeRTOS tasks. |
| `system.cpu_freq_mhz` | Current CPU frequency. |

WebUI shows these on the System page. MQTT publishes if listed у
`mqtt.publish`.

If `min_free_heap < threshold`, reports `ErrorCode::LOW_MEMORY` to
ErrorService. Threshold is Kconfig-tunable (default ~16 KB).

## `OtaHandler` — firmware updates

```cpp
#include "modesp/services/ota_handler.h"

class OtaHandler {
public:
    // Called from HTTP /api/ota/upload handler.
    bool begin(size_t total_size);
    bool write(const uint8_t* data, size_t len);
    bool finish(bool& valid);

    bool confirm_running_partition();    // mark current image stable
    bool rollback();                      // boot з previous partition
};
```

Not а BaseModule — utility used by `modesp_net::HttpService`'s
`/api/ota/*` handlers. State keys у `_ota.*` namespace (underscore prefix
marks framework-internal):

| Key | Notes |
|---|---|
| `_ota.status` | "idle" / "downloading" / "verifying" / "rebooting" / "rolled_back" |
| `_ota.progress` | Bytes transferred. |
| `_ota.error` | Error string if status = "failed". |
| `_ota.version` | Active firmware version. |
| `_ota.partition` | Active partition name. |
| `_ota.date` / `_ota.idf` | Build metadata. |

Full OTA flow: [04-hardware/ota.md](../../04-hardware/ota.md) *(planned)*.

## `nvs_helper` — raw NVS API

For when `persist: true` isn't enough — large blobs, custom encoding,
own namespace.

```cpp
#include "modesp/services/nvs_helper.h"
namespace modesp::nvs_helper {

bool init();
bool erase_all();

// Typed
bool read_i32(const char* ns, const char* key, int32_t& out);
bool read_float(const char* ns, const char* key, float& out);
bool read_bool(const char* ns, const char* key, bool& out);
bool read_str(const char* ns, const char* key, char* out, size_t max_len);
bool read_blob(const char* ns, const char* key, void* out, size_t max_len, size_t& out_len);

bool write_i32(const char* ns, const char* key, int32_t value);
bool write_float(const char* ns, const char* key, float value);
bool write_bool(const char* ns, const char* key, bool value);
bool write_str(const char* ns, const char* key, const char* value);
bool write_blob(const char* ns, const char* key, const void* data, size_t len);

bool erase_key(const char* ns, const char* key);
bool erase_namespace(const char* ns);

// Batch (faster для many writes)
struct nvs_handle_t* batch_open(const char* ns, bool readonly);
bool batch_read_*(...);   // same но on open handle
bool batch_write_*(...);
bool batch_close(struct nvs_handle_t* h);

}
```

Use cases у the framework itself:
- `scnstate` namespace (scenario engine tokens).
- `auth` namespace (admin credentials).
- `time` namespace (NTP / timezone).
- `seqstate` (legacy scenario engine — being phased out).

Use а namespace OTHER than `"persist"` to avoid conflict із the
auto-managed PersistService keys.

## Service registration у main.cpp

```cpp
static modesp::ErrorService    error_service;
static modesp::ConfigService   config_service;
static modesp::PersistService  persist_service;
static modesp::LoggerService   logger_service;
static modesp::SystemMonitor   system_monitor(error_service);
// WatchdogService instantiated dynamically because it needs ModuleManager ref:
// (see main.cpp's app_main)

// Phase 1 registration:
app.modules().register_module(error_service);
app.modules().register_module(logger_service);
app.modules().register_module(config_service);

persist_service.set_state(&app.state());
app.modules().register_module(persist_service);

app.modules().register_module(system_monitor);
app.modules().register_module(watchdog_service);

app.modules().init_all(app.state());   // Phase 1 complete
```

## What business modules need to know

For 95% of business modules, you can ignore це component entirely.
Things to know у case they bite:

1. **`persist: true`** у your manifest → PersistService handles storage.
2. **Faults you detect** → call `error_service.report_fault(code, "...")`
   to surface to UI / MQTT.
3. **Long operations** → watchdog will reset you if `on_update` exceeds
   ~5 s. Break work into ticks.
4. **NVS writes** → use `persist: true` (5 s debounce) или explicit
   `nvs_helper::write_*` for one-shot stuff. Don't write раз а tick.
5. **`/api/log`** → your ESP_LOG messages are visible via HTTP. Don't log
   sensitive data.

## Common patterns

### Module reports а fault і recovers

```cpp
void Module::on_update(uint32_t dt_ms) {
    bool sensor_ok = read_bool("equipment.air_temp_ok", false);

    if (!sensor_ok && !was_faulted_) {
        error_service_->report_fault(ErrorCode::SENSOR_FAILED,
                                     "air_temp sensor unhealthy");
        was_faulted_ = true;
    } else if (sensor_ok && was_faulted_) {
        error_service_->clear_fault(ErrorCode::SENSOR_FAILED);
        was_faulted_ = false;
    }
}
```

Edge-trigger pattern — report once, clear once. Don't spam on every tick.

### Custom NVS namespace for large data

```cpp
bool Module::save_calibration_table(const CalibTable& tbl) {
    return modesp::nvs_helper::write_blob(
        "my_module", "calib_v1", &tbl, sizeof(tbl));
}

bool Module::load_calibration_table(CalibTable& out) {
    size_t len = 0;
    return modesp::nvs_helper::read_blob(
        "my_module", "calib_v1", &out, sizeof(out), len);
}
```

Encapsulate NVS access у member functions; call from `on_init` (load) і
а deliberate save trigger (е.g., `apply_calibration` action). Never у
`on_update`.

## Common pitfalls

**`PersistService` doesn't restore on first boot:** that's correct —
no value у NVS means use the default. Make sure your `state` declaration
has а `default` field for fresh-boot fallback.

**`ConfigService::reload()` from `on_update`:** parses JSON synchronously,
takes ~50 ms. Block. Use only від а one-shot HTTP handler.

**Heap leak with `nvs_helper::read_blob`:** function fills а buffer you
provide, doesn't allocate. But if you allocate that buffer із `new`,
remember to delete. Use stack buffers when possible.

**Reset reason "WDT" persists across boots:** that's expected — last reset
reason ship through reboot. If you see it after а power cycle, it means
the previous run hit watchdog. Capture а log dump via `/api/log` if
reproducible.

## Next steps

- **[components/modesp_hal.md](modesp_hal.md)** *(planned)* — HAL і
  DriverManager.
- **[components/modesp_net.md](modesp_net.md)** *(planned)* — WiFi і
  HTTP services that consume ConfigService і OtaHandler.
- **[02-module-author-guide/persistence.md](../../02-module-author-guide/persistence.md)**
  — author-side perspective on PersistService.
- **[02-module-author-guide/debugging.md](../../02-module-author-guide/debugging.md)**
  — `/api/log`, `/api/state`, `/api/modules` debugging surface.

## Source

- [`components/modesp_services/include/modesp/`](../../../../components/modesp_services/include/modesp/)
  — public headers.
- [`components/modesp_services/src/`](../../../../components/modesp_services/src/)
  — implementations.
