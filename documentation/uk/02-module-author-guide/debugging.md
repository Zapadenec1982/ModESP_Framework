# Debugging модулів і рецептів

> 📖 **In English:** [documentation/en/02-module-author-guide/debugging.md](../../en/02-module-author-guide/debugging.md)

Коли ваш модуль misbehaves, ModESP exposes достатньо diagnostic surface
щоб з'ясувати чому без debugger attached. Ця сторінка walks routine
checks — logs, state inspection через HTTP / WebSocket / MQTT, scenario
runtime visibility — і catalogs найпоширеніші failure modes для
business-logic і recipe authors.

## Diagnostic surface overview

| Channel | Що дає | Коли використовувати |
|---|---|---|
| **ESP_LOG (UART monitor)** | Real-time per-module log lines | Перший крок завжди. Boot issues, init errors, runtime warnings. |
| **HTTP `GET /api/state`** | Full SharedState snapshot як JSON | Verify state keys exist, hold expected values. |
| **HTTP `GET /api/modules`** | Module list із initialization status | Confirm ваш модуль registered і init succeeded. |
| **WebSocket `/api/ws`** | Live state changes streamed | Бачити data flow real-time без polling. |
| **HTTP `GET /api/scenario/list`** і `info` | Scenario engine slot pool | Для recipe debugging. |
| **MQTT broker logs** | Topic publishes / subscribes | External-facing data flow problems. |
| **NVS dump** | Що persisted | Коли persistence не surviving reboot. |

## Logs як перший port of call

```bash
idf.py -p COM15 monitor
```

ESP_LOG output streams до UART. Filter by tag:

```
I (1234) my_module: Initialised, setpoint=22.0
I (1235) scenario: Engine started, slot 1 = recipe_x
W (1240) MqttService: broker unreachable, retrying у 5 с
```

Levels: `V` (verbose), `D` (debug), `I` (info), `W` (warning), `E` (error).
Filter через `idf.py monitor -t <module_tag>` або set log level у Kconfig:

```ini
# sdkconfig
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y
```

Потім у вашому модулі:

```cpp
static const char* TAG = "my_module";
ESP_LOGI(TAG, "Setpoint changed to %.1f°C", setpoint);
ESP_LOGD(TAG, "Internal state: %d, %f", flag_, value_);  // лише коли level=DEBUG
```

Logging guidelines:
- **`E` (error):** щось failed і recovery вимагає intervention. Один
  рядок per occurrence (не спамити).
- **`W` (warning):** unexpected, possibly transient. Log once per
  triggering transition.
- **`I` (info):** notable events — init complete, mode change, alarm
  cleared. Sparingly.
- **`D` (debug):** під час development. Compile out для production через
  `CONFIG_LOG_MAXIMUM_LEVEL_INFO`.
- **`V` (verbose):** chatty internal trace. Compile out by default.

ESP-IDF clips lines на 256 chars total — не dump big buffers; reach for
external loggers якщо вам треба verbose telemetry.

## Inspecting SharedState live

```bash
curl -u admin:modesp http://192.168.1.85/api/state | python -m json.tool
```

Returns JSON object з кожним state key. Filter by prefix:

```bash
curl -u admin:modesp http://192.168.1.85/api/state | python -m json.tool | grep simple_thermo
```

Quick sanity checks:
- **Key missing entirely:** module не registered його. Або `state_set`
  ніколи не called, або ваш module не `on_init` at all. Check
  `/api/modules` щоб confirm registration.
- **Key has wrong type:** likely an early `state_set` із typo cast (int
  literal where float expected). Variant type-locks на first set.
- **Key updates stop:** module alive але `on_update` не writing цей
  key. Або guard logic broke (sensor unhealthy → skip update?) або
  `on_update` itself не running (priority misorder, init failed).

## Module registration і init status

```bash
curl -u admin:modesp http://192.168.1.85/api/modules
```

Returns array із module names і states:

```json
[
  {"name": "error_service", "state": "INITIALISED"},
  {"name": "my_module",     "state": "FAILED"},          // ← problem
  ...
]
```

Module states у lifecycle фреймворку:

| State | Meaning |
|---|---|
| `CREATED` | Constructed but not yet init'd. |
| `INITIALISED` | `on_init` повернув `true`; module live і ticking. |
| `FAILED` | `on_init` повернув `false`. Module registered але inactive. |
| `STOPPED` | `on_stop` ran (rarely seen на live device). |

`FAILED` — найбільш actionable — check logs навколо module's init time
для reason.

## Real-time data flow через WebSocket

Open WebUI у браузері і watch state keys update live. Кожен tick що
змінює state key (з `track_change=true`) sends delta до connected
WebSocket clients. WebUI's auto-generated cards reflect changes у real
time.

Для programmatic debugging, connect з `websocat`:

```bash
websocat ws://192.168.1.85/api/ws
```

Streams JSON messages of changed keys. Useful для:
- Verifying mqtt delta-publish behavior.
- Watching scenario phase transitions real time.
- Confirming sensor reads update на expected cadence.

## Scenario / recipe debugging

Якщо recipe fails to load:

```bash
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/load \
     -d '{"path": "/data/scenarios/my_recipe.modr"}'
# → {"ok": false, "error": "unknown_action"}
```

Error string identifies class failure. Common codes:

| Error | Meaning | Fix |
|---|---|---|
| `invalid_file` | Не valid `.modr` blob (magic / size / format). | Re-run `compile_scenario.py`. |
| `crc_mismatch` | File corrupted у transit / flash. | Re-flash. |
| `unsupported_version` | `.modr` built для іншої engine version. | Rebuild firmware після compile fresh recipes. |
| `unknown_action` | Recipe використовує action name не у `ActionRegistry`. | Verify domain module registered action; check `known_actions.json`. |
| `unknown_condition` | Same але для conditions. | Same fix path. |
| `invalid_transition` | Bad target phase, bad transition kind. | compile_scenario.py повинен був catch — check він ran cleanly. |
| `no_slot` | Усі engine slots у use. | Unload completed scenario first. |

Якщо load succeeds але `start` fails:

```bash
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/start \
     -d '{"handle": 1}'
# → {"ok": false, "error": "resource_contended"}
```

`resource_contended` — інший scenario holds ваші declared resources.
`/api/scenario/list` shows currently active instances.

Once started, inspect runtime:

```bash
curl -u admin:modesp "http://192.168.1.85/api/scenario/info?handle=1"
```

```json
{
  "state": "running",
  "elapsed_s": 12,
  "tracks": [
    {"idx": 0, "state": "running", "phase_idx": 1, "phase_elapsed_s": 4},
    {"idx": 1, "state": "running", "phase_idx": 0, "phase_elapsed_s": 12}
  ]
}
```

Plus recipe's mirror keys у `/api/state`:

```
my_recipe.scenario_state:    "running"
my_recipe.main_phase_name:   "phase_b"
my_recipe.watcher_state:     "running"
```

Stuck phase:
- `phase_elapsed_s` keeps incrementing але `phase_idx` не advances.
- Check transition conditions — maybe waiting на state key що не arriving.
- Manually write awaited key (`POST /api/settings`) щоб force transition
  AND verify condition logic correct.

Aborted scenario:
- `state: "failed"`. Check `last_error_code` if available, OR check logs
  для abort reason (наприклад, global transition fired, action returned
  FAILED_ABORT, scenario timeout exceeded).

## NVS inspection

Якщо value повинен persist але не doing it:

```bash
idf.py -p COM15 monitor
# У monitor, Ctrl+T then Ctrl+L sends Ctrl+L до ESP — typically nothing useful.
# Use framework's NVS dump endpoint якщо implemented,
# або fall back:
```

ESP-IDF's `nvs_partition_dump` tool:

```bash
esp-idf-mon nvs_partition_dump --partition nvs
```

Returns key/value list у NVS partition. Look для `persist/<your_key>`
entries. Якщо absent, PersistService не writeкало (debounce timeout?
value unchanged?). Якщо present але scenario не see them on boot,
PersistService's restore step skipped your key (likely manifest's
`persist: true` flag missing — re-check manifest).

## MQTT message tracing

External-facing data issues: subscribe to relevant topics і watch:

```bash
mosquitto_sub -h <broker-ip> -t 'modesp/v1/+/+/#' -v
```

Якщо key declared у `mqtt.publish` не з'являється:
- Confirm broker reachable (`/api/state` shows `mqtt.connected = true`).
- Check topic format — typo у key АБО `device_id` mismatch.
- Verify key's value actually changing (delta semantics — unchanged
  values не publish).

Якщо MQTT writes не reach SharedState:
- Confirm `mqtt_subscribe: true` set на state key.
- Topic format: `<base>/cmd/<key>` із DOT у `<key>` (не slash).
- Payload format: plain ASCII (`24`, не `{"value": 24}`).

## Common module bugs і symptoms

### Module не registered

**Symptom:** `/api/modules` не lists ваш module. State keys missing.
WebUI page absent.

**Причини:**
- Module не у `project.json` `modules` array.
- Missing/wrong `CMakeLists.txt` REQUIRES.
- `module_register.h` не regenerated (CMake не re-run generate_ui.py).

**Fix:** ensure project.json lists name, CMakeLists builds, `idf.py
fullclean && idf.py build`.

### Module registered але FAILED

**Symptom:** `/api/modules` shows state `"FAILED"`.

**Причини:**
- `on_init` повернув `false` (найпоширеніше).
- Dependency не yet initialised (priority misorder).

**Fix:** check logs around boot time для ESP_LOGE messages. Most modules
log reason. Якщо dependency module needed (е.g., HAL not ready), change
ваш priority до later phase.

### State key value wrong / stale

**Symptom:** `/api/state` shows value що не matching що ви wrote, OR
shows stale value хоча module wrote new one.

**Причини:**
- Wrong type passed: `state_set("key", 42)` з `int` literal на 64-bit
  host. Use `static_cast<int32_t>(42)`.
- Key type-locked by an earlier `state_set` від different module із
  different type. Each key locks до first-set type.
- `state_set` returned `false` (capacity exhausted, key too long).
  Check `state.set_failures()` counter.

**Fix:** add ESP_LOG immediately after `state_set` щоб confirm. Use
`state_get<T>(key, out)` із explicit type щоб verify що там.

### Module running але не doing anything

**Symptom:** Module INITIALISED, ESP_LOG shows it ticked once during
init, але expected behavior не happens.

**Причини:**
- `on_update` ніколи не overridden — default is no-op.
- Guard logic wrong (`if (sensor_ok)` завжди false бо key wrong).
- Priority misorder — ваш module ticks before sensor module fills data.

**Fix:** sprinkle ESP_LOGI у `on_update` (maybe `ESP_LOGI(TAG, "tick
temp=%.1f", temp)` — once per second, NOT every tick). Confirm `on_update`
runs і inputs match expectations.

### High CPU / watchdog reset

**Symptom:** Device reboots із "Task watchdog got triggered" у logs.
Або sluggish WebUI.

**Причини:**
- `on_update` doing > 5 мс work consistently.
- Heap fragmentation із allocations у hot path.
- Tight loop у action / condition handler.

**Fix:** profile ваш `on_update`. Move heavy work до окремого timer
або task. Eliminate `new` / `std::string` з tick path. Use ETL
fixed-size containers.

### Scenario stuck або loops

**Symptom:** `phase_idx` не advances OR keeps re-entering same phase.

**Причини:**
- Transition condition ніколи не fires (waiting на state key що ніхто
  не writes).
- Phase timeout = 0 (engine treats як "no timeout"; verify ваш manifest).
- `loop_on_complete` flag на short phase із `$complete` target —
  perpetual loop.

**Fix:** dump mirror keys із `/api/state` — verify `phase_name`,
`phase_elapsed_s` look right. Manually inject expected condition key
із `POST /api/settings` щоб confirm transition logic.

### Build errors

`compile_scenario.py` rejects:
- Schema validation — manifest field missing / wrong type. Error message
  points до line.
- Cross-validation — mirror state key не declared. Add it до manifest's
  `state` section.
- Unknown action — name не у `known_actions.json` AND не built-in.

generate_ui.py rejects:
- Hardware ID не declared у board.json. Edit board.json або fix bindings.
- Duplicate role name. Each role unique у межах модуля.

## Production-mode debugging

На deployed device без UART:

1. **Logs go to MQTT (Stage 1.5 feature):** `logger_service` буде forward
   ESP_LOGI/W/E messages до dedicated MQTT topic. Subscribe via broker.
2. **WebUI logs page:** `/api/log` returns recent log buffer (~last 100
   lines retained у RAM).
3. **State + module status через HTTP:** усі `/api/state` і
   `/api/modules` calls work через network connection.

## Pre-flight checklist (перед reporting bug)

Перед filing bug або asking для help, gather:

1. **`/api/state` output** filtered to ваш module's prefix.
2. **`/api/modules` output** showing ваш module's state.
3. **Last 20-50 lines monitor output** around problem.
4. **Manifest** of failing module (just relevant section).
5. **Steps to reproduce** including HTTP calls / WebUI clicks / etc.

Це usually points на root cause у межах few iterations.

## Що далі

- **[best-practices.md](best-practices.md)** — patterns що avoid цих
  problems у first place.
- **[shared-state.md](shared-state.md)** — type rules і common pitfalls.
- **[scenario-engine/10_error_model.md](../03-framework-reference/scenario-engine/10_error_model.md)** —
  full engine error taxonomy.
- **[components/modesp_services.md](../03-framework-reference/components/modesp_services.md)**
  *(planned)* — error_service і watchdog_service deep dive.

## Source pointers

- [`components/modesp_services/src/error_service.cpp`](../../../components/modesp_services/src/error_service.cpp)
  — central error reporting.
- [`components/modesp_services/src/logger_service.cpp`](../../../components/modesp_services/src/logger_service.cpp)
  — log buffer / forwarding.
- [`components/modesp_net/src/http_service.cpp`](../../../components/modesp_net/src/http_service.cpp)
  — `/api/state`, `/api/modules` handlers.
