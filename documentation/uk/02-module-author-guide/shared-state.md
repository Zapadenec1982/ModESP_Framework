# SharedState

> 📖 **In English:** [documentation/en/02-module-author-guide/shared-state.md](../../en/02-module-author-guide/shared-state.md)

SharedState — це data backbone ModESP. Це типізована, thread-safe,
in-memory key-value сховище що кожен модуль читає і пише. У common case
point-to-point messages між модулями нема — вони обмінюються даними лише
через state keys. Ця сторінка покриває read/write патерни, type system,
change tracking, і поширені pitfalls.

Прочитавши, ви знатимете який API call reach-ити, як уникнути поширених
data-loss багів, і коли state — не правильний інструмент (рідко, але
реально).

## Ментальна модель

Уявіть одну велику `std::map<string, variant>` з mutex навколо неї. Це
SharedState. Map має bounded capacity (compile-time константа
`MODESP_MAX_STATE_ENTRIES`, auto-generated з маніфестів), fixed-size keys
(32 chars max), і fixed type set для values:

```cpp
using StateValue = etl::variant<int32_t, float, bool, etl::string<32>>;
```

ETL containers всюди — без heap, deterministic memory footprint. Reads і
writes — O(1) average через unordered_map hash lookup.

## Для чого SharedState

- **Module-to-module data flow** — sensor values, control state, computed
  values, user setpoints.
- **System-to-WebUI broadcast** — кожна зміна з `track_change=true`
  queues delta для WebSocket clients.
- **Module-to-MQTT publish** — keys у `mqtt.publish` arrays auto-publish
  при зміні.
- **Module-to-NVS persist** — keys із `persist: true` flag round-trip
  через PersistService.

## Для чого SharedState НЕ

- **Large payloads** — keys ≤ 32 chars, string values ≤ 32 chars. Не
  зберігайте JSON blobs, image data, log lines.
- **Cross-task messaging із семантикою** — це `etl::imessage` через
  `ModuleManager::send_message()`. Use для typed events з multiple fields.
- **High-frequency time-series** — datalogger має власні ring buffers з
  compact encoding. SharedState writes при >10 Hz спамлять WebSocket clients.
- **Persistent storage non-config даних** — використовуйте NVS напряму
  через PersistService для blobs, custom encodings.

## API через BaseModule

Кожен модуль отримує ці helpers через inheritance. Повний SharedState
interface exposed для contexts що не модулі (HTTP handlers, ActionContext
для scenarios, тощо).

### Запис

```cpp
// Typed overloads — оберіть той що match-ить ваш value type.
state_set("my_module.temperature", 23.5f);           // float
state_set("my_module.count", static_cast<int32_t>(42));  // int (зверніть увагу cast)
state_set("my_module.active", true);                 // bool
state_set("my_module.label", "running");             // const char* → StringValue

// Всі return bool. false означає: store rejected (capacity exhausted,
// key length > 32, або internal mutex failure). Track failures через
// SharedState's set_failures() counter для diagnostics.

// Silent update — не тригерить WebSocket broadcast. Use для:
// - Counters (`*_count` keys) — flood-ити WS кожним incrementом — wasteful
// - Fast-changing internal state що UI не потребує display
state_set("my_module.tick_count", n, /*track_change=*/false);
```

### Читання

```cpp
// Typed reads з default fallback — return default якщо key відсутній або
// type mismatch.
float temp = read_float("equipment.air_temp", 0.0f);
int32_t count = read_int("my_module.count", 0);
bool active = read_bool("simple_thermo.output", false);

// Generic — повертає etl::optional<StateValue>. Use коли тип не фіксований
// або вам потрібно detect type mismatch explicitly.
auto opt = state_get("some.key");
if (!opt.has_value()) {
    // Key відсутній.
} else {
    auto& v = *opt;
    if (auto* f = etl::get_if<float>(&v)) {
        // Це float, use *f
    } else if (auto* s = etl::get_if<modesp::StringValue>(&v)) {
        // Це string, use s->c_str()
    }
}
```

## Правила type system

Variant має 4 cases. Кожен key locks на його first-set type:

```cpp
state_set("foo", 1.5f);       // foo тепер float
state_set("foo", true);       // ← REJECTED. Returns false. foo лишається float.

// Щоб змінити type: спочатку remove, потім re-set.
state_->remove("foo");
state_set("foo", true);       // OK тепер
```

**Common surprise:** integer literals у C++ — це `int`, не `int32_t`. Якщо
забудете cast, overload resolution обирає `int32_t` лише якщо `int` ≤
32 bits (true на ESP32 і host). На 64-bit hosts call ambiguous або обирає
неправильний overload — завжди cast-уйте або use explicit suffix:

```cpp
state_set("foo", static_cast<int32_t>(42));  // safe
state_set("foo", 42L);                       // long — picks right overload
state_set("foo", 42);                        // works на ESP32, fragile у host tests
```

## Change tracking і WebSocket broadcast

Кожен `set` call з `track_change=true` (default) appends key до
`changed_keys_` vector. WebSocket service flushes vector кожні ~500 мс:

1. Якщо `changed_keys_.size() ≤ MAX_CHANGED_KEYS` (32): надсилає delta
   payload з лише цими keys і їхніми current values.
2. Якщо вище 32 (overflow): надсилає **full state snapshot** усім
   subscribed clients. Marker: `SharedState::needs_full_broadcast()`
   повертає `true`.

**Performance implication:** якщо ваш модуль пише > 32 distinct keys per
500 мс tick, кожен WS broadcast — full-state. На pull-heavy WebUIs це
стає visible latency. Use `track_change=false` для high-frequency keys
що не потребують real-time UI display.

## State changes що persist

Якщо state key декларує `"persist": true` у маніфесті, PersistService
hooks SharedState через `set_persist_callback`. На кожній зміні такого
key, PersistService:

1. Throttles writes (один per 30 с per key за замовчуванням).
2. Serializes value до NVS під key `state.<key_name>`.
3. При boot, restore-ить value перед запуском будь-якого модуля `on_init()`.

Ваш модуль читає persisted value через regular `read_float/int/bool` call
— без спеціального API. PersistService transparently робить value "sticky".

**Limits:**
- Throttle = 30 с default. Налаштовується у PersistService config якщо
  треба
  ([components/modesp_services.md](../03-framework-reference/components/modesp_services.md)
  *(planned)*).
- Value sizes pay-aть for themselves на NVS — 16-byte typed values,
  including short strings — fine. Не `persist: true` keys що змінюються
  кожен tick.

## Патерни доступу

### Pull pattern (sensor reading)

Module читає inputs у верху `on_update`, computes, writes outputs:

```cpp
void ThermoModule::on_update(uint32_t dt_ms) {
    float temp = read_float("equipment.air_temp", 0.0f);
    float setpoint = read_float("simple_thermo.setpoint", 22.0f);
    bool need_heat = (temp < setpoint - hysteresis_);
    state_set("simple_thermo.output", need_heat);
}
```

Це **default pattern**. Модулі не subscribe-яться і не get notified —
кожен tick вони re-read що їм треба.

### Edge detection

Щоб trigger логіку лише на transitions, тримайте `prev_value_` member і
порівнюйте:

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

> 💡 **Tip:** built-in scenario condition `state_key_changed{key}` — це
> placeholder у MVP (повертає "no edge"). True edge detection — module
> author's responsibility. Stage 1.5 буде wire engine-side edge tracking.

### Atomic compute-and-write

Якщо ваше обчислення залежить від current value, read-compute-write НЕ
race-ається бо всі module updates відбуваються serially у тому ж task —
але лише якщо ви stay у межах одного `on_update` call:

```cpp
// Safe — single tick, single thread.
void on_update(uint32_t dt_ms) {
    int32_t n = read_int("my_module.count", 0);
    state_set("my_module.count", n + 1);
}

// NOT safe — split across ticks дозволяє іншому task interleave.
// (HTTP handler може write між ticks.)
```

Якщо value може бути написане HTTP / MQTT (`access: "readwrite"`),
assume external writes interleave. Для counters, prefer per-tick deltas,
не cumulative reads.

## Iteration і bulk operations

Іноді вам треба scan all keys (debug dump, full snapshot для WebSocket
initial sync):

```cpp
state.for_each([](const StateKey& key, const StateValue& value, void* user) {
    // Callback runs UNDER the mutex. Не:
    //   - Block (sleep, log to UART, NVS write)
    //   - Call інші SharedState методи (deadlock)
    //   - Throw exceptions (no exceptions у цьому codebase anyway)
    //
    // Do:
    //   - Append to buffer (your `user` context)
    //   - Filter by key prefix
}, this);
```

Для change-tracking-aware iteration (WebSocket delta path):

```cpp
bool had_changes = state.for_each_changed_and_clear(callback, ctx);
// `changed_keys_` reset-иться atomically. Майбутні writes start fresh delta.
```

## Capacity і budget

Compile-time constant `MODESP_MAX_STATE_ENTRIES` (declared у
`state_meta.h`, auto-generated) caps загальну кількість keys. Поточний
default — 96, sized щоб fit all declared keys across modules у проекті
plus headroom для recipe mirror keys, OTA status, system stats.

**Якщо ви hit cap:**

1. Look на ваш manifest's `state` section — кожна entry рахується.
2. Check `state_meta.h`'s `MODESP_MAX_STATE_ENTRIES` value.
3. Bump у Kconfig (`CONFIG_MODESP_MAX_STATE_ENTRIES`) ТІЛЬКИ якщо ви
   справді додали keys і потребуєте їх усі. Cost — ~80 bytes RAM per entry.

> ⚠️ **Warning:** кожен `state_set` call із новим key allocates entry.
> Якщо ваш модуль dynamically constructs key names (`"my_module.item_X"`
> per якийсь `X`), ви заповните table швидко і `set()` починає
> повертати `false`. Dynamic keys — anti-pattern. Декларуйте що ви будете
> писати і тримайте set фіксованим.

## Thread safety

SharedState методи acquire FreeRTOS mutex. Safe для виклику з:

- Module `on_init`, `on_update`, `on_stop` (завжди)
- Module `on_message` (завжди)
- HTTP request handlers (так, вони run на httpd task)
- MQTT subscribe handlers (так, separate task)
- Recipe action handlers (engine task)
- ISR? **НІ.** Mutex acquisition може block. Use task-side queue якщо вам
  треба pipe ISR data до SharedState.

Mutex timeout сконфігурований до 100 мс; якщо contention starves вас на
це довго, `set` returns false і increments `set_failures_`. Перевіряйте
`state.set_failures()` періодично — non-zero натякає на contention bugs.

## Diagnostic API

- `state.size()` — current entry count.
- `state.version()` — monotonic counter, increment-иться при кожному
  tracked `set`. Корисно для polling clients.
- `state.has_changes()` — true якщо `changed_keys_` non-empty.
- `state.needs_full_broadcast()` — true якщо останній delta overflowed.
- `state.set_failures()` — count `set()` calls що returned false.
  Якщо non-zero, dig у логи.

External diagnostic endpoint: `GET /api/state` повертає всі keys і values
як JSON. Корисно для debugging без monitor connection.

## Коли use messages замість

SharedState найкраща для **continuous data flow**. Messages
(`etl::imessage` через `ModuleManager::send_message`) найкращі для:

- **Дискретні events з typed payload** — "OTA download started, size = N
  bytes, partition = ota_1". State keys потребували б 3 separate writes
  і нема consistency guarantees.
- **Cross-module commands** — "shutdown gracefully", "reload config".
- **One-shot signals** — fire-and-forget, no state needed afterwards.

Messages у ModESP — stateless і треба declare-ти у C++ через etl message
classes. Ми не генеруємо їх з маніфестів. Use sparingly; більшість речей
fit-яться краще у SharedState.

## Поширені помилки

**Читання key перед тим як будь-який модуль set-ить його:**
`read_float("key", 0.0f)` повертає `0.0f` бо key не існує. Ваша business
логіка може поводитись неправильно (наприклад, compute setpoint relative
до "current temp" коли temp — 0). Завжди надавайте sensible default АБО
check `state_get().has_value()` explicitly перед computing.

**Забутий `static_cast<int32_t>(...)` для int literals:** integer literals
— `int`, який на 64-bit hosts (test environments) — 64-bit. Overload
resolution fails або обирає wrong overload. Завжди cast-уйте або use
literal suffixes.

**Spamming high-frequency writes з `track_change=true`:** кожна зміна йде
до WebSocket delta queue. > 64 distinct keys per delta window forces
full snapshots, що costs CPU і WS bandwidth. Use `false` для fast-changing
internal counters.

**Забутий types match manifest:** declar-ючи `"type": "int"` у маніфесті
але writing з `state_set("key", 1.5f)` — set succeeds (variant type-locks
на float), але manifest тепер lies. WebUI displays number input з int
constraints для float value. Match types між manifest і code.

**Recipe mirror keys writing без manifest declaration:** scenario engine
пише mirror keys (`<recipe>.scenario_state`, etc.) автоматично. Якщо
вони не pre-declared у recipe's `state` section, вони йдуть у SharedState
fine але `state_meta.h` не знає їх, і UI widgets що reference-ять їх
silently fail. Завжди declare-уйте що engine буде писати.

## Що далі

- **[ui-widgets.md](ui-widgets.md)** *(planned)* — як WebUI рендерить
  ваші state keys.
- **[mqtt.md](mqtt.md)** *(planned)* — auto-publish патерни, subscribe
  семантика.
- **[persistence.md](persistence.md)** *(planned)* — `persist: true`
  flag, PersistService internals.
- **[debugging.md](debugging.md)** *(planned)* — using `/api/state` і WS
  щоб inspect SharedState live.
- **[components/modesp_core.md](../03-framework-reference/components/modesp_core.md)**
  *(planned)* — повний SharedState reference (raw interface, для non-module
  callers).
