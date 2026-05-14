# 03 — C++ API Reference

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/03_api_reference.md](../../../uk/03-framework-reference/scenario-engine/03_api_reference.md)

Authoritative reference for the public C++ API surface of the `modesp::scenario`
namespace. Source: `components/modesp_scenario/include/modesp/scenario/`.

## Constants

```cpp
namespace modesp::scenario {

// Slot pool — configurable via Kconfig CONFIG_MODESP_MAX_SEQUENCES
constexpr size_t   MAX_SEQUENCES = 2;            // default; range typically 2..8

// Track count caps
constexpr uint8_t  MAX_TRACKS_PER_SCENARIO = 6;
// MAX_TOTAL_TRACKS = MAX_SEQUENCES × MAX_TRACKS_PER_SCENARIO

// Composite condition tree depth
constexpr uint8_t  MAX_CONDITION_DEPTH = 16;

// Resource ownership map size
constexpr size_t   MAX_RESOURCES = 32;

// Sentinel for non-real track index (= scenario-scope ownership)
constexpr TrackIdx TRACK_IDX_SCENARIO = 0xFF;

// Registry capacities
constexpr size_t   MAX_REGISTRY_ENTRIES = 64;          // actions AND conditions, separate pools
constexpr size_t   MAX_CONTINUOUS_REGISTRATIONS = 32;  // continuous factories

// Composite condition sentinel hashes
constexpr uint16_t MODR_COND_HASH_ALL_OF;        // djb2_hash16("all_of")
constexpr uint16_t MODR_COND_HASH_ANY_OF;        // djb2_hash16("any_of")
constexpr uint16_t MODR_COND_HASH_NOT;           // djb2_hash16("not")

// File format
constexpr uint32_t MODR_MAGIC = 0x52444F4D;      // 'MODR' LE
constexpr uint16_t MODR_FORMAT_VERSION = 1;
constexpr size_t   MODR_MAX_SIZE = 4 * 1024;     // default 4 KB; Kconfig-configurable

// Persistence token — magic bumped from old 'SQTK' during the rebuild
constexpr uint32_t SEQ_TOKEN_MAGIC = 0x4B544353; // 'SCTK' LE
constexpr uint16_t SEQ_TOKEN_VERSION = 1;
constexpr size_t   SEQ_TOKEN_SIZE = 96;

}
```

> **Note on the magic bump:** the persistence token magic changed from `'SQTK'`
> (0x4B545153) in the legacy `modesp_sequence` engine to `'SCTK'` (0x4B544353)
> in `modesp_scenario`. Old tokens in NVS are silently rejected after firmware
> upgrade — affected scenarios start fresh.

## Type aliases

```cpp
using SequenceHandle = uint8_t;   // 1..MAX_SEQUENCES, 0 = invalid
using TrackIdx       = uint8_t;   // 0..MAX_TRACKS_PER_SCENARIO-1
```

## Enums

### `EngineError` — uniform error code

19 values. Full table in [10_error_model.md](10_error_model.md#engineerror-codes).

```cpp
enum class EngineError : uint8_t {
    OK = 0,
    INVALID_FILE, UNSUPPORTED_VERSION, CRC_MISMATCH, BUFFER_OVERFLOW,
    UNKNOWN_ACTION, UNKNOWN_CONDITION, INVALID_TRANSITION,
    TOO_MANY_TRACKS, NAME_TOO_LONG,
    NO_SLOT, NOT_LOADED, INVALID_HANDLE, INVALID_TRACK,
    PARAM_OUT_OF_RANGE, PARAM_NOT_OVERRIDABLE,
    RESOURCE_CONTENDED, NVS_ERROR, ABORTED_BY_SAFETY,
};
constexpr bool is_ok(EngineError e);  // helper
```

### `ActionStatus` — action handler return code

```cpp
enum class ActionStatus : uint8_t {
    OK = 0,                ///< action complete, engine advances
    PENDING = 1,           ///< re-call next tick (long-running ops)
    FAILED_RECOVERABLE = 2,///< log + skip remaining actions in the phase, continue with transitions
    FAILED_ABORT = 3,      ///< track → TRACK_FAILED
};
```

### `ParamType` — typed parameter discriminator

```cpp
enum class ParamType : uint8_t {
    I32 = 0, F32 = 1, BOOL = 2, STR = 3,
};
```

### State enums

```cpp
struct SequenceRuntime {
    enum class State : uint8_t {
        IDLE = 0,        ///< slot empty
        LOADED,          ///< .modr loaded and validated, awaiting start()
        RUNNING,
        PAUSED,
        ABORTING,        ///< abort in progress; tracks running exit actions
                         ///< (per-phase $abort) or forced FAILED (scenario abort)
        COMPLETED,       ///< completion_rule satisfied
        FAILED,          ///< abort path completed or terminal failure
    };
};

struct TrackRuntime {
    enum class State : uint8_t {
        IDLE = 0,                ///< before scenario start
        RUNNING,                 ///< executing phase entry/dwell/transitions
        WAITING_FOR_RESOURCE,    ///< phase entry blocked by phase-scope claim
        ABORTING,                ///< per-phase $abort fired; running exit actions
        COMPLETED,               ///< reached MODR_TARGET_COMPLETE
        FAILED,                  ///< $abort, FAILED_ABORT, or scenario-level abort
    };
};
```

## Core types

### `ActionParam` — typed parameter

Wire-format equivalent of `modr_param_entry`. POD, exactly 8 bytes.

```cpp
struct ActionParam {
    uint16_t key_hash;     ///< djb2_hash16 of param name
    uint8_t  type;         ///< ParamType value
    uint8_t  flags;        ///< MODR_PARAM_FLAG_*
    union {
        int32_t  i;
        float    f;
        bool     b;
        uint16_t s_idx;    ///< string pool offset (when type == STR)
    } v;
};
```

### `ActionContext` — passed to each action/condition handler

```cpp
struct ActionContext {
    IStateBackend*       state;             ///< injected state backend (nullable in tests)
    const ActionParam*   params;            ///< array of param_count entries
    uint8_t              param_count;
    uint8_t              _pad0;
    uint16_t             string_pool_size;
    const char*          string_pool;       ///< used to resolve STR-type params
    uint32_t             scenario_elapsed_ms;
    uint32_t             phase_elapsed_ms;
    uint8_t              phase_idx;         ///< current phase within track
    SequenceHandle       handle;
    TrackIdx             track;
    uint8_t              _pad1;
    const char*          recipe_name;       ///< from recipe header (string pool)
    const char*          track_name;        ///< from track entry (string pool)
};
```

`state` is `IStateBackend*` — the engine no longer hard-couples to
`modesp::SharedState`. Action handlers read/write state via
`ctx.state->get<T>(key, out)` / `ctx.state->set<T>(key, value)`. See
[`IStateBackend`](#istatebackend--state-backend-interface) below.

### `ActionDescriptor` — registry entry

```cpp
using ActionFn = ActionStatus (*)(ActionContext&);

struct ActionDescriptor {
    uint16_t hash;          ///< MUST equal djb2_hash16(name)
    const char* name;
    ActionFn fn;
    uint8_t param_min;      ///< min params engine validates (caller's check)
    uint8_t param_max;      ///< max params
};
```

## `IStateBackend` — state backend interface

Source: `i_state_backend.h`. The engine talks to the surrounding state store
**only** through this interface. Production wires `modesp::SharedState` via a
thin adapter (lives in `main/`); host tests use `StubStateBackend`.

```cpp
class IStateBackend {
public:
    virtual ~IStateBackend() = default;

    // Two raw virtuals — backend implements these
    virtual bool get_raw(const char* key, modesp::StateValue& out) const = 0;
    virtual bool set_raw(const char* key, const modesp::StateValue& value) = 0;

    // Non-virtual typed helpers (zero v-table cost)
    template <typename T> bool get(const char* key, T& out) const;
    template <typename T> bool set(const char* key, T value);
    bool set(const char* key, const char* value);   // string-literal overload
};
```

Two raw virtuals over the existing `modesp::StateValue` variant
(int32/float/bool/string); typed `get<T>`/`set<T>` are header-inline templates
that fall through to the variant — no per-type v-table cost.

## `Engine` — public API

`class Engine : public modesp::BaseModule`. **Caller-owned**, no singleton.
All collaborators are injected through the constructor.

### Construction and wiring

```cpp
// 1. State backend adapter — implements IStateBackend over SharedState
static SharedStateBackend sb{shared_state};

// 2. Caller-owned registries (populated before engine.start())
static modesp::scenario::ActionRegistry     actions;
static modesp::scenario::ContinuousRegistry continuous;

// 3. NVS persistence observer (optional — omit if you do not need recovery)
static modesp::scenario::NvsObserver        nvs_obs{nvs_write_fn, nvs_read_fn, nullptr};
static modesp::scenario::IEngineObserver*   obs_list[] = {&nvs_obs};

// 4. Engine with dependencies injected
static modesp::scenario::Engine engine{sb, actions, continuous, obs_list};

// 5. Populate registries — must complete before any scenario uses them
modesp::scenario::builtins::register_builtins(actions);
modesp::scenario::primitives::register_primitives(continuous);  // optional standard primitives

// 6. Bind observer back to engine (needed for serialization in callbacks)
nvs_obs.bind_engine(engine);

// 7. Register engine with ModuleManager
manager.register_module(engine);
```

Constructor:

```cpp
Engine(IStateBackend& state,
       ActionRegistry& actions,
       ContinuousRegistry& continuous,
       etl::span<IEngineObserver*> observers = {});
```

Reference lifetimes must outlive the engine. The observer span is constexpr-known
— there is no runtime add/remove API.

### BaseModule interface (managed by ModuleManager)

```cpp
bool on_init() override;            // resets all slots, clears arbiter
void on_update(uint32_t dt_ms) override;
                                    // ticks running instances and dispatches
                                    // lifecycle events to observers
void on_stop() override;            // unloads all slots
```

### Recipe loading

```cpp
SequenceHandle load_buffer(const uint8_t* data, size_t size);
SequenceHandle load_path(const char* path);
EngineError unload(SequenceHandle h);
```

`load_buffer` copies bytes into the engine-owned slot buffer (max MODR_MAX_SIZE).
Returns 0 on failure; check `last_error()`.

`load_path` reads the file via `std::fopen` (target: ESP-IDF VFS LittleFS).
Returns 0 on file-not-found, read error, or validation failure.

### Lifecycle

```cpp
EngineError start(SequenceHandle h);                     // LOADED → RUNNING
EngineError pause(SequenceHandle h);                     // RUNNING → PAUSED
EngineError resume(SequenceHandle h);                    // PAUSED → RUNNING
EngineError abort(SequenceHandle h, uint8_t reason = 0); // → ABORTING
```

`start` atomically acquires scenario-scope resources. Returns `RESOURCE_CONTENDED`
on conflict with no partial state.

`abort` forces tracks to FAILED (releasing phase-scope resources). Per plan
Q7 MVP scope: scenario-level abort does NOT run phase exit actions —
recipe authors use global transitions to dedicated cleanup phases.

### Persistence (Stage 1)

NVS callbacks are **not** set on the engine. They are owned by `NvsObserver`,
which is registered with the engine through the constructor's observer span.

```cpp
using NvsWriteFn = bool (*)(void* user, uint8_t slot,
                            const uint8_t* token, size_t len);
using NvsReadFn  = bool (*)(void* user, uint8_t slot,
                            uint8_t* buf, size_t* in_out_len);

// Recovery is a round-trip operation — caller passes the NvsObserver explicitly
// rather than the engine id-casting through the observer list.
EngineError try_recover(SequenceHandle h, NvsObserver& nvs);
```

`try_recover` requires the slot to already be in LOADED state (recipe re-loaded
after boot). It calls the observer's read callback, deserializes the token,
applies the state, and sets the scenario state to PAUSED. See
[07_persistence.md](07_persistence.md).

### Diagnostics

```cpp
SequenceRuntime::State state(SequenceHandle h) const;
EngineError last_error() const;
uint32_t scenario_elapsed_ms(SequenceHandle h) const;
uint8_t  track_count(SequenceHandle h) const;
TrackRuntime::State track_state(SequenceHandle h, TrackIdx t) const;
uint8_t  track_phase_idx(SequenceHandle h, TrackIdx t) const;
uint32_t track_phase_elapsed_ms(SequenceHandle h, TrackIdx t) const;
uint8_t  active_count() const;          ///< counts RUNNING + PAUSED slots

ResourceArbiter&    arbiter();          ///< direct arbiter access (tests, HTTP diagnostics)
ActionRegistry&     actions();          ///< convenience accessor (same ref injected at construction)
ContinuousRegistry& continuous();       ///< likewise

SequenceRuntime*       runtime_for(SequenceHandle h);        ///< persistence observer use
const SequenceRuntime* runtime_for(SequenceHandle h) const;  ///< likewise
```

All diagnostics return safe defaults (IDLE state, 0 counts) for invalid
handles — they never trap.

## `IEngineObserver` — lifecycle hooks

Source: `i_engine_observer.h`. Engine emits three edge-triggered events plus a
tick hook synchronously from the update task. Empty default bodies — overrides
only what you care about. **Observers must not mutate engine state.**

```cpp
class IEngineObserver {
public:
    virtual ~IEngineObserver() = default;

    virtual void on_scenario_started(SequenceHandle h) {}
    virtual void on_phase_entered(SequenceHandle h, TrackIdx t, uint8_t phase_idx) {}
    virtual void on_scenario_terminal(SequenceHandle h, SequenceRuntime::State final_state) {}
    virtual void on_tick(uint32_t dt_ms) {}
};
```

Mirror writes (the `scenario.*` SharedState keys) are NOT observers — the
engine writes them every tick via a direct call. Only edge-triggered side
effects belong here.

## `NvsObserver` — built-in persistence observer

Source: `nvs_observer.h`. Implements `IEngineObserver`; persists 96-byte tokens
to NVS via caller-supplied callbacks. Throttle policy: immediate write on
`scenario_started`, `scenario_terminal`, and main-track `phase_entered`;
non-main-track `phase_entered` is throttled to 1 s minimum between writes.

```cpp
class NvsObserver : public IEngineObserver {
public:
    NvsObserver(NvsWriteFn write_fn, NvsReadFn read_fn, void* user);

    /// Must be called after engine construction, before engine.start().
    void bind_engine(const Engine& eng);

    // Observer overrides
    void on_scenario_started(SequenceHandle h) override;
    void on_phase_entered(SequenceHandle h, TrackIdx t, uint8_t phase_idx) override;
    void on_scenario_terminal(SequenceHandle h, SequenceRuntime::State final_state) override;
    void on_tick(uint32_t dt_ms) override;

    /// Engine forwards Engine::try_recover() into this.
    EngineError try_recover(SequenceHandle h, SequenceRuntime& sr);
};
```

## `ActionRegistry` — caller-owned

Source: `action_registry.h`. **Not a singleton.** Caller (typically `main.cpp`)
constructs an `ActionRegistry` instance, populates it via
`builtins::register_builtins(reg)` plus any custom registrations, and injects
the reference into the engine constructor. Multiple registries can coexist in
the same process.

```cpp
class ActionRegistry {
public:
    ActionRegistry() = default;
    ActionRegistry(const ActionRegistry&) = delete;
    ActionRegistry& operator=(const ActionRegistry&) = delete;
    ActionRegistry(ActionRegistry&&) = default;
    ActionRegistry& operator=(ActionRegistry&&) = default;

    bool register_action(const ActionDescriptor& d);
    bool register_condition(const ActionDescriptor& d);
                            ///< Returns false on collision, capacity exhaustion,
                            ///< or hash != djb2_hash16(name) inconsistency.

    const ActionDescriptor* find_action(uint16_t hash) const;
    const ActionDescriptor* find_condition(uint16_t hash) const;
                            ///< nullptr if not found.

    size_t action_count() const;
    size_t condition_count() const;

    void clear();           ///< test-isolation helper
};

constexpr uint16_t djb2_hash16(const char* str) noexcept;
```

Capacity: `MAX_REGISTRY_ENTRIES = 64` per registry (actions and conditions
separate). Thread safety: lookups (`find_*`) are read-only and lock-free after
initialization. Registrations must complete before the engine starts;
concurrent `register_*` calls are NOT safe.

See [usage/03_registering_actions.md](usage/03_registering_actions.md).

## `ContinuousRegistry` — caller-owned

Source: `continuous_behavior.h`. **Not a singleton** — same ownership and
threading model as `ActionRegistry`. Caller constructs, populates with factory
functions, and injects the reference into the engine constructor.

```cpp
class ContinuousBehavior {
public:
    virtual ~ContinuousBehavior() = default;
    virtual void on_activate(const ActionParam* params, uint8_t n,
                             const char* string_pool, ActionContext& ctx) = 0;
    virtual void on_tick(uint32_t dt_ms, ActionContext& ctx) = 0;
    virtual void on_deactivate(ActionContext& ctx) = 0;
    virtual size_t serialize(uint8_t* buf, size_t cap) const { return 0; }
    virtual bool deserialize(const uint8_t* buf, size_t len) { return true; }
    virtual uint16_t hash() const = 0;
    virtual const char* name() const = 0;
};

using ContinuousFactory = ContinuousBehavior* (*)();

class ContinuousRegistry {
public:
    ContinuousRegistry() = default;
    ContinuousRegistry(const ContinuousRegistry&) = delete;
    ContinuousRegistry& operator=(const ContinuousRegistry&) = delete;
    ContinuousRegistry(ContinuousRegistry&&) = default;
    ContinuousRegistry& operator=(ContinuousRegistry&&) = default;

    bool register_factory(uint16_t hash, const char* name, ContinuousFactory factory);
    ContinuousFactory   find(uint16_t hash) const;
    ContinuousBehavior* create(uint16_t hash) const;  ///< find() + factory() shortcut

    size_t count() const;
    void   clear();
};
```

Capacity: `MAX_CONTINUOUS_REGISTRATIONS = 32`.

### Standard primitives (Stage 2)

Per ADR-0006 the framework still ships **0 built-in continuous behaviours that
are auto-registered**. Stage 2 added an *opt-in* catalogue of standard control
primitives in `continuous_primitives.h`:

- `PidController` — closed-loop PID with anti-windup; reads `input_key`,
  writes `output_key`. Params: `input_key`, `output_key`, `setpoint`, `kp`,
  `ki`, `kd`, `out_min`, `out_max`.
- `HysteresisController` — bang-bang controller with symmetric deadband; cooling
  or heating mode. Params: `input_key`, `output_key`, `setpoint`, `deadband`,
  `mode` (0 = cooling, 1 = heating).
- `RampProfile` — linear ramp from `start_value` to `end_value` over
  `duration_ms`, then holds at `end_value`. Params: `output_key`,
  `start_value`, `end_value`, `duration_ms`.

To make them available to scenarios, call the registration helper on your
`ContinuousRegistry` instance:

```cpp
namespace modesp::scenario::primitives {
    ContinuousBehavior* pid_factory();
    ContinuousBehavior* hysteresis_factory();
    ContinuousBehavior* ramp_factory();

    /// Register all three primitives into the supplied registry.
    bool register_primitives(ContinuousRegistry& registry);

    constexpr int PRIMITIVE_COUNT = 3;
}
```

Factories return heap-allocated instances — the registry owner is responsible
for `delete` on unload/permanent-deactivate.

## Built-in actions and conditions

Source: `builtin_actions.h`. Registered via `builtins::register_builtins(reg)`
(takes the registry by reference — no singleton).

```cpp
namespace modesp::scenario::builtins {
    bool register_builtins(ActionRegistry& registry);
    constexpr int BUILTIN_ACTION_COUNT = 3;
    constexpr int BUILTIN_CONDITION_COUNT = 10;
}
```

3 actions:
- `log {msg: string}` — diagnostic ESP_LOG
- `set_state {key: str, type: i32-enum, value: typed}` — typed state write via
  `ctx.state->set(...)` (writes through the injected `IStateBackend`)
- `wait_ms {ms: i32}` — PENDING-based delay

10 leaf conditions:
- `time_elapsed_ms {ms: i32}`
- `state_key_eq/ne/lt/gt/le/ge {key, value}`
- `state_key_in_range {key, min, max}`
- `state_key_changed {key}` — placeholder, Step 14+ wires edges
- `time_of_day_eq {hh, mm}` — wall-clock match (requires SNTP)

Composite conditions (`all_of`, `any_of`, `not`) are handled inline by the
engine without registry entries — they use sentinel hashes in `cond_pool` entries.

## `LoadedScenario` — read-only view (advanced)

Source: `modr_loader.h`. Returned by `modr_validate` on success. Holds
raw pointers into the caller-owned buffer.

```cpp
struct LoadedScenario {
    const uint8_t* buffer;
    size_t         buffer_size;

    const modr_header*           header() const;
    const modr_track*            tracks() const;
    const modr_phase*            phases(uint8_t track_idx) const;
    const modr_transition*       transitions(uint16_t off) const;
    const modr_action*           action_pool() const;
    const modr_action*           cond_pool() const;
    const modr_param_entry*      param_pool() const;
    const modr_global_transition* global_transitions() const;
    const modr_resource_decl*    resources() const;
    const modr_phase_resource_claim* phase_resources(uint16_t off) const;

    const char* string_pool_data() const;
    uint16_t    string_pool_size() const;
    bool read_string(uint16_t offset, char* buf, size_t buf_size) const;
};

EngineError modr_validate(const uint8_t* buffer, size_t size,
                          const ActionRegistry& registry,
                          LoadedScenario& out);
uint32_t crc32_iso_hdlc(const uint8_t* data, size_t len);
```

`modr_validate` resolves action and condition hashes against the supplied
`ActionRegistry` reference — caller-owned, must outlive the call. Engine
clients typically don't touch `LoadedScenario` directly — use
`Engine::load_*` instead.

## `ResourceArbiter` — ISA-88 §5.3

Source: `resource_arbiter.h`. Concrete class (no interface abstraction).
Engine-internal but exposed via `engine.arbiter()` for diagnostic and test
access.

```cpp
class ResourceArbiter {
public:
    EngineError acquire_scenario(SequenceHandle h,
                                  const modr_resource_decl* resources,
                                  uint8_t count, uint8_t phase_idx = 0);
    void release_scenario(SequenceHandle h);

    bool try_acquire_phase(SequenceHandle h, TrackIdx track, uint8_t phase_idx,
                           const modr_phase_resource_claim* claims, uint8_t count);
    void release_phase(SequenceHandle h, TrackIdx track);

    bool is_owned(uint16_t resource_hash) const;
    const OwnerInfo* owner_of(uint16_t resource_hash) const;
    size_t count() const;

    void clear_for_tests();
};
```

See [06_resource_arbitration.md](06_resource_arbitration.md) for the design.

## Cross-references

- [usage/01_quickstart.md](usage/01_quickstart.md) — typical use pattern
- [usage/03_registering_actions.md](usage/03_registering_actions.md) — extending with custom actions
- [02_binary_format.md](02_binary_format.md) — `.modr` byte layout (input to loader)
- [07_persistence.md](07_persistence.md) — NVS callback contract and token format
- [10_error_model.md](10_error_model.md) — full EngineError table with handling guidance
