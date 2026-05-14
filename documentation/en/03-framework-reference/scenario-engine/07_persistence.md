# 07 — NVS Persistence

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/07_persistence.md](../../../uk/03-framework-reference/scenario-engine/07_persistence.md)

Power-cycle recovery via 96-byte tokens stored in ESP-IDF NVS. The
engine detects state changes per tick, throttles non-urgent writes, and
invokes a caller-provided callback. On boot, the recipe is re-loaded
from the filesystem; the caller invokes `try_recover()` which reads the
token AND restores phase position before the scenario re-enters PAUSED
state.

## Storage layout

| NVS namespace | Key format | Value |
|---|---|---|
| `seqstate` | `t<slot>` (e.g. `t0`, `t1`, ..., `t<MAX_SEQUENCES-1>`) | 96-byte `seq_token` blob |

`MAX_SEQUENCES = 4` by default → keys `t0`..`t3`. Per-instance keying
allows independent recovery of multiple concurrent scenarios.

## Token format (`seq_token`, 96 bytes)

Defined in `nvs_token.h`. The layout matches plan Q7. POD with no
padding beyond explicit fields:

```c
struct seq_token {                       // 96 bytes total
    uint32_t magic;                      // [0..3]   = 'SQTK' (0x4B545153 LE)
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

`SequenceEngine::persist_scan()` applies the policy each tick after
`instance_tick`:

| Event | Persist timing | Rationale |
|-------|---------------|-----------|
| Scenario state change (LOADED→RUNNING, abort, complete, fail) | **Immediate** | Crash-critical event must survive |
| Main-track phase advance (track with `MODR_TRACK_FLAG_MAIN`) | **Immediate** | Main track invariants preserved |
| Side-track phase advance | Throttled to ≥1s between writes | Flash wear protection |
| 5-minute checkpoint (Stage 1.5) | Periodic | Ensures resume accuracy when only side-track activity |

Per-slot tracking fields in `Slot`:
- `last_persisted_state` — what was last written
- `last_persisted_phase_idx[6]` — per-track phase indices
- `time_since_persist_ms` — saturating throttle counter

Change detection compares current runtime to `last_persisted_*`; a
persist only fires when an actual change exists.

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

The engine does not directly call `nvs_set_blob` — instead it invokes a
callback provided by the caller. This keeps engine code
target-agnostic (host tests provide in-memory mocks).

```cpp
using NvsWriteFn = bool (*)(void* user, uint8_t slot,
                            const uint8_t* token, size_t len);
using NvsReadFn  = bool (*)(void* user, uint8_t slot,
                            uint8_t* token_buf, size_t* in_out_len);

engine.set_nvs_callbacks(write_fn, read_fn, user_ctx);
```

Reference target wiring in `main.cpp`:

```cpp
static auto seq_nvs_write = [](void*, uint8_t slot,
                                const uint8_t* token, size_t len) -> bool {
    char key[8];
    std::snprintf(key, sizeof(key), "t%u", static_cast<unsigned>(slot));
    return modesp::nvs_helper::write_blob("seqstate", key, token, len);
};

static auto seq_nvs_read = [](void*, uint8_t slot,
                               uint8_t* buf, size_t* in_out_len) -> bool {
    char key[8];
    std::snprintf(key, sizeof(key), "t%u", static_cast<unsigned>(slot));
    size_t out_len = 0;
    bool ok = modesp::nvs_helper::read_blob("seqstate", key, buf,
                                             *in_out_len, out_len);
    if (ok) *in_out_len = out_len;
    return ok;
};

sequence_engine.set_nvs_callbacks(seq_nvs_write, seq_nvs_read, nullptr);
```

Callbacks are invoked from the engine update task only — the caller
does not need synchronization beyond what NVS itself provides.

### Callback failure handling

`write_fn` returning `false`:
- The engine logs a warning (Stage 1.5 — currently silent fallback).
- The slot's `last_persisted_*` is NOT updated → the next tick will retry.
- Cascading persistent failures can starve other operations; recipes
  that depend on persistence for safety should monitor backend health.

`read_fn` returning `false`:
- `try_recover()` returns `EngineError::NVS_ERROR`.
- The caller decides whether to abort the scenario or start fresh.

## Recovery flow

On firmware boot after reset:

```cpp
// Step 1: Re-load recipe (recipe binary lives in LittleFS, persists
// across reboots independent of NVS)
auto handle = engine.load_path("/data/scenarios/abs_test.modr");
if (handle == 0) { /* recipe missing */ return; }

// Step 2: Attempt recovery
EngineError err = engine.try_recover(handle);
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
1. Magic check (`SEQ_TOKEN_MAGIC` == 'SQTK')
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
  `engine.cpp::persist_scan` and `try_recover`
