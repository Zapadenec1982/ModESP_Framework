# 07 — NVS Persistence

Power-cycle recovery via 96-byte tokens stored у ESP-IDF NVS. Engine
detects state changes per tick, throttles non-urgent writes, і invokes
а caller-provided callback. On boot, recipe re-loaded from filesystem;
caller invokes `try_recover()` which reads token AND restores phase
position before scenario re-enters PAUSED state.

## Storage layout

| NVS namespace | Key format | Value |
|---|---|---|
| `seqstate` | `t<slot>` (e.g. `t0`, `t1`, ..., `t<MAX_SEQUENCES-1>`) | 96-byte `seq_token` blob |

`MAX_SEQUENCES = 4` default → keys `t0`..`t3`. Per-instance keying allows
independent recovery of multiple concurrent scenarios.

## Token format (`seq_token`, 96 bytes)

Defined у `nvs_token.h`. Layout matches plan Q7. POD з no padding beyond
explicit fields:

```c
struct seq_token {                       // 96 bytes total
    uint32_t magic;                      // [0..3]   = 'SQTK' (0x4B545153 LE)
    uint16_t version;                    // [4..5]   = SEQ_TOKEN_VERSION (1)
    uint16_t scenario_id;                // [6..7]   djb2(module_name) low16
    uint8_t  scenario_state;             // [8]      SequenceRuntime::State
    uint8_t  track_count;                // [9]
    uint16_t resource_owner_mask;        // [10..11] reserved (Stage 1.5)
    uint32_t scenario_elapsed_ms;        // [12..15]
    uint32_t wall_clock_started_at;      // [16..19] unix epoch на start, 0 if no SNTP
    struct {                             // [20..67] 6 tracks × 8 bytes
        uint8_t  state;                  //          TrackRuntime::State
        uint8_t  phase_idx;
        uint16_t reserved;
        uint32_t phase_elapsed_ms;
    } tracks[6];
    uint8_t  cont_state[16];             // [68..83] reserved для ContinuousBehaviors (Stage 2)
    uint32_t reserved_a;                 // [84..87]
    uint32_t reserved_b;                 // [88..91]
    uint16_t crc16;                      // [92..93] CRC-CCITT of [0..91]
    uint16_t reserved_c;                 // [94..95]
};
```

CRC-CCITT (XMODEM variant: poly 0x1021, init 0x0000, no reflection).
Computed over bytes [0..91] inclusive; trailer at [92..93].

## Write policy (per plan Q7)

`SequenceEngine::persist_scan()` applies the policy each tick після
`instance_tick`:

| Event | Persist timing | Rationale |
|-------|---------------|-----------|
| Scenario state change (LOADED→RUNNING, abort, complete, fail) | **Immediate** | Crash-critical event must survive |
| Main-track phase advance (track із `MODR_TRACK_FLAG_MAIN`) | **Immediate** | Main track invariants preserved |
| Side-track phase advance | Throttled to ≥1s between writes | Flash wear protection |
| 5-minute checkpoint (Stage 1.5) | Periodic | Ensures resume точність якщо only side-track activity |

Per-slot tracking fields у `Slot`:
- `last_persisted_state` — what was last written
- `last_persisted_phase_idx[6]` — per-track phase indices
- `time_since_persist_ms` — saturating throttle counter

Change detection compares current runtime to last_persisted_*; persist
only fires when actual change exists.

### Wear budget calculation

ESP-IDF NVS = wear-leveled flash. Worst-case write rate:
- 6-track distillation recipe з phase changes every ~30 sec
- Side tracks throttle to 1 write/sec → ≤ 5 writes/sec aggregate
- 5 × 60 × 60 × 24 = 432K writes/day theoretical max

Realistic recipes are much sparser:
- 8-hour cooking recipe з ~50 phase changes total → ~50 writes per run
- 5 runs/day → 250 writes/day per slot

NVS rated 100K cycles/sector. Аt 250 writes/day → 400 days continuous
operation per sector. Wear-leveling distributes across multiple sectors
у the namespace. Realistic field life: many years.

If write rate becomes а concern, increase throttle interval або add
explicit checkpoint logic у recipe (e.g. only persist on major phase
boundaries).

## Callback contract

Engine doesn't directly call `nvs_set_blob` — instead invokes а callback
provided by the caller. Це keeps engine code target-agnostic (host tests
provide in-memory mocks).

```cpp
using NvsWriteFn = bool (*)(void* user, uint8_t slot,
                            const uint8_t* token, size_t len);
using NvsReadFn  = bool (*)(void* user, uint8_t slot,
                            uint8_t* token_buf, size_t* in_out_len);

engine.set_nvs_callbacks(write_fn, read_fn, user_ctx);
```

Reference target wiring у `main.cpp`:

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

Callbacks invoked from engine update task only — caller doesn't need
synchronization beyond what NVS itself provides.

### Callback failure handling

`write_fn` returning `false`:
- Engine logs warning (Stage 1.5 — currently silent fallback)
- Slot's `last_persisted_*` NOT updated → next tick will retry
- Каскад persistent failure can starve other operations; recipes що
  depend on persistence для safety should monitor backend health

`read_fn` returning `false`:
- `try_recover()` returns `EngineError::NVS_ERROR`
- Caller decides whether to abort scenario або start fresh

## Recovery flow

On firmware boot після reset:

```cpp
// Step 1: Re-load recipe (recipe binary lives у LittleFS, persists
// across reboots independent of NVS)
auto handle = engine.load_path("/data/scenarios/abs_test.modr");
if (handle == 0) { /* recipe missing */ return; }

// Step 2: Attempt recovery
EngineError err = engine.try_recover(handle);
if (err == EngineError::OK) {
    // Slot тепер у PAUSED state із phase_idx + phase_elapsed_ms restored.
    // Scenario не auto-resumes. User decides via WebUI:
    //   - resume(handle) → continues from where it stopped
    //   - abort(handle)  → forces FAILED, releases all resources
    //   - unload(handle) → discards без abort sequence
} else if (err == EngineError::NVS_ERROR) {
    // No persisted token (fresh install або never persisted) — slot stays
    // у LOADED. start() будe restart scenario from beginning.
} else {
    // Token corrupt (CRC), wrong scenario_id (recipe changed), або
    // out-of-range fields. Erase NVS slot AND treat як no-data:
    erase_nvs_slot(handle);
    // engine state unchanged — slot still LOADED
}
```

### Recovery validation chain

`deserialize_token` performs у order:
1. Magic check (`SEQ_TOKEN_MAGIC` == 'SQTK')
2. Version check (`version` == `SEQ_TOKEN_VERSION`)
3. CRC16 verification over [0..91]
4. `scenario_id` matches loaded recipe header (rejects token from а
   previous recipe із same slot)
5. `track_count` matches recipe track count (rejects schema changes)
6. Per-track `phase_idx` < recipe's `phase_count` (rejects schema changes
   що might cause OOB read)

Any check failure returns specific `EngineError` (INVALID_FILE,
UNSUPPORTED_VERSION, CRC_MISMATCH).

### Recovery observability

(Stage 1.5 enhancement — not yet implemented):
- Engine writes `scenario.engine_recovery_pending = true` коли recovery
  succeeded but scenario awaits user action
- WebUI uses `visible_when: {scenario.engine_recovery_pending: [true]}`
  to show banner з resume/abort/unload buttons
- Log ESP_LOGW message: "Scenario %s recovered у state %s; may control
  resources [...]. Resume або abort to restore module state."

## Schema migration

Token version (`SEQ_TOKEN_VERSION`) bumped only on incompatible changes.
Backward/forward compatibility rules:

1. **Add fields ONLY at end of struct.** Reserved slots (`reserved_a`,
   `reserved_b`, `cont_state`, etc.) are designated growth points.
2. **NEVER remove fields.** Mark deprecated; old tokens still parse.
3. **NEVER reuse field positions.** New semantic = new field at end.
4. **Major bump (version → 2):** triggers `UNSUPPORTED_VERSION` error;
   caller treats as no-data, starts fresh.

Tokens are derived state (recipes are authoritative source у LittleFS).
Migration "tools" не required — на firmware update із token format
bump, accept loss of in-progress scenarios. Document як known constraint.

## See also

- [02_binary_format.md](02_binary_format.md) — `.modr` (recipe) byte layout
- [03_api_reference.md](03_api_reference.md#persistence-stage-1) — public callback API
- [10_error_model.md](10_error_model.md) — recovery error codes
- [adr/0001-binary-format-not-constexpr.md](adr/0001-binary-format-not-constexpr.md) — token format design
- Source: `components/modesp_scenario/src/nvs_token.cpp`,
  `engine.cpp::persist_scan` і `try_recover`
