# Best practices

> 📖 **In English:** [documentation/en/02-module-author-guide/best-practices.md](../../en/02-module-author-guide/best-practices.md)

Digest patterns що працюють і anti-patterns що kусаються. Прочитавши, ви
знатимете як виглядає good module / driver / recipe code у ModESP, і
чому deviations cost performance, reliability, або maintainability.

Ця сторінка concise on purpose — examples і rationale сидять у focused
topic pages (shared-state, writing-a-module, тощо). Думайте про неї як
про checklist.

## Memory і allocation

✅ **Stay heap-free у hot paths.** Modules tick at 100 Hz. Кожен `new`,
`std::string`, `std::vector::push_back`, `std::map` insertion leaks RAM
або fragments heap. Use ETL fixed-capacity containers (`etl::string`,
`etl::vector`, `etl::flat_map`), C arrays, або POD types.

✅ **Allocate once при init, reuse forever.** Якщо вашому модулю треба
buffer, make it member вашого class. Не allocate per call.

❌ **Не `new` object у `on_update`. Period.**

❌ **Не зберігайте JSON blobs у SharedState.** Strings 32 chars max;
larger values потребують raw NVS або LittleFS file.

## Lifecycle і timing

✅ **Use `on_init` для setup, `on_update` для work.** Read persisted
config, log initial state, cache references у init. Do actual business
logic у update.

✅ **Choose priority wisely.** CRITICAL (0): error_service, watchdog
лише. HIGH (1): WiFi, HAL, drivers, scenario engine. NORMAL (2): business
logic — більшість modules. LOW (3): HTTP, WebSocket, datalogger —
залежать від всього вище.

✅ **Tick budget < 1 мс per module.** Багато modules running у одному
10 мс tick. Anything heavier потребує окремий timer / task / scenario.

❌ **Не `vTaskDelay` / sleep у `on_update`.** Blocks every other
module. Use accumulator pattern:

```cpp
elapsed_ms_ += dt_ms;
if (elapsed_ms_ < interval_ms_) return;
elapsed_ms_ = 0;
// ... do periodic work ...
```

❌ **Не write NVS у `on_update`.** Synchronous I/O, 5-50 мс per write.
Use `persist: true` flag на state keys, OR explicit `nvs_helper` calls
з one-shot event handler.

❌ **Не log on every tick.** UART floods, monitor lags. Один ESP_LOGI
per state change, не per tick.

## State management

✅ **Read inputs at top of `on_update`, write outputs at bottom.**
Sequential per-tick pattern. Clear to read.

✅ **Cast int literals to `int32_t`.** `state_set("key", 42)` fragile
across hosts; use `state_set("key", static_cast<int32_t>(42))` або `42L`.

✅ **Use typed accessors:** `read_float(key, 0.0f)`, не `state_get(key)
+ etl::get_if`. Менше code, fail-safe defaults.

✅ **Provide sensible defaults у `read_*`.** Якщо key не set yet (інший
module не tick), default — ваш fallback. Choose conservatively (safe
values, не zero unless safe).

❌ **Не construct dynamic state keys.** `"my_module.item_" + std::to_string(i)`
fills bounded state map fast і breaks compile-time validation. Declare
fixed set у manifest.

❌ **Не type-flip keys.** Once key set as float, all subsequent writes
must be float. Variant rejects type changes silently — log warning якщо
suspect це happens.

## Cross-module communication

✅ **Default to pure state-key data flow.** Module A writes key, module
B reads it. No pointers, no message passing.

✅ **Declare producer modules before consumers у `project.json`.**
Determines update order у priority bucket. Producer's writes від same
tick visible to consumer's reads.

✅ **Для edge detection, keep `prev_value_` member.** No engine-level
edge tracking у MVP. Pattern:

```cpp
bool fault = read_bool("equipment.fault", false);
if (fault && !prev_fault_) { /* rising edge */ }
prev_fault_ = fault;
```

❌ **Не poke other modules' internals.** No `manager.get_module<X>()`
patterns. State keys only.

❌ **Не use messages для periodic data.** State keys simpler і more
observable. Messages лише для discrete events (alarm fired, mode change
command).

## Manifests і generation

✅ **One file per topic / unit.** Не sprawl across many manifests for
related state. One module = one manifest.

✅ **Declare all state keys upfront.** Generator emits constexpr
metadata; dynamic state keys break it.

✅ **Match manifest types to code types.** `"type": "float"` у manifest
means write із `float`/`f32`, не `int`. Generator не cross-validate але
runtime variance hurts.

✅ **Keep key prefixes short.** `<module>.<key>` ≤ 32 chars budget,
recipe names ≤ 12, track names ≤ 8.

❌ **Не забудьте `priority` field для non-default modules.** Default —
NORMAL (2). HAL or driver-hosting modules need HIGH (1).

❌ **Не ставте `persist: true` на fast-changing keys.** Debounce window
— 5 с; counter incrementing per second flushes too often. Persist
user-controlled config, calibration constants, mode selections.

## Drivers

✅ **Implement health monitoring.** Track consecutive failures; set
`is_healthy() = false` після threshold. Business modules check `_ok`
suffix на equipment.* keys.

✅ **Respect `min_switch_ms` for actuators.** Compressors і pumps fail
mechanically із rapid switching. Encode lockout у driver, не expecting
business module discipline.

✅ **Apply calibration at read time, не at store time.** Changes до
`offset` setting affect next read immediately, не після reboot.

❌ **Не block у `update()`.** Slow protocols (DS18B20 conversion = 750
мс) потребують state-machine pattern across multiple ticks, не
synchronous waits.

❌ **Не allocate per read.** Use stack buffers для parsing / formatting.

## Scenarios і recipes

✅ **Set phase timeout завжди.** Навіть якщо conditions завжди повинні
fire, `timeout_ms` — defense у depth. ADR-0007 makes them mandatory.

✅ **Declare усі mirror keys у recipe manifest's `state` section.**
Engine cross-validates і fails build otherwise.

✅ **Use transition `time_elapsed_ms` над `wait_ms` action.** Transitions
zero-cost per tick; actions invoke handler.

✅ **Declare producer tracks перед consumer tracks.** Cross-track sync
— tick-order. Watcher track що waits для main's phase_name must come
після main у `tracks` array.

❌ **Не put side effects у conditions.** Conditions — pure reads.
Evaluated multiple times per tick during transition checks. Side effects
accumulate unpredictably.

❌ **Не rely на snapshot consistency across tracks.** Tracks tick
serially. Track 1 reads track 0's same-tick writes (good). Track 0 не
може see track 1's writes від same tick (bad assumption).

❌ **Не write long recipes як one giant track.** Use parallel tracks
для monitors, supervisors, timeouts. Easier to reason about.

## MQTT і external API

✅ **Use manifest, ніколи не raw esp_mqtt_client.** Фреймворк handles
TLS, reconnect, throttling, HA discovery, LWT — все вашими словами:
`mqtt.publish: [...]`.

✅ **Validate `mqtt_subscribe: true` flag** на кожному writable key
exposed через MQTT. Generator catches missing flags при build.

✅ **Plain ASCII payloads на `cmd/` topics.** Не JSON. `"24"`, не
`"{"value": 24}"`.

❌ **Не expose internal config keys для external write.** Keep
`mqtt_subscribe` selective — user-facing setpoints, не debug counters.

❌ **Не assume atomic multi-key writes через MQTT.** Each topic delivers
independently. Use composite state key (наприклад, JSON-encoded string)
якщо atomicity matters.

## Persistence

✅ **`persist: true` для user-tunable settings.** Setpoints, calibration,
operating modes. Survives reboot transparently.

✅ **Provide `default` value** для кожного persisted key. First boot
(no NVS data) falls back gracefully.

✅ **Для migration при renaming persisted key**, add explicit migration
code у `on_init` вашого module (read old key, write new key, erase old).

❌ **Не `persist: true` counters / sensor readings.** Debounce не keep
up; flash wear, slow boot.

❌ **Не bypass PersistService casually.** Direct `nvs_helper` calls у
ваш own namespace — fine; mixing із `"persist"` namespace causes
conflicts.

## Build і CI

✅ **Run `idf.py fullclean && idf.py build` після major changes.**
Generated headers можуть go stale; fullclean forces regeneration.

✅ **Use host tests для state-machine логіки.** `tests/host/` runs з
stub SharedState backend. Fast iteration без flashing.

✅ **Check size budget periodically.** `idf.py size-components` shows
RAM / IRAM usage per component. Surprises у growth point на heap leaks
або static-init bloat.

❌ **Не ignore compiler warnings.** `-Wall -Wextra` produces signal,
не noise. Address them або document why suppression OK.

❌ **Не commit code без flashing і testing на hardware.** Host tests
catch much але WiFi / NVS / display behaviors emerge лише на real chip.

## Naming і structure

✅ **`snake_case` для state keys, module names, action names.** Reserved
для фреймворку, consistent across усіх manifests.

✅ **`CamelCase` для C++ class names.** Module class = `<Name>Module`
(suffix). Driver class = `<Name>Driver`.

✅ **Short module names** (≤ 12 chars). Long names eat state key budget.

✅ **Group related state keys з common prefix.** Усі thermostat keys під
`simple_thermo.*`, усі equipment під `equipment.*`.

❌ **Не put framework-level keys у ваш module.** `system.time`,
`_ota.version` reserved.

## Documentation і code style

✅ **One-line module description у `manifest.json`.** First sentence
повинен answer "що це робить?".

✅ **Document edge cases у file-header comments.** "Lifecycle: configure
→ init → update → emergency_stop". Specific, не generic.

✅ **ESP_LOGI states — valid runtime documentation.** "Setpoint changed
to 22.5" — debugger gold dust шість months later.

❌ **Не comment що код робить — comment чому.** "Increment count" —
noise. "Increment until threshold; resetting у `reset()`" — useful.

## Коли doubt

Read existing modules first:
1. **`simple_thermo`** — minimal service module. Найкраща перша річ
   для читання.
2. **`datalogger`** — bigger module із features і channels.
3. **`equipment`** — most coupled module, read after basics.
4. **`abs_test`** — recipe-only module із 2 parallel tracks.

Reading 50 lines of working code beats reading 500 lines documentation.

## Що далі

Ця сторінка — end of Module Author Guide. Звідси:

- **[scenario-engine/](../03-framework-reference/scenario-engine/)** —
  scenario engine internals якщо хочете understand або extend it.
- **[components/](../03-framework-reference/components/)** *(in progress)* —
  per-component framework reference.
- **[04-hardware/](../04-hardware/)** — board.json, bindings, OTA, deployment.
- **[06-contributing/](../06-contributing/)** *(planned)* — для contributing
  до самого фреймворку.
