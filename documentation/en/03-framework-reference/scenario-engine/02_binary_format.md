# 02 — `.modr` Binary Format (v1)

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/02_binary_format.md](../../../uk/03-framework-reference/scenario-engine/02_binary_format.md)

**Status:** Complete (Step 1).
**Source of truth:** [`components/modesp_scenario/include/modesp/scenario/modr_format.h`](../../components/modesp_scenario/include/modesp/scenario/modr_format.h)
**Test invariants:** [`tools/tests/test_modr_format.py`](../../tools/tests/test_modr_format.py)
**Golden binary:** [`tools/tests/fixtures/scenarios/minimal_v1.modr`](../../tools/tests/fixtures/scenarios/minimal_v1.modr) (114 bytes)

## Overview

`.modr` is a single-file binary recipe loaded by SequenceEngine at runtime from LittleFS. Little-endian, 4-byte natural alignment (no `__attribute__((packed))` needed). The whole file fits in a fixed buffer `MODR_MAX_SIZE = 16 KB`.

Load pipeline: `f_read` the entire file → validate magic/version/CRC → parse offsets → execute. Zero-parse runtime — the engine reads structs directly from the buffer.

## Constants

| Symbol | Value | Description |
|--------|-------|-------------|
| `MODR_MAGIC` | `0x52444F4D` | 'MODR' as LE uint32 |
| `MODR_FORMAT_VERSION` | `1` | Bump on breaking schema changes |
| `MODR_MAX_SIZE` | `16384` (16 KB) | Buffer cap per loaded scenario |
| `MODR_NO_OFFSET` | `0xFFFF` | "no entry" sentinel (entry/exit_action_off, etc.) |
| `MODR_TARGET_COMPLETE` | `0xFFFF` | Transition target = $complete (this track) |
| `MODR_TARGET_ABORT` | `0xFFFE` | Transition target = $abort (whole scenario) |

## File layout

```
┌────────────────────────────────────┐  offset 0
│  modr_header (56 bytes)            │
├────────────────────────────────────┤
│  modr_track[track_count] (16 each) │  ← header.track_table_off
├────────────────────────────────────┤
│  modr_phase[N] (20 each)           │  ← track[i].phases_off (per-track phase array)
│  ...                               │
├────────────────────────────────────┤
│  modr_transition[] (12 each)       │  ← phase[j].transitions_off (inline transitions)
│  ...                               │
├────────────────────────────────────┤
│  modr_action[] action pool (8)     │  ← header.action_pool_off
├────────────────────────────────────┤
│  modr_action[] cond pool (8)       │  ← header.cond_pool_off
├────────────────────────────────────┤
│  modr_param_entry[] (8)            │  ← header.param_pool_off
├────────────────────────────────────┤
│  modr_global_transition[] (8)      │  ← header.global_trans_off
├────────────────────────────────────┤
│  modr_resource_decl[] (4)          │  ← header.resource_off
├────────────────────────────────────┤
│  modr_phase_resource_claim[] (4)   │  ← phase[j].phase_resources_off
│  ...                               │
├────────────────────────────────────┤
│  String pool (length-prefixed)     │  ← header.string_pool_off
│    [u8 len][bytes][u8 len][bytes]  │
├────────────────────────────────────┤
│  CRC32 trailer (4 bytes)           │  ← total_size - 4
└────────────────────────────────────┘  total_size

CRC = CRC-32/ISO-HDLC computed over [0 .. total_size-4]
       (matches Python zlib.crc32 and ESP-IDF esp_crc32_le)
```

## Struct sizes (validated by static_assert + pytest)

| Struct | Size | Notes |
|--------|------|-------|
| `modr_header` | 56 | Includes default_phase_timeout_ms and scenario_timeout_max_ms |
| `modr_track` | 16 | Per track entry |
| `modr_phase` | 20 | +4 from the plan Q1 spec for phase_resources fields (Step 0.75) |
| `modr_transition` | 12 | **Corrected** from plan Q1's spec of 8 (alignment fix) |
| `modr_global_transition` | 8 | Applied to all tracks each tick |
| `modr_action` | 8 | **Corrected** from plan Q1's spec of 6 (padded for alignment) |
| `modr_param_entry` | 8 | |
| `modr_resource_decl` | 4 | Scenario-scope resource |
| `modr_phase_resource_claim` | 4 | Phase-scope claim (Step 0.75) |

## Layout corrections from plan Q1

Plan Q1's spec'd byte counts had two arithmetic errors caught during Step 1 implementation:

1. **`modr_transition`**: spec'd 8 bytes, but the fields total 2+2+1+1+4 = 10 bytes; uint32 must be 4-aligned → padded to 12.
2. **`modr_action`**: spec'd 6 bytes, but not 4-byte aligned → padded to 8 (added `uint16_t reserved`).

`modr_phase` was additionally extended to 20 bytes (from the spec'd 16) for the `phase_resources_off + phase_resource_n + reserved` fields after the Step 0.75 paper pilot identified the need for phase-scope resource arbitration.

These corrections do NOT break any existing functionality — Stage 0 has no compiled recipes yet. Plan Q1 will be updated to match the implementation.

## Header fields (offset → field)

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0 | 4 | `magic` | `0x52444F4D` |
| 4 | 2 | `format_version` | `1` |
| 6 | 2 | `flags` | bitfield: `MODR_FLAG_*` |
| 8 | 4 | `total_size` | full file size incl. CRC |
| 12 | 2 | `scenario_id` | djb2(module_name) low16 |
| 14 | 2 | `name_str_idx` | recipe name string pool offset |
| 16 | 1 | `track_count` | 1..6 |
| 17 | 1 | `cont_count` | ContinuousBehavior slot count (0 in MVP) |
| 18 | 1 | `resource_count` | scenario-scope resources |
| 19 | 1 | `global_trans_count` | global transitions |
| 20 | 1 | `completion_rule` | `MODR_COMPLETION_*` |
| 21 | 1 | `reserved_a` | |
| 22 | 2 | `track_table_off` | |
| 24 | 2 | `param_pool_off` | |
| 26 | 2 | `param_pool_count` | |
| 28 | 2 | `action_pool_off` | |
| 30 | 2 | `action_pool_count` | |
| 32 | 2 | `cond_pool_off` | |
| 34 | 2 | `cond_pool_count` | |
| 36 | 2 | `string_pool_off` | |
| 38 | 2 | `global_trans_off` | |
| 40 | 2 | `resource_off` | |
| 42 | 2 | `reserved_b` | padding for uint32 alignment |
| 44 | 4 | `default_phase_timeout_ms` | applied to phases without an explicit timeout |
| 48 | 4 | `scenario_timeout_max_ms` | hard cap; 0 = no limit |
| 52 | 4 | `reserved_c` | |

## Validation rules (loader, Step 8)

To be enforced when `modr_loader.cpp` lands. Documented here for visibility:

1. `magic == MODR_MAGIC` → else `INVALID_FILE`
2. `format_version == 1` → else `UNSUPPORTED_VERSION`
3. `total_size <= MODR_MAX_SIZE` AND `total_size <= file_size` → else `INVALID_FILE`
4. CRC32 over `[0..total_size-4]` matches trailer → else `CRC_MISMATCH`
5. `track_count >= 1` AND `<= MAX_TRACKS_PER_SCENARIO`
6. Each track `phase_count >= 1`
7. Each phase: all action pool indices < `action_pool_count`
8. Each transition: `target_phase < phase_count` OR sentinel
9. `global_trans_off + global_trans_count * sizeof(modr_global_transition) <= total_size - 4`
10. All action `action_hash` values resolve in `ActionRegistry::find_action`
11. All condition `action_hash` values resolve in `ActionRegistry::find_condition`
12. Default phase timeout > 0
13. All string pool offsets < `total_size - 4`

Failure → `EngineError` returned, file rejected, no engine state mutated.

## Edge cases

- **Empty pools** (no actions/no globals/no resources) — represented by `*_count = 0`; offset can be 0 (loader skips).
- **Single-track scenario** — `track_count = 1`, the simplest valid form. Used in the `minimal_v1.modr` golden.
- **Unconditional transition** — `kind = MODR_TRANS_KIND_UNCONDITIONAL (4)`; `cond_pool_idx` and `time_threshold_ms` are ignored. Fires immediately after the phase entry actions complete. The engine rejects ambiguous combos (e.g., `kind=COND` with `cond_pool_idx=NO_OFFSET`) as `INVALID_FILE`.
- **Implicit timeout transition** — phase `timeout_ms = 0` falls back to `header.default_phase_timeout_ms`. If the timeout is reached AND no explicit time-based transition catches it → the engine synthesizes an implicit transition to `$abort`.

## Versioning policy

Format version bumps:
- **Patch (1.x):** comment-only changes, no struct layout changes. Same `format_version = 1`, but documented in an ADR.
- **Minor:** new optional fields at the end of a struct (zero-init OK for old loaders). Bump `format_version` to 2; the loader supports both.
- **Major (breaking):** struct layouts change incompatibly. New `format_version`. Migration tooling is provided (post Stage 1).

Currently at `format_version = 1`. Will bump if the binary layout changes after the first production deployment.

## See also

- ADR-0001 — rationale for binary format vs constexpr
- ADR-0007 — mandatory phase timeouts (validation rule 12)
- ADR-0008 — Step 0.75 paper pilot findings (phase-resource fields, +4 bytes)
- Plan `.claude/plans/quirky-imagining-lake.md` Q1 — original spec (with the arithmetic corrections noted above)
