# Debugging modules і recipes

> 📖 **Українською:** [documentation/uk/02-module-author-guide/debugging.md](../../uk/02-module-author-guide/debugging.md)

When your module misbehaves, ModESP exposes enough diagnostic surface
to find out why without а debugger attached. This page walks the routine
checks — logs, state inspection through HTTP / WebSocket / MQTT, scenario
runtime visibility — і catalogs the most common failure modes for
business-logic і recipe authors.

## Diagnostic surface overview

| Channel | What it gives you | When to use |
|---|---|---|
| **ESP_LOG (UART monitor)** | Real-time per-module log lines | First step always. Boot issues, init errors, runtime warnings. |
| **HTTP `GET /api/state`** | Full SharedState snapshot як JSON | Verify state keys exist, hold expected values. |
| **HTTP `GET /api/modules`** | Module list із initialization status | Confirm your module registered і init succeeded. |
| **WebSocket `/api/ws`** | Live state changes streamed | See data flow real-time without polling. |
| **HTTP `GET /api/scenario/list`** і `info` | Scenario engine slot pool | For recipe debugging. |
| **MQTT broker logs** | Topic publishes / subscribes | External-facing data flow problems. |
| **NVS dump** | What's persisted | When persistence isn't surviving reboot. |

## Logs as the first port of call

```bash
idf.py -p COM15 monitor
```

ESP_LOG output streams to UART. Filter by tag:

```
I (1234) my_module: Initialised, setpoint=22.0
I (1235) scenario: Engine started, slot 1 = recipe_x
W (1240) MqttService: broker unreachable, retrying у 5 s
```

Levels: `V` (verbose), `D` (debug), `I` (info), `W` (warning), `E` (error).
Filter via `idf.py monitor -t <module_tag>` або set log level у Kconfig:

```ini
# sdkconfig
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y
```

Then у your module:

```cpp
static const char* TAG = "my_module";
ESP_LOGI(TAG, "Setpoint changed to %.1f°C", setpoint);
ESP_LOGD(TAG, "Internal state: %d, %f", flag_, value_);  // only когда level=DEBUG
```

Logging guidelines:
- **`E` (error):** something failed і recovery requires intervention. One
  line per occurrence (don't spam).
- **`W` (warning):** unexpected, possibly transient. Log once на triggering
  transition.
- **`I` (info):** notable events — init complete, mode change, alarm cleared.
  Sparingly.
- **`D` (debug):** during development. Compile out for production via
  `CONFIG_LOG_MAXIMUM_LEVEL_INFO`.
- **`V` (verbose):** chatty internal trace. Compile out by default.

ESP-IDF clips lines at 256 chars total — don't dump big buffers; reach for
external loggers if you need verbose telemetry.

## Inspecting SharedState live

```bash
curl -u admin:modesp http://192.168.1.85/api/state | python -m json.tool
```

Returns а JSON object із every state key. Filter by prefix:

```bash
curl -u admin:modesp http://192.168.1.85/api/state | python -m json.tool | grep simple_thermo
```

Quick sanity checks:
- **Key missing entirely:** module didn't register it. Either `state_set`
  never called, or your module didn't `on_init` at all. Check
  `/api/modules` to confirm registration.
- **Key has wrong type:** likely an early `state_set` із typo cast (int
  literal where float expected). Variant type-locks on first set.
- **Key updates stop:** module is alive але `on_update` isn't writing
  this key. Either guard logic broke (sensor unhealthy → skip update?) or
  `on_update` itself isn't running (priority misorder, init failed).

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

Module states у the framework's lifecycle:

| State | Meaning |
|---|---|
| `CREATED` | Constructed but not yet init'd. |
| `INITIALISED` | `on_init` returned `true`; module is live і ticking. |
| `FAILED` | `on_init` returned `false`. Module is registered but inactive. |
| `STOPPED` | `on_stop` ran (rarely seen на live device). |

`FAILED` is the most actionable — check logs around that module's init
time для the reason.

## Real-time data flow via WebSocket

Open the WebUI у а browser і watch state keys update live. Each tick that
changes а state key (із `track_change=true`) sends а delta до connected
WebSocket clients. The WebUI's auto-generated cards reflect changes у
real time.

For programmatic debugging, connect із `websocat`:

```bash
websocat ws://192.168.1.85/api/ws
```

Streams JSON messages of changed keys. Useful for:
- Verifying mqtt delta-publish behavior.
- Watching scenario phase transitions у real time.
- Confirming sensor reads update at expected cadence.

## Scenario / recipe debugging

If а recipe fails to load:

```bash
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/load \
     -d '{"path": "/data/scenarios/my_recipe.modr"}'
# → {"ok": false, "error": "unknown_action"}
```

The error string identifies the class of failure. Common codes:

| Error | Meaning | Fix |
|---|---|---|
| `invalid_file` | Not а valid `.modr` blob (magic / size / format). | Re-run `compile_scenario.py`. |
| `crc_mismatch` | File corrupted у transit / flash. | Re-flash. |
| `unsupported_version` | `.modr` was built for а different engine version. | Rebuild firmware after compiling fresh recipes. |
| `unknown_action` | Recipe uses an action name not у `ActionRegistry`. | Verify domain module registered the action; check `known_actions.json`. |
| `unknown_condition` | Same but для conditions. | Same fix path. |
| `invalid_transition` | Bad target phase, bad transition kind. | compile_scenario.py should have caught — check it ran cleanly. |
| `no_slot` | All engine slots у use. | Unload а completed scenario first. |

If load succeeds but `start` fails:

```bash
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/start \
     -d '{"handle": 1}'
# → {"ok": false, "error": "resource_contended"}
```

`resource_contended` — another scenario holds your declared resources.
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

Plus the recipe's mirror keys у `/api/state`:

```
my_recipe.scenario_state:    "running"
my_recipe.main_phase_name:   "phase_b"
my_recipe.watcher_state:     "running"
```

Stuck phase:
- `phase_elapsed_s` keeps incrementing but `phase_idx` doesn't advance.
- Check transition conditions — maybe waiting on а state key що's not
  arriving.
- Manually write the awaited key (`POST /api/settings`) to force the
  transition AND verify the condition logic is correct.

Aborted scenario:
- `state: "failed"`. Check `last_error_code` if available, OR check logs
  for the abort reason (e.g., global transition fired, action returned
  FAILED_ABORT, scenario timeout exceeded).

## NVS inspection

If а value should persist but doesn't:

```bash
idf.py -p COM15 monitor
# In monitor, Ctrl+T then Ctrl+L sends Ctrl+L to ESP — typically does nothing useful.
# Use the framework's NVS dump endpoint instead (if implemented),
# or fall back to:
```

ESP-IDF's `nvs_partition_dump` tool:

```bash
esp-idf-mon nvs_partition_dump --partition nvs
```

Returns key/value list у the NVS partition. Look for `persist/<your_key>`
entries. If absent, PersistService didn't write (debounce timeout? value
unchanged?). If present але scenario doesn't see them on boot,
PersistService's restore step skipped your key (likely manifest's
`persist: true` flag missing — re-check manifest).

## MQTT message tracing

External-facing data issues: subscribe to relevant topics і watch:

```bash
mosquitto_sub -h <broker-ip> -t 'modesp/v1/+/+/#' -v
```

If а key declared у `mqtt.publish` doesn't appear:
- Confirm the broker is reachable (`/api/state` shows `mqtt.connected = true`).
- Check the topic format — typo у key OR `device_id` mismatch.
- Verify the key's value is actually changing (delta semantics — unchanged
  values don't publish).

If MQTT writes don't reach SharedState:
- Confirm `mqtt_subscribe: true` set on the state key.
- Topic format: `<base>/cmd/<key>` із а DOT у `<key>` (not slash).
- Payload format: plain ASCII (`24`, не `{"value": 24}`).

## Common module bugs і symptoms

### Module didn't register

**Symptom:** `/api/modules` doesn't list your module. State keys missing.
WebUI page absent.

**Causes:**
- Module not у `project.json` `modules` array.
- Missing/wrong `CMakeLists.txt` REQUIRES.
- `module_register.h` not regenerated (CMake didn't re-run generate_ui.py).

**Fix:** ensure project.json lists name, CMakeLists builds, `idf.py
fullclean && idf.py build`.

### Module registered but FAILED

**Symptom:** `/api/modules` shows state `"FAILED"`.

**Causes:**
- `on_init` returned `false` (most common).
- Dependency not yet initialised (priority misorder).

**Fix:** check logs around boot time for ESP_LOGE messages. Most modules
log the reason. If а dependency module needed (е.g., HAL not ready),
change your priority to а later phase.

### State key value wrong / stale

**Symptom:** `/api/state` shows а value не matching what you wrote, OR
shows stale value хоча module wrote а new one.

**Causes:**
- Wrong type passed: `state_set("key", 42)` with `int` literal on 64-bit
  host. Use `static_cast<int32_t>(42)`.
- Key type-locked by an earlier `state_set` від а different module із
  different type. Each key locks to first-set type.
- `state_set` returned `false` (capacity exhausted, key too long).
  Check `state.set_failures()` counter.

**Fix:** add ESP_LOG immediately after `state_set` to confirm. Use
`state_get<T>(key, out)` із explicit type to verify what's there.

### Module running but не doing anything

**Symptom:** Module is INITIALISED, ESP_LOG shows it ticked once during
init, but expected behavior doesn't happen.

**Causes:**
- `on_update` never overridden — default is no-op.
- Guard logic wrong (`if (sensor_ok)` always false because key wrong).
- Priority misorder — your module ticks before sensor module fills data.

**Fix:** sprinkle ESP_LOGI у `on_update` (maybe `ESP_LOGI(TAG, "tick
temp=%.1f", temp)` — once per second, NOT every tick). Confirm `on_update`
runs і inputs match expectations.

### High CPU / watchdog reset

**Symptom:** Device reboots із "Task watchdog got triggered" у logs. Or
sluggish WebUI.

**Causes:**
- `on_update` doing > 5 ms work consistently.
- Heap fragmentation з allocations у hot path.
- Tight loop у an action / condition handler.

**Fix:** profile your `on_update`. Move heavy work to а separate timer
or task. Eliminate `new` / `std::string` from tick path. Use ETL
fixed-size containers.

### Scenario stuck або loops

**Symptom:** `phase_idx` doesn't advance OR keeps re-entering the same phase.

**Causes:**
- Transition condition never fires (waiting on а state key що nobody writes).
- Phase timeout = 0 (engine treats as "no timeout"; verify ваше manifest).
- `loop_on_complete` flag на а short phase із `$complete` target —
  perpetual loop.

**Fix:** dump mirror keys із `/api/state` — verify `phase_name`,
`phase_elapsed_s` look right. Manually inject the expected condition key
із `POST /api/settings` to confirm the transition logic.

### Build errors

`compile_scenario.py` rejects:
- Schema validation — manifest field missing / wrong type. Error message
  points to the line.
- Cross-validation — mirror state key not declared. Add it to manifest's
  `state` section.
- Unknown action — name not у `known_actions.json` AND not built-in.

generate_ui.py rejects:
- Hardware ID not declared у board.json. Edit board.json або fix bindings.
- Duplicate role name. Each role unique within а module.

## Production-mode debugging

On а deployed device без UART:

1. **Logs go to MQTT (Stage 1.5 feature):** `logger_service` will forward
   ESP_LOGI/W/E messages to а dedicated MQTT topic. Subscribe via the
   broker.
2. **WebUI logs page:** `/api/log` returns recent log buffer (~last 100
   lines retained у RAM).
3. **State + module status via HTTP:** all the `/api/state` і
   `/api/modules` calls work over а network connection.

## Pre-flight checklist (before reporting а bug)

Before filing а bug or asking для help, gather:

1. **`/api/state` output** filtered to your module's prefix.
2. **`/api/modules` output** showing your module's state.
3. **Last 20-50 lines of monitor output** around the problem.
4. **The manifest** of the failing module (just the relevant section).
5. **Steps to reproduce** including HTTP calls / WebUI clicks / etc.

This usually points at the root cause within а few iterations.

## Next steps

- **[best-practices.md](best-practices.md)** — patterns that avoid these
  problems у the first place.
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
