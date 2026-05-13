# SharedState

> 📖 **Українською:** [documentation/uk/02-module-author-guide/shared-state.md](../../uk/02-module-author-guide/shared-state.md)

SharedState is the data backbone of ModESP. It's а typed, thread-safe,
in-memory key-value store що every module reads from і writes to. There
are no point-to-point messages between modules у the common case — they
exchange data only through state keys. This page covers the read/write
patterns, type system, change tracking, і common pitfalls.

By the end you'll know which API call to reach for, how to avoid common
data-loss bugs, і when state isn't the right tool (rare, but real).

## Mental model

Imagine one big `std::map<string, variant>` із а mutex around it. That's
SharedState. The map has bounded capacity (compile-time constant
`MODESP_MAX_STATE_ENTRIES`, auto-generated z manifests), fixed-size keys
(32 chars max), і а fixed type set for values:

```cpp
using StateValue = etl::variant<int32_t, float, bool, etl::string<32>>;
```

ETL containers everywhere — no heap, deterministic memory footprint.
Reads і writes are O(1) average through unordered_map hash lookup.

## What SharedState is for

- **Module-to-module data flow** — sensor values, control state, computed
  values, user setpoints.
- **System-to-WebUI broadcast** — every change з `track_change=true` queues
  а delta for WebSocket clients.
- **Module-to-MQTT publish** — keys у `mqtt.publish` arrays auto-publish
  on change.
- **Module-to-NVS persist** — keys із `persist: true` flag round-trip
  through PersistService.

## What SharedState is NOT for

- **Large payloads** — keys are ≤ 32 chars, string values ≤ 32 chars. Don't
  store JSON blobs, image data, log lines.
- **Cross-task messaging із semantics** — that's `etl::imessage` через
  `ModuleManager::send_message()`. Use for typed events із multiple fields.
- **High-frequency time-series** — datalogger has its own ring buffers
  з compact encoding. SharedState writes at >10 Hz spam WebSocket clients.
- **Persistent storage of non-config data** — use NVS directly через
  PersistService для blobs, custom encodings.

## API через BaseModule

Every module gets these helpers via inheritance. The full SharedState
interface is exposed для contexts that aren't modules (HTTP handlers,
ActionContext for scenarios, etc.).

### Writing

```cpp
// Typed overloads — pick the one matching your value type.
state_set("my_module.temperature", 23.5f);           // float
state_set("my_module.count", static_cast<int32_t>(42));  // int (note cast — avoid int→int32_t implicit fail)
state_set("my_module.active", true);                 // bool
state_set("my_module.label", "running");             // const char* → StringValue

// All return bool. false means: store rejected (capacity exhausted, key
// length > 32, or internal mutex failure). Track failures via SharedState's
// set_failures() counter для diagnostics.

// Silent update — doesn't trigger WebSocket broadcast. Use для:
// - Counters (`*_count` keys) — flooding WS із every increment is wasteful
// - Fast-changing internal state що UI doesn't need to display
state_set("my_module.tick_count", n, /*track_change=*/false);
```

### Reading

```cpp
// Typed reads із default fallback — return default if key missing or type mismatch.
float temp = read_float("equipment.air_temp", 0.0f);
int32_t count = read_int("my_module.count", 0);
bool active = read_bool("simple_thermo.output", false);

// Generic — returns etl::optional<StateValue>. Use коли type isn't fixed
// or you need to detect type mismatch explicitly.
auto opt = state_get("some.key");
if (!opt.has_value()) {
    // Key missing.
} else {
    auto& v = *opt;
    if (auto* f = etl::get_if<float>(&v)) {
        // It's а float, use *f
    } else if (auto* s = etl::get_if<modesp::StringValue>(&v)) {
        // It's а string, use s->c_str()
    }
}
```

## Type system rules

The variant has 4 cases. Each key locks to its first-set type:

```cpp
state_set("foo", 1.5f);       // foo is now float
state_set("foo", true);       // ← REJECTED. Returns false. foo stays float.

// To change type: remove first, then re-set.
state_->remove("foo");
state_set("foo", true);       // OK now
```

**Common surprise:** integer literals у C++ are `int`, not `int32_t`. If
you forget the cast, overload resolution picks `int32_t` only if `int` ≤
32 bits (true on ESP32 and host). On 64-bit hosts the call is ambiguous
or picks the wrong overload — always cast або use explicit suffix:

```cpp
state_set("foo", static_cast<int32_t>(42));  // safe
state_set("foo", 42L);                       // long — picks the right overload
state_set("foo", 42);                        // works on ESP32, fragile on host tests
```

## Change tracking і WebSocket broadcast

Every `set` call з `track_change=true` (the default) appends the key to а
`changed_keys_` vector. The WebSocket service flushes the vector every
~500 ms by:

1. If `changed_keys_.size() ≤ MAX_CHANGED_KEYS` (32): send а delta payload
   із just those keys і their current values.
2. If above 32 (overflow): send а **full state snapshot** to all subscribed
   clients. Marker: `SharedState::needs_full_broadcast()` returns `true`.

**Performance implication:** if your module writes > 32 distinct keys per
500 ms tick, every WS broadcast is full-state. На pull-heavy WebUIs this
becomes visible latency. Use `track_change=false` for high-frequency keys
that don't need real-time UI display.

## State changes that persist

If а state key declares `"persist": true` у the manifest, PersistService
hooks SharedState through `set_persist_callback`. On every change to such
а key, PersistService:

1. Throttles writes (one per 30 s per key by default).
2. Serialises the value до NVS under key `state.<key_name>`.
3. On boot, restores the value before any module's `on_init()` runs.

Your module reads the persisted value через а regular `read_float/int/bool`
call — no special API. The PersistService transparently makes the value
"sticky".

**Limits:**
- Throttle = 30 s default. Adjust у PersistService config if needed
  ([components/modesp_services.md](../03-framework-reference/components/modesp_services.md)
  *(planned)*).
- Value sizes pay for themselves on NVS — 16-byte typed values, including
  short strings, are fine. Don't `persist: true` keys що change every tick.

## Access patterns

### Pull pattern (sensor reading)

Module reads inputs at top of `on_update`, computes, writes outputs:

```cpp
void ThermoModule::on_update(uint32_t dt_ms) {
    float temp = read_float("equipment.air_temp", 0.0f);
    float setpoint = read_float("simple_thermo.setpoint", 22.0f);
    bool need_heat = (temp < setpoint - hysteresis_);
    state_set("simple_thermo.output", need_heat);
}
```

This is the **default pattern**. Modules don't subscribe или get notified —
each tick they re-read what they need.

### Edge detection

To trigger logic only on transitions, keep а `prev_value_` member і compare:

```cpp
void Module::on_update(uint32_t dt_ms) {
    bool fault = read_bool("equipment.fault", false);
    if (fault && !prev_fault_) {
        // Rising edge: fault just appeared
        state_set("my_module.fault_count", ++count_);
    }
    prev_fault_ = fault;
}
```

> 💡 **Tip:** the built-in scenario condition `state_key_changed{key}` is
> а placeholder у MVP (returns "no edge"). True edge detection is module
> author's responsibility. Stage 1.5 will wire engine-side edge tracking.

### Atomic compute-and-write

If your computation depends on the current value, read-compute-write does
NOT race because all module updates happen serially у the same task — but
only if you stay within one `on_update` call:

```cpp
// Safe — single tick, single thread.
void on_update(uint32_t dt_ms) {
    int32_t n = read_int("my_module.count", 0);
    state_set("my_module.count", n + 1);
}

// NOT safe — split across ticks lets another task interleave.
// (HTTP handler could write between ticks.)
```

If а value can be written by HTTP / MQTT (`access: "readwrite"`), assume
external writes interleave. For counters, prefer per-tick deltas, not
cumulative reads.

## Iteration і bulk operations

Sometimes you need to scan all keys (debug dump, full snapshot для
WebSocket initial sync):

```cpp
state.for_each([](const StateKey& key, const StateValue& value, void* user) {
    // Callback runs UNDER the mutex. Don't:
    //   - Block (sleep, log to UART, NVS write)
    //   - Call other SharedState methods (deadlock)
    //   - Throw exceptions (no exceptions у this codebase anyway)
    //
    // Do:
    //   - Append to а buffer (your `user` context)
    //   - Filter by key prefix
}, this);
```

For change-tracking-aware iteration (WebSocket delta path):

```cpp
bool had_changes = state.for_each_changed_and_clear(callback, ctx);
// `changed_keys_` is reset atomically. Future writes start а fresh delta.
```

## Capacity і budget

The compile-time constant `MODESP_MAX_STATE_ENTRIES` (declared у
`state_meta.h`, auto-generated) caps the total number of keys. Current
default is 96, sized to fit all declared keys across modules у the project
plus headroom для recipe mirror keys, OTA status, system stats.

**If you hit the cap:**

1. Look at your manifest's `state` section — every entry counts.
2. Check `state_meta.h`'s `MODESP_MAX_STATE_ENTRIES` value.
3. Bump у Kconfig (`CONFIG_MODESP_MAX_STATE_ENTRIES`) ONLY if you've truly
   added keys і need them all. Cost is ~80 bytes RAM per entry.

> ⚠️ **Warning:** every `state_set` call із а new key allocates an entry.
> If your module dynamically constructs key names (`"my_module.item_X"` за
> some `X`), you'll fill the table fast і `set()` starts returning `false`.
> Dynamic keys are an anti-pattern — declare what you'll write і keep the
> set fixed.

## Thread safety

SharedState methods acquire а FreeRTOS mutex. Safe to call from:

- Module `on_init`, `on_update`, `on_stop` (always)
- Module `on_message` (always)
- HTTP request handlers (yes, they run on httpd task)
- MQTT subscribe handlers (yes, separate task)
- Recipe action handlers (engine task)
- ISR? **NO.** Mutex acquisition can block. Use а task-side queue if you
  need to pipe ISR data to SharedState.

The mutex timeout is configured to 100 ms; if contention starves you
що long, `set` returns false і increments `set_failures_`. Check
`state.set_failures()` periodically — non-zero hints at contention bugs.

## Diagnostic API

- `state.size()` — current entry count.
- `state.version()` — monotonic counter, incremented on every tracked
  `set`. Useful для polling clients.
- `state.has_changes()` — true if `changed_keys_` non-empty.
- `state.needs_full_broadcast()` — true if last delta overflowed.
- `state.set_failures()` — count of `set()` calls що returned false. If
  non-zero, dig into logs.

External diagnostic endpoint: `GET /api/state` returns all keys і values
as JSON. Useful for debugging без needing а monitor connection.

## When to use messages instead

SharedState is best for **continuous data flow**. Messages
(`etl::imessage` через `ModuleManager::send_message`) are best for:

- **Discrete events із typed payload** — "OTA download started, size = N
  bytes, partition = ota_1". State keys would need 3 separate writes іна
  consistency guarantees.
- **Cross-module commands** — "shutdown gracefully", "reload config".
- **One-shot signals** — fire-and-forget, no state needed afterwards.

Messages у ModESP are stateless і need to be declared у С++ via etl
message classes. We don't generate them from manifests. Use sparingly;
most things fit better у SharedState.

## Common mistakes

**Reading а key before any module sets it:** `read_float("key", 0.0f)`
returns `0.0f` because the key doesn't exist. Your business logic might
behave incorrectly (е.g., compute setpoint relative to "current temp" while
temp is 0). Always provide а sensible default OR check `state_get().has_value()`
explicitly before computing.

**Forgetting `static_cast<int32_t>(...)` для int literals:** integer
literals are `int`, which on 64-bit hosts (test environments) is 64-bit.
Overload resolution fails or picks wrong overload. Always cast або use
literal suffixes.

**Spamming high-frequency writes із `track_change=true`:** every change
goes до WebSocket delta queue. > 64 distinct keys per delta window forces
full snapshots, which costs CPU і WS bandwidth. Use `false` для
fast-changing internal counters.

**Forgetting types match manifest:** declaring `"type": "int"` у manifest
але writing з `state_set("key", 1.5f)` — the set succeeds (variant is
type-locked to float), але the manifest now lies. WebUI displays а number
input із int constraints для а float value. Match types between manifest
і code.

**Recipe mirror keys writing без manifest declaration:** scenario engine
writes mirror keys (`<recipe>.scenario_state`, etc.) automatically. If
they're not pre-declared у the recipe's `state` section, they go to
SharedState fine але `state_meta.h` doesn't know them, і UI widgets що
reference them silently fail. Always declare what the engine will write.

## Next steps

- **[ui-widgets.md](ui-widgets.md)** *(planned)* — how WebUI renders your
  state keys.
- **[mqtt.md](mqtt.md)** *(planned)* — auto-publish patterns, subscribe
  semantics.
- **[persistence.md](persistence.md)** *(planned)* — `persist: true` flag,
  PersistService internals.
- **[debugging.md](debugging.md)** *(planned)* — using `/api/state` і WS
  to inspect SharedState live.
- **[components/modesp_core.md](../03-framework-reference/components/modesp_core.md)**
  *(planned)* — full SharedState reference (raw interface, для non-module
  callers).
