# 03 — C++ API Reference

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/03_api_reference.md](../../../uk/03-framework-reference/scenario-engine/03_api_reference.md)

Authoritative reference for the public C++ API surface of the `modesp::scenario`
namespace. Source: `components/modesp_scenario/include/modesp/scenario/`.

## Constants

```cpp
namespace modesp::scenario {

// Slot pool — configurable via Kconfig MODESP_MAX_SEQUENCES
constexpr size_t   MAX_SEQUENCES = 4;            // default; range 2..8

// Track count caps
constexpr uint8_t  MAX_TRACKS_PER_SCENARIO = 6;
// MAX_TOTAL_TRACKS = MAX_SEQUENCES × MAX_TRACKS_PER_SCENARIO = 24 (default)

// Composite condition tree depth
constexpr uint8_t  MAX_CONDITION_DEPTH = 16;

// Resource ownership map size
constexpr size_t   MAX_RESOURCES = 32;

// Sentinel for non-real track index (= scenario-scope ownership)
constexpr TrackIdx TRACK_IDX_SCENARIO = 0xFF;

// Composite condition sentinel hashes
constexpr uint16_t MODR_COND_HASH_ALL_OF;        // djb2_hash16("all_of")
constexpr uint16_t MODR_COND_HASH_ANY_OF;        // djb2_hash16("any_of")
constexpr uint16_t MODR_COND_HASH_NOT;           // djb2_hash16("not")

// File format
constexpr uint32_t MODR_MAGIC = 0x52444F4D;      // 'MODR' LE
constexpr uint16_t MODR_FORMAT_VERSION = 1;
constexpr size_t   MODR_MAX_SIZE = 16 * 1024;    // 16 KB

// Persistence token
constexpr uint32_t SEQ_TOKEN_MAGIC = 0x4B545153; // 'SQTK' LE
constexpr uint16_t SEQ_TOKEN_VERSION = 1;
constexpr size_t   SEQ_TOKEN_SIZE = 96;

}
```

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
    modesp::SharedState* state;
    const ActionParam*   params;
    uint8_t              param_count;
    const char*          string_pool;
    uint16_t             string_pool_size;
    uint32_t             scenario_elapsed_ms;
    uint32_t             phase_elapsed_ms;
    uint8_t              phase_idx;
    SequenceHandle       handle;
    TrackIdx             track;
    const char*          recipe_name;       ///< from recipe header (string pool)
    const char*          track_name;        ///< from track entry (string pool)
};
```

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

## `SequenceEngine` — public API

`class Engine : public modesp::BaseModule`. Default constructor:
`SequenceEngine(SharedState* state = nullptr)`.

### Construction and wiring

```cpp
// Static instance
static modesp::scenario::SequenceEngine engine;

// In main.cpp post-construction:
engine.set_state(&app.state());

// Optional: NVS persistence callbacks (see [07_persistence.md](07_persistence.md))
engine.set_nvs_callbacks(&write_fn, &read_fn, user_ctx);

// Register builtins ONCE before any module init
modesp::scenario::builtins::register_builtins();

// Register engine with ModuleManager
app.modules().register_module(engine);
```

### BaseModule interface (managed by ModuleManager)

```cpp
bool on_init() override;            // resets all slots, clears arbiter
void on_update(uint32_t dt_ms) override;
                                    // ticks running instances, publishes mirror
                                    // keys, runs persist scan
void on_stop() override;            // unloads all slots
```

### Recipe loading

```cpp
SequenceHandle load_buffer(const uint8_t* data, size_t size);
SequenceHandle load_path(const char* path);
EngineError unload(SequenceHandle h);
```

`load_buffer` copies bytes into engine-owned slot buffer (max MODR_MAX_SIZE).
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

```cpp
using NvsWriteFn = bool (*)(void* user, uint8_t slot,
                            const uint8_t* token, size_t len);
using NvsReadFn  = bool (*)(void* user, uint8_t slot,
                            uint8_t* buf, size_t* in_out_len);

void set_nvs_callbacks(NvsWriteFn write, NvsReadFn read, void* user);
EngineError try_recover(SequenceHandle h);
```

`try_recover` requires the slot to already be in LOADED state (recipe re-loaded
after boot). It calls the read callback, deserializes the token, applies the
state, and sets the scenario state to PAUSED. See [07_persistence.md](07_persistence.md).

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

ResourceArbiter& arbiter();             ///< direct arbiter access (tests)
```

All diagnostics return safe defaults (IDLE state, 0 counts) for invalid
handles — they never trap.

## `ActionRegistry` — singleton

Source: `action_registry.h`. Used by domain modules to register custom
actions and conditions.

```cpp
class ActionRegistry {
public:
    static ActionRegistry& instance();

    bool register_action(const ActionDescriptor& d);
    bool register_condition(const ActionDescriptor& d);
                            ///< Returns false on collision, capacity exhaustion,
                            ///< or hash != djb2_hash16(name) inconsistency.

    const ActionDescriptor* find_action(uint16_t hash) const;
    const ActionDescriptor* find_condition(uint16_t hash) const;
                            ///< nullptr if not found.

    size_t action_count() const;
    size_t condition_count() const;

    void clear_for_tests();  ///< test-only — production never calls
};

constexpr uint16_t djb2_hash16(const char* str) noexcept;
```

Capacity: `MAX_REGISTRY_ENTRIES = 64` per registry (actions and conditions
separate). See [usage/03_registering_actions.md](usage/03_registering_actions.md).

## `ContinuousRegistry` — singleton (Stage 1.5)

Source: `continuous_behavior.h`. Reserved for future ContinuousBehavior
implementations (PID, hysteresis, ramp). 0 built-ins in Stage 1.

```cpp
class ContinuousBehavior {
public:
    virtual void on_activate(const ActionParam* params, uint8_t n,
                             const char* string_pool, ActionContext& ctx) = 0;
    virtual void on_tick(uint32_t dt_ms, ActionContext& ctx) = 0;
    virtual void on_deactivate(ActionContext& ctx) = 0;
    virtual size_t serialize(uint8_t* buf, size_t cap) const { return 0; }
    virtual bool deserialize(const uint8_t* buf, size_t len) { return true; }
    virtual uint16_t hash() const = 0;
    virtual const char* name() const = 0;
};

class ContinuousRegistry {
public:
    using FactoryFn = ContinuousBehavior* (*)();
    static ContinuousRegistry& instance();

    bool register_factory(uint16_t hash, const char* name, FactoryFn);
    ContinuousBehavior* create(uint16_t hash);
};
```

## Built-in actions and conditions

Source: `builtin_actions.h`. Registered via `builtins::register_builtins()`.

3 actions:
- `log {msg: string}` — diagnostic ESP_LOG
- `set_state {key: str, type: i32-enum, value: typed}` — typed SharedState write
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
                          LoadedScenario& out);
uint32_t crc32_iso_hdlc(const uint8_t* data, size_t len);
```

Engine clients typically don't touch `LoadedScenario` directly — use
`SequenceEngine::load_*` instead.

## `ResourceArbiter` — ISA-88 §5.3

Source: `resource_arbiter.h`. Engine-internal but exposed via `engine.arbiter()`
for diagnostic and test access.

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
