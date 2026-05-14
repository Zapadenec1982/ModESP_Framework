# `modesp_services` — Config, Persist, Error, Watchdog, Logger, SystemMonitor

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_services.md](../../../en/03-framework-reference/components/modesp_services.md)

`modesp_services` ships system services що потребує кожна firmware
regardless of business logic: configuration loading, NVS persistence,
error reporting, watchdog, log buffering, system monitoring, і OTA
handling. Усі run at `CRITICAL` priority (or close to it), initialising
перед any business module.

Ця сторінка документує що кожен service provides, як вони wire to
SharedState, і коли business modules потребують interact із ними напряму
(рідко — більшість integration transparent).

REQUIRES: `modesp_core`, `nvs_flash`, ESP-IDF helpers.

## Services overview

| Service | Priority | Purpose |
|---|---|---|
| **ErrorService** | CRITICAL | Central error reporting, fault tracking. |
| **WatchdogService** | CRITICAL | Feeds ESP-IDF task watchdog з main task. |
| **ConfigService** | CRITICAL | Reads `board.json` і `bindings.json` з LittleFS. |
| **PersistService** | CRITICAL | Mirrors persisted state keys у NVS. |
| **LoggerService** | CRITICAL | RAM ring buffer recent ESP_LOG lines (для `/api/log`). |
| **SystemMonitor** | CRITICAL | Heap, uptime, task stats, free-RAM tracking. |
| **OtaHandler** | (helper) | Не BaseModule — utility called by HTTP / MQTT. |

Plus `nvs_helper` namespace — thin C++ wrapper над ESP-IDF's NVS API
(`nvs_*` functions). Used internally by PersistService і available для
modules що потребують direct NVS access.

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

    // ... тощо.
};
```

State keys exposed:

| Key | Type | Notes |
|---|---|---|
| `error_service.last_code` | int | Numeric code most recent fault. |
| `error_service.last_details` | string | Free-form text supplied by reporter. |
| `error_service.active_count` | int | Кількість unresolved faults. |
| `error_service.cleared_count` | int | Cumulative cleared (для diagnostics). |

Business modules викликають `report_fault` з `on_update` коли щось wrong.
Module status (`equipment.<role>_ok = false`, наприклад) flows через
SharedState; ErrorService аккумулирует higher-level fault summary для
UI / MQTT publishing.

## `WatchdogService` — keeps device alive

```cpp
class WatchdogService : public modesp::BaseModule {
public:
    WatchdogService();
};
```

Subscribes itself до ESP-IDF's task watchdog у `on_init` і викликає
`esp_task_wdt_reset()` з `on_update`. Якщо main task hangs (any module's
`on_update` takes too long), watchdog triggers system reboot після
configured timeout (~5 секунд default).

Без state keys, без API. Module exists primarily щоб prove що framework
ticking properly. Якщо ви бачите watchdog resets у logs, run profiler —
some module exceeds tick budget.

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

Reads `data/board.json` і `data/bindings.json` при `on_init`. Provides
parsed structures що інші components (HAL, Equipment Manager, drivers)
consume.

`reload()` re-reads files. Used після OTA file replacement або WebUI
bindings editor (planned). Зараз invoked лише з HTTP `/api/bindings`
POST handler.

State keys:

| Key | Notes |
|---|---|
| `config.board_name` | Board identifier з `board.json::board`. |
| `config.bindings_count` | Number bindings entries. |

## `PersistService` — NVS state mirror

```cpp
#include "modesp/services/persist_service.h"

class PersistService : public modesp::BaseModule {
public:
    PersistService();

    static constexpr uint32_t DEBOUNCE_MS = 5000;
    static constexpr const char* NVS_NAMESPACE = "persist";

    // Internal — wired через SharedState::set_persist_callback.
    static void on_state_changed(const StateKey&, const StateValue&, void*);
};
```

Loop:
1. При `on_init`, scan `state_meta.h` для усіх keys з `persist: true`.
   Read кожен з NVS namespace `"persist"` і set value у SharedState.
2. Register callback із `SharedState::set_persist_callback`. Callback
   fires при кожному tracked write.
3. Callback identifies persisted keys і schedules debounced NVS write
   (5 секунд "no further change" → flush до flash).

Full reading: [persistence.md](../../02-module-author-guide/persistence.md).

API surface для modules — **просто `persist: true` flag у manifest**.
Ви не call-ите PersistService напряму.

## `LoggerService` — recent log buffer

```cpp
#include "modesp/logger_service.h"

class LoggerService : public modesp::BaseModule {
public:
    LoggerService();

    // Internal — captures ESP_LOG vprintf hook.
    static int esp_log_vprintf_hook(const char* fmt, va_list args);

    // Public — retrieve recent lines для /api/log.
    void copy_recent(char* out, size_t cap, size_t* out_len);
};
```

Hooks ESP-IDF's `esp_log_set_vprintf` і captures recent log lines у RAM
ring buffer (default ~4 KB, last ~100 lines). HTTP `/api/log` serves це
buffer для remote diagnostics — корисно коли UART unavailable.

State keys:

| Key | Notes |
|---|---|
| `logger.line_count` | Number lines captured since boot. |
| `logger.buffer_used_bytes` | Current bytes у ring buffer. |

Без filtering / level controls у MVP. Stage 1.5 може add per-tag level
adjustments через `/api/log/config`.

## `SystemMonitor` — heap і uptime stats

```cpp
#include "modesp/system_monitor.h"

class SystemMonitor : public modesp::BaseModule {
public:
    SystemMonitor(ErrorService& errors);
};
```

Periodically (кожні ~1 секунду) writes diagnostic state keys:

| Key | Notes |
|---|---|
| `system.free_heap` | `esp_get_free_heap_size()` bytes. |
| `system.min_free_heap` | `esp_get_minimum_free_heap_size()`. |
| `system.uptime_s` | Seconds since boot. |
| `system.reset_reason` | One of "POWERON" / "BROWNOUT" / "WDT" / "EXTERNAL" тощо. |
| `system.task_count` | Active FreeRTOS tasks. |
| `system.cpu_freq_mhz` | Current CPU frequency. |

WebUI shows це на System page. MQTT publishes якщо listed у `mqtt.publish`.

Якщо `min_free_heap < threshold`, reports `ErrorCode::LOW_MEMORY` до
ErrorService. Threshold — Kconfig-tunable (default ~16 KB).

## `OtaHandler` — firmware updates

```cpp
#include "modesp/services/ota_handler.h"

class OtaHandler {
public:
    // Called з HTTP /api/ota/upload handler.
    bool begin(size_t total_size);
    bool write(const uint8_t* data, size_t len);
    bool finish(bool& valid);

    bool confirm_running_partition();    // mark current image stable
    bool rollback();                      // boot із previous partition
};
```

Не BaseModule — utility used by `modesp_net::HttpService`'s `/api/ota/*`
handlers. State keys у `_ota.*` namespace (underscore prefix marks
framework-internal):

| Key | Notes |
|---|---|
| `_ota.status` | "idle" / "downloading" / "verifying" / "rebooting" / "rolled_back" |
| `_ota.progress` | Bytes transferred. |
| `_ota.error` | Error string якщо status = "failed". |
| `_ota.version` | Active firmware version. |
| `_ota.partition` | Active partition name. |
| `_ota.date` / `_ota.idf` | Build metadata. |

Full OTA flow: [04-hardware/ota.md](../../04-hardware/ota.md) *(planned)*.

## `nvs_helper` — raw NVS API

Для коли `persist: true` недостатньо — large blobs, custom encoding,
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

// Batch (швидше для many writes)
struct nvs_handle_t* batch_open(const char* ns, bool readonly);
bool batch_read_*(...);   // same але на open handle
bool batch_write_*(...);
bool batch_close(struct nvs_handle_t* h);

}
```

Use cases у самому фреймворку:
- `scnstate` namespace (scenario engine tokens).
- `auth` namespace (admin credentials).
- `time` namespace (NTP / timezone).
- `seqstate` (legacy scenario engine — being phased out).

Use namespace ІНШИЙ than `"persist"` щоб уникнути conflict із
auto-managed PersistService keys.

## Service registration у main.cpp

```cpp
static modesp::ErrorService    error_service;
static modesp::ConfigService   config_service;
static modesp::PersistService  persist_service;
static modesp::LoggerService   logger_service;
static modesp::SystemMonitor   system_monitor(error_service);
// WatchdogService instantiated dynamically бо needs ModuleManager ref:
// (див. main.cpp's app_main)

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

## Що business modules повинні знати

Для 95% business modules ви можете ignore цей component повністю. Речі
що варто знати на випадок:

1. **`persist: true`** у manifest → PersistService handles storage.
2. **Faults що ви detect** → call `error_service.report_fault(code, "...")`
   щоб surface до UI / MQTT.
3. **Long operations** → watchdog reset вас якщо `on_update` exceeds
   ~5 с. Break work у ticks.
4. **NVS writes** → use `persist: true` (5 с debounce) або explicit
   `nvs_helper::write_*` для one-shot stuff. Не write раз на tick.
5. **`/api/log`** → ваші ESP_LOG messages visible через HTTP. Не log
   sensitive data.

## Common patterns

### Module reports fault і recovers

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

Edge-trigger pattern — report once, clear once. Не spam на every tick.

### Custom NVS namespace для large data

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

Encapsulate NVS access у member functions; call із `on_init` (load) і
deliberate save trigger (наприклад, `apply_calibration` action). Ніколи
у `on_update`.

## Common pitfalls

**`PersistService` не restore при first boot:** це correct — no value у
NVS means use default. Make sure ваш `state` declaration has `default`
field для fresh-boot fallback.

**`ConfigService::reload()` з `on_update`:** parses JSON synchronously,
takes ~50 мс. Block. Use лише з one-shot HTTP handler.

**Heap leak із `nvs_helper::read_blob`:** function fills buffer що ви
provide, doesn't allocate. Але якщо ви allocate цей buffer із `new`,
remember to delete. Use stack buffers коли possible.

**Reset reason "WDT" persists across boots:** це expected — last reset
reason ship через reboot. Якщо бачите після power cycle, означає що
previous run hit watchdog. Capture log dump через `/api/log` якщо
reproducible.

## Що далі

- **[components/modesp_hal.md](modesp_hal.md)** *(planned)* — HAL і
  DriverManager.
- **[components/modesp_net.md](modesp_net.md)** *(planned)* — WiFi і
  HTTP services що consume ConfigService і OtaHandler.
- **[02-module-author-guide/persistence.md](../../02-module-author-guide/persistence.md)**
  — author-side perspective на PersistService.
- **[02-module-author-guide/debugging.md](../../02-module-author-guide/debugging.md)**
  — `/api/log`, `/api/state`, `/api/modules` debugging surface.

## Source

- [`components/modesp_services/include/modesp/`](../../../../components/modesp_services/include/modesp/)
  — public headers.
- [`components/modesp_services/src/`](../../../../components/modesp_services/src/)
  — implementations.
