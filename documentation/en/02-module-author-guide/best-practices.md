# Best practices

> 📖 **Українською:** [documentation/uk/02-module-author-guide/best-practices.md](../../uk/02-module-author-guide/best-practices.md)

A digest of patterns that work і anti-patterns that bite. After reading
this you'll know what good module / driver / recipe code looks like у
ModESP, і why deviations cost performance, reliability, або maintainability.

This page is concise on purpose — examples і rationale сидять у the
focused topic pages (shared-state, writing-a-module, etc.). Think of це
як а checklist.

## Memory і allocation

✅ **Stay heap-free у hot paths.** Modules tick at 100 Hz. Every `new`,
`std::string`, `std::vector::push_back`, `std::map` insertion leaks RAM
или fragments the heap. Use ETL fixed-capacity containers (`etl::string`,
`etl::vector`, `etl::flat_map`), C arrays, або POD types.

✅ **Allocate once at init, reuse forever.** If your module needs а buffer,
make it а member of your class. Don't allocate per call.

❌ **Don't** `new` an object у `on_update`. Period.

❌ **Don't store JSON blobs у SharedState.** Strings are 32 chars max;
larger values need raw NVS або LittleFS file.

## Lifecycle і timing

✅ **Use `on_init` for setup, `on_update` для work.** Read persisted
config, log initial state, cache references у init. Do the actual
business logic у update.

✅ **Choose priority wisely.** CRITICAL (0): error_service, watchdog only.
HIGH (1): WiFi, HAL, drivers, scenario engine. NORMAL (2): business
logic — most modules. LOW (3): HTTP, WebSocket, datalogger — depend on
everything above.

✅ **Tick budget < 1 ms per module.** Many modules running у one 10 ms
tick. Anything heavier needs а separate timer / task / scenario.

❌ **Don't `vTaskDelay` / sleep у `on_update`.** Blocks every other
module. Use accumulator pattern:

```cpp
elapsed_ms_ += dt_ms;
if (elapsed_ms_ < interval_ms_) return;
elapsed_ms_ = 0;
// ... do periodic work ...
```

❌ **Don't write NVS у `on_update`.** Synchronous I/O, 5-50 ms per
write. Use `persist: true` flag on state keys, OR explicit `nvs_helper`
calls from а one-shot event handler.

❌ **Don't log on every tick.** UART floods, monitor lags. One ESP_LOGI
per state change, not per tick.

## State management

✅ **Read inputs at the top of `on_update`, write outputs at the bottom.**
Sequential per-tick pattern. Clear І to read.

✅ **Cast int literals to `int32_t`.** `state_set("key", 42)` is fragile
across hosts; use `state_set("key", static_cast<int32_t>(42))` або `42L`.

✅ **Use typed accessors:** `read_float(key, 0.0f)`, not `state_get(key)
+ etl::get_if`. Less code, fail-safe defaults.

✅ **Provide sensible defaults у `read_*`.** If а key isn't set yet (other
module hasn't ticked), the default is your fallback. Choose conservatively
(safe values, not zero unless that's safe).

❌ **Don't construct dynamic state keys.** `"my_module.item_" + std::to_string(i)`
fills the bounded state map fast and breaks compile-time validation. Declare
а fixed set у manifest.

❌ **Don't type-flip keys.** Once а key is set as float, all subsequent
writes must be float. Variant rejects type changes silently — log а
warning if you suspect це happens.

## Cross-module communication

✅ **Default to pure state-key data flow.** Module A writes а key,
module B reads it. No pointers, no message passing.

✅ **Declare producer modules before consumers у `project.json`.**
Determines update order within а priority bucket. Producer's writes from
the same tick visible to consumer's reads.

✅ **For edge detection, keep а `prev_value_` member.** No engine-level
edge tracking у MVP. Pattern:

```cpp
bool fault = read_bool("equipment.fault", false);
if (fault && !prev_fault_) { /* rising edge */ }
prev_fault_ = fault;
```

❌ **Don't poke other modules' internals.** No `manager.get_module<X>()`
patterns. State keys only.

❌ **Don't use messages for periodic data.** State keys are simpler і
more observable. Messages only для discrete events (alarm fired, mode
change command).

## Manifests і generation

✅ **One file per topic / unit.** Don't sprawl across many manifests for
related state. One module = one manifest.

✅ **Declare всі state keys upfront.** The generator emits constexpr
metadata; dynamic state keys break it.

✅ **Match manifest types to code types.** `"type": "float"` у manifest
means write із `float`/`f32`, not `int`. Generator doesn't cross-validate
але runtime variance hurts.

✅ **Keep key prefixes short.** `<module>.<key>` ≤ 32 chars budget,
recipe names ≤ 12, track names ≤ 8.

❌ **Don't forget `priority` field for non-default modules.** Default is
NORMAL (2). HAL or driver-hosting modules need HIGH (1).

❌ **Don't put `persist: true` on fast-changing keys.** Debounce window
is 5 s; counter incrementing per second flushes too often. Persist
user-controlled config, calibration constants, mode selections.

## Drivers

✅ **Implement health monitoring.** Track consecutive failures; set
`is_healthy() = false` after threshold. Business modules check `_ok`
suffix on the equipment.* keys.

✅ **Respect `min_switch_ms` for actuators.** Compressors і pumps fail
mechanically with rapid switching. Encode the lockout у the driver, not
expecting business module discipline.

✅ **Apply calibration at read time, not at store time.** Changes to
`offset` setting affect next read immediately, not after reboot.

❌ **Don't block у `update()`.** Slow protocols (DS18B20 conversion = 750
ms) need state-machine pattern across multiple ticks, not synchronous
waits.

❌ **Don't allocate per read.** Use stack buffers for parsing / formatting.

## Scenarios і recipes

✅ **Set а phase timeout always.** Even if conditions should always fire,
`timeout_ms` is defense у depth. ADR-0007 makes them mandatory.

✅ **Declare всі mirror keys у the recipe manifest's `state` section.**
Engine cross-validates і fails build otherwise.

✅ **Use transition `time_elapsed_ms` over `wait_ms` action.** Transitions
are zero-cost per tick; actions invoke а handler.

✅ **Declare producer tracks before consumer tracks.** Cross-track sync
is tick-order. Watcher track що waits for main's phase_name must come
after main у the `tracks` array.

❌ **Don't put side effects у conditions.** Conditions are pure reads.
Evaluated multiple times per tick during transition checks. Side effects
accumulate unpredictably.

❌ **Don't rely on snapshot consistency across tracks.** Tracks tick
serially. Track 1 reads track 0's same-tick writes (good). Track 0 can't
see track 1's writes from the same tick (bad assumption).

❌ **Don't write long recipes as one giant track.** Use parallel tracks
для monitors, supervisors, timeouts. Easier to reason about.

## MQTT і external API

✅ **Use the manifest, never raw esp_mqtt_client.** The framework handles
TLS, reconnect, throttling, HA discovery, LWT — все вашими словами:
`mqtt.publish: [...]`.

✅ **Validate the `mqtt_subscribe: true` flag** on every writable key
exposed via MQTT. Generator catches missing flags при build.

✅ **Plain ASCII payloads on `cmd/` topics.** Не JSON. `"24"`, не
`"{"value": 24}"`.

❌ **Don't expose internal config keys для external write.** Keep
`mqtt_subscribe` selective — user-facing setpoints, not debug counters.

❌ **Don't assume atomic multi-key writes via MQTT.** Each topic delivers
independently. Use а composite state key (e.g., JSON-encoded string)
if atomicity matters.

## Persistence

✅ **`persist: true` for user-tunable settings.** Setpoints, calibration,
operating modes. Survives reboot transparently.

✅ **Provide а `default` value** for every persisted key. First boot
(no NVS data) falls back gracefully.

✅ **For migration when renaming а persisted key**, add explicit migration
code у your module's `on_init` (read old key, write new key, erase old).

❌ **Don't `persist: true` counters / sensor readings.** Debounce can't
keep up; flash wear, slow boot.

❌ **Don't bypass PersistService casually.** Direct `nvs_helper` calls
у your own namespace are fine; mixing із the `"persist"` namespace causes
conflicts.

## Build і CI

✅ **Run `idf.py fullclean && idf.py build` after major changes.**
Generated headers can go stale; fullclean forces regeneration.

✅ **Use host tests for state-machine logic.** `tests/host/` runs з а
stub SharedState backend. Fast iteration без flashing.

✅ **Check the size budget periodically.** `idf.py size-components`
shows RAM / IRAM usage per component. Surprises у growth point at heap
leaks або static-init bloat.

❌ **Don't ignore compiler warnings.** `-Wall -Wextra` produces signal,
not noise. Address them or document why suppression is OK.

❌ **Don't commit code without flashing і testing на hardware.** Host
tests catch much але WiFi / NVS / display behaviors emerge only on the
real chip.

## Naming і structure

✅ **`snake_case` for state keys, module names, action names.** Reserved
for the framework, consistent across all manifests.

✅ **`CamelCase` для C++ class names.** Module class = `<Name>Module`
(suffix). Driver class = `<Name>Driver`.

✅ **Short module names** (≤ 12 chars). Long names eat state key budget.

✅ **Group related state keys із а common prefix.** All thermostat keys
under `simple_thermo.*`, all equipment under `equipment.*`.

❌ **Don't put framework-level keys у your module.** `system.time`,
`_ota.version` are reserved.

## Documentation і code style

✅ **One-line module description у `manifest.json`.** First sentence
should answer "what does це do?".

✅ **Document edge cases у file-header comments.** "Lifecycle: configure
→ init → update → emergency_stop". Specific, не generic.

✅ **ESP_LOGI states are valid runtime documentation.** "Setpoint changed
to 22.5" is debugger gold dust six months later.

❌ **Don't comment what the code does — comment why.** "Increment count"
is noise. "Increment until threshold; resetting у `reset()`" is useful.

## When у doubt

Read existing modules first:
1. **`simple_thermo`** — minimal service module. Best first read.
2. **`datalogger`** — bigger module із features і channels.
3. **`equipment`** — most coupled module, read after basics.
4. **`abs_test`** — recipe-only module із 2 parallel tracks.

Reading 50 lines of working code beats reading 500 lines of documentation.

## Next steps

This page is the end of the Module Author Guide. From here:

- **[scenario-engine/](../03-framework-reference/scenario-engine/)** —
  scenario engine internals if you want to understand or extend it.
- **[components/](../03-framework-reference/components/)** *(in progress)* —
  per-component framework reference.
- **[04-hardware/](../04-hardware/)** — board.json, bindings, OTA, deployment.
- **[06-contributing/](../06-contributing/)** *(planned)* — for contributing
  to the framework itself.
