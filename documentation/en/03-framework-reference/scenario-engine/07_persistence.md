# 07 — NVS Persistence

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/07_persistence.md](../../../uk/03-framework-reference/scenario-engine/07_persistence.md)

Power-cycle recovery via 96-byte tokens stored in ESP-IDF NVS. The
engine emits scenario lifecycle edge events through `IEngineObserver`
hooks; `NvsObserver` listens to those edges, applies the throttle
policy in its own per-slot state, and invokes a caller-provided
callback to write the token. On boot, the recipe is re-loaded from the
filesystem; the caller invokes `engine.try_recover(handle, nvs_observer)`
which reads the token AND restores phase position before the scenario
re-enters PAUSED state.

## Storage layout

| NVS namespace | Key format | Value |
|---|---|---|
| `scnstate` | `t<slot>` (e.g. `t0`, `t1`, ..., `t<MAX_SEQUENCES-1>`) | 96-byte `seq_token` blob |

`MAX_SEQUENCES = 2` by default (Kconfig `CONFIG_MODESP_MAX_SEQUENCES`,
max 8) → keys `t0`..`t1`. Per-instance keying allows independent
recovery of multiple concurrent scenarios.

## Token format (`seq_token`, 96 bytes)

Defined in `nvs_token.h`. The layout matches plan Q7. POD with no
padding beyond explicit fields:

```c
struct seq_token {                       // 96 bytes total
    uint32_t magic;                      // [0..3]   = 'SCTK' (0x4B544353 LE)
    uint16_t version;                    // [4..5]   = SEQ_TOKEN_VERSION (1)
    uint16_t scenario_id;                // [6..7]   djb2(module_name) low16
    uint8_t  scenario_state;             // [8]      SequenceRuntime::State
    uint8_t  track_count;                // [9]
    uint16_t resource_owner_mask;        // [10..11] reserved (Stage 1.5)
    uint32_t scenario_elapsed_ms;        // [12..15]
    uint32_t wall_clock_started_at;      // [16..19] unix epoch at start, 0 if no SNTP
    struct {                             // [20..67] 6 tracks × 8 bytes
        uint8_t  state;                  //          TrackRuntime::State
        uint8_t  phase_idx;
        uint16_t reserved;
        uint32_t phase_elapsed_ms;
    } tracks[6];
    uint8_t  cont_state[16];             // [68..83] reserved for ContinuousBehaviors (Stage 2)
    uint32_t reserved_a;                 // [84..87]
    uint32_t reserved_b;                 // [88..91]
    uint16_t crc16;                      // [92..93] CRC-CCITT of [0..91]
    uint16_t reserved_c;                 // [94..95]
};
```

CRC-CCITT (XMODEM variant: poly 0x1021, init 0x0000, no reflection).
Computed over bytes [0..91] inclusive; trailer at [92..93].

## Write policy (per plan Q7)

The engine no longer scans for state changes inside `on_update`.
Instead it emits edge events through `IEngineObserver` hooks
(`on_scenario_started`, `on_phase_entered`, `on_scenario_terminal`)
synchronously from the tick path. `NvsObserver` implements those
hooks and applies this policy:

| Event | Persist timing | Rationale |
|-------|---------------|-----------|
| `on_scenario_started` (LOADED→RUNNING) | **Immediate** | Crash-critical event must survive |
| `on_scenario_terminal` (COMPLETED, FAILED, including ABORTING→FAILED) | **Immediate** | Final state must be recorded |
| `on_phase_entered` for main track (`MODR_TRACK_FLAG_MAIN`) | **Immediate** | Main track invariants preserved |
| `on_phase_entered` for side track | Throttled to ≥1 s between writes | Flash wear protection |
| 5-minute checkpoint (Stage 1.5) | Periodic | Ensures resume accuracy when only side-track activity |

Per-slot throttle state lives **in `NvsObserver`**, not in the engine
`Slot`. The observer keeps a saturating `time_since_persist_ms_[]`
counter per slot index (advanced from `on_tick`) and uses it inside
`throttle_check()` to gate non-urgent writes. The engine does not
track "last persisted" data — it just emits edges; the observer
decides what to persist when.

### Wear budget calculation

ESP-IDF NVS = wear-leveled flash. Worst-case write rate:
- 6-track distillation recipe with phase changes every ~30 sec
- Side tracks throttled to 1 write/sec → ≤ 5 writes/sec aggregate
- 5 × 60 × 60 × 24 = 432K writes/day theoretical max

Realistic recipes are much sparser:
- 8-hour cooking recipe with ~50 phase changes total → ~50 writes per run
- 5 runs/day → 250 writes/day per slot

NVS is rated for 100K cycles/sector. At 250 writes/day → 400 days of
continuous operation per sector. Wear-leveling distributes across
multiple sectors in the namespace. Realistic field life: many years.

If write rate becomes a concern, increase the throttle interval or add
explicit checkpoint logic in the recipe (e.g. only persist on major
phase boundaries).

## Callback contract

`NvsObserver` does not call `nvs_set_blob` directly — instead it
invokes callbacks passed to its **constructor**. This keeps observer
code target-agnostic (host tests provide in-memory mocks). The engine
itself has no NVS hooks: it accepts the observer through its
`etl::span<IEngineObserver*>` constructor parameter and emits edge
events; the observer owns the callbacks.

```cpp
using NvsWriteFn = bool (*)(void* user, uint8_t slot,
                            const uint8_t* token, size_t len);
using NvsReadFn  = bool (*)(void* user, uint8_t slot,
                            uint8_t* token_buf, size_t* in_out_len);

// Constructed with callbacks; injected into engine via observer span.
NvsObserver nvs_obs{write_fn, read_fn, user_ctx};
IEngineObserver* obs_list[] = {&nvs_obs};
Engine engine{state_backend, actions, continuous, obs_list};
nvs_obs.bind_engine(engine);  // required before engine.start()
```

Reference target wiring in `main.cpp`:

```cpp
static auto seq_nvs_write = [](void*, uint8_t slot,
                                const uint8_t* token, size_t len) -> bool {
    char key[8];
    std::snprintf(key, sizeof(key), "t%u", static_cast<unsigned>(slot));
    return modesp::nvs_helper::write_blob("scnstate", key, token, len);
};

static auto seq_nvs_read = [](void*, uint8_t slot,
                               uint8_t* buf, size_t* in_out_len) -> bool {
    char key[8];
    std::snprintf(key, sizeof(key), "t%u", static_cast<unsigned>(slot));
    size_t out_len = 0;
    bool ok = modesp::nvs_helper::read_blob("scnstate", key, buf,
                                             *in_out_len, out_len);
    if (ok) *in_out_len = out_len;
    return ok;
};

static modesp::scenario::NvsObserver nvs_obs{seq_nvs_write, seq_nvs_read, nullptr};
```

Callbacks are invoked from the engine update task only (synchronously
inside an observer event) — the caller does not need synchronization
beyond what NVS itself provides.

### Callback failure handling

`write_fn` returning `false`:
- The observer logs a warning (Stage 1.5 — currently silent fallback).
- The observer's per-slot throttle counter is NOT reset → the next
  edge event will retry.
- Cascading persistent failures can starve other operations; recipes
  that depend on persistence for safety should monitor backend health.

`read_fn` returning `false`:
- `engine.try_recover(h, nvs_obs)` returns `EngineError::NVS_ERROR`.
- The caller decides whether to abort the scenario or start fresh.

## Recovery flow

On firmware boot after reset:

```cpp
// Step 1: Re-load recipe (recipe binary lives in LittleFS, persists
// across reboots independent of NVS)
auto handle = engine.load_path("/data/scenarios/abs_test.modr");
if (handle == 0) { /* recipe missing */ return; }

// Step 2: Attempt recovery (observer passed explicitly — engine doesn't
// ID-cast observers; recovery is a round-trip read that doesn't fit
// the fire-and-forget event model)
EngineError err = engine.try_recover(handle, nvs_obs);
if (err == EngineError::OK) {
    // Slot is now in PAUSED state with phase_idx + phase_elapsed_ms restored.
    // Scenario does not auto-resume. User decides via WebUI:
    //   - resume(handle) → continues from where it stopped
    //   - abort(handle)  → forces FAILED, releases all resources
    //   - unload(handle) → discards without abort sequence
} else if (err == EngineError::NVS_ERROR) {
    // No persisted token (fresh install or never persisted) — slot stays
    // in LOADED. start() will restart the scenario from the beginning.
} else {
    // Token corrupt (CRC), wrong scenario_id (recipe changed), or
    // out-of-range fields. Erase NVS slot AND treat as no-data:
    erase_nvs_slot(handle);
    // engine state unchanged — slot still LOADED
}
```

### Recovery validation chain

`deserialize_token` performs the following in order:
1. Magic check (`SEQ_TOKEN_MAGIC` == 'SCTK' / `0x4B544353` LE; old
   `'SQTK'` tokens from `modesp_sequence` are rejected)
2. Version check (`version` == `SEQ_TOKEN_VERSION`)
3. CRC16 verification over [0..91]
4. `scenario_id` matches the loaded recipe header (rejects a token from
   a previous recipe in the same slot).
5. `track_count` matches the recipe track count (rejects schema changes).
6. Per-track `phase_idx` < recipe's `phase_count` (rejects schema changes
   that might cause OOB read).

Any check failure returns a specific `EngineError` (INVALID_FILE,
UNSUPPORTED_VERSION, CRC_MISMATCH).

### Recovery observability

(Stage 1.5 enhancement — not yet implemented):
- The engine writes `scenario.engine_recovery_pending = true` when
  recovery succeeded but the scenario awaits user action.
- The WebUI uses `visible_when: {scenario.engine_recovery_pending: [true]}`
  to show a banner with resume/abort/unload buttons.
- Logs an ESP_LOGW message: "Scenario %s recovered in state %s; may
  control resources [...]. Resume or abort to restore module state."

## Schema migration

The token version (`SEQ_TOKEN_VERSION`) is bumped only on incompatible
changes. Backward/forward compatibility rules:

1. **Add fields ONLY at end of struct.** Reserved slots (`reserved_a`,
   `reserved_b`, `cont_state`, etc.) are designated growth points.
2. **NEVER remove fields.** Mark deprecated; old tokens still parse.
3. **NEVER reuse field positions.** New semantic = new field at end.
4. **Major bump (version → 2):** triggers `UNSUPPORTED_VERSION` error;
   caller treats as no-data, starts fresh.

Tokens are derived state (recipes are the authoritative source in
LittleFS). Migration "tools" are not required — on a firmware update
with a token format bump, accept loss of in-progress scenarios.
Document as a known constraint.

## See also

- [02_binary_format.md](02_binary_format.md) — `.modr` (recipe) byte layout
- [03_api_reference.md](03_api_reference.md#persistence-stage-1) — public callback API
- [10_error_model.md](10_error_model.md) — recovery error codes
- [adr/0001-binary-format-not-constexpr.md](adr/0001-binary-format-not-constexpr.md) — token format design
- Source: `components/modesp_scenario/src/nvs_token.cpp`,
  `nvs_observer.cpp` (throttle policy + persist_slot),
  `engine.cpp::try_recover`
