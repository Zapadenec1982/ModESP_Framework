# 10 — Error Model

Catalog of error codes returned by SequenceEngine API і loader, plus the
action failure policy machine. Use це as the lookup table when handling
EngineError у business modules або interpreting HTTP API error responses.

## `EngineError` codes

`enum class EngineError : uint8_t` defined у `engine_error.h`. Returned
through API methods (`load`, `start`, `pause`, `abort`, `unload`,
`try_recover`) і loader (`modr_validate`).

| Code | Triggered by | Caller handling |
|---|---|---|
| `OK` (0) | Success | Continue normal flow |
| `INVALID_FILE` | Bad magic, truncated `.modr`, total_size > MODR_MAX_SIZE, malformed structures | Recipe corrupted або wrong file format. Verify path, regenerate `.modr` from manifest |
| `UNSUPPORTED_VERSION` | `format_version` у header не matches `MODR_FORMAT_VERSION` | Recipe compiled з older/newer compiler than firmware. Recompile recipes |
| `CRC_MISMATCH` | Trailer CRC32 не matches computed | File corrupted (filesystem bit-flip, transmission error). Re-upload recipe |
| `BUFFER_OVERFLOW` | Internal offset+count would read past `total_size` | Loader-detected structural corruption. Treated як malicious-input safe-fail |
| `UNKNOWN_ACTION` | `action_pool` entry's hash не у ActionRegistry | Recipe references unregistered action. Either recipe author typo або required module не registered before engine init |
| `UNKNOWN_CONDITION` | `cond_pool` leaf entry's hash не у conditions registry AND не composite (all_of/any_of/not) | Same — recipe references unknown condition |
| `INVALID_TRANSITION` | Bad `kind` enum, `target_phase` out of range, `kind=COND` with `cond_pool_idx == NO_OFFSET` | Compiler bug або corrupted recipe |
| `TOO_MANY_TRACKS` | `track_count` > MAX_TRACKS_PER_SCENARIO (6) | Recipe declares more parallel tracks than engine supports. Refactor recipe |
| `NAME_TOO_LONG` | Recipe name або track name exceeds budget | Use shorter names (recipe ≤ 12 chars, track ≤ 8 chars) |
| `NO_SLOT` | All MAX_SEQUENCES slots occupied | Unload а completed/failed scenario before loading another |
| `NOT_LOADED` | Operation requires LOADED state, slot is empty/active/terminal | Verify state via `engine.state(handle)` before operation |
| `INVALID_HANDLE` | Handle 0 або > MAX_SEQUENCES, або points to empty slot | Caller passed stale або made-up handle |
| `INVALID_TRACK` | Track index ≥ recipe's track_count у diagnostic accessor | Caller iterating beyond `track_count(handle)` |
| `PARAM_OUT_OF_RANGE` | (Stage 2) override value не у [min, max] | WebUI editor should validate before sending |
| `PARAM_NOT_OVERRIDABLE` | (Stage 2) override targets param без OVERRIDABLE flag | Recipe author marks specific params overridable у manifest |
| `RESOURCE_CONTENDED` | Scenario-scope resource already held by а different handle | Wait for owning scenario to complete/release, OR refactor recipes що don't conflict |
| `NVS_ERROR` | Persist callback returned false (no data, write failure) | Check NVS partition mounted, recovery callback configured |
| `ABORTED_BY_SAFETY` | (Reserved) global transition fired з safety condition | Investigate why fault triggered |

## Compiler error codes (`tools/compile_scenario.py`)

Compiler emits errors у format `<file>:<line>:<col>: error[<code>]: <message>`.
Tooling (CI, VSCode) can parse це formatting via standard error regex.

### E01XX — Schema validation

Generic JSON schema mismatches caught by jsonschema.

| Code | Trigger | Example | Fix |
|---|---|---|---|
| E0101 | Generic schema validation failure | Required field missing | Read schema у `scenario_schema.json` для exact requirements |

### E02XX — Semantic validation

Reference і type validation що jsonschema can't express.

| Code | Trigger | Fix |
|---|---|---|
| E0202 | Action hash у `known_actions.json` doesn't match djb2 of name | Recompute hash, fix table |
| E0203 | Action name collision у `known_actions.json` | Rename one action |
| E0208 | Duplicate phase name within а track, або duplicate track name | Rename |
| E0211 | Condition expression не single-key object | Fix recipe — each condition is `{"op": ...}` (one key) |
| E0212 | `time_elapsed_ms` value not non-negative int | Use 0 or positive integer |
| E0213 | `state_key_changed` body missing `key` field | Add `{"key": "..."}` |
| E0214 | `time_of_day_eq` body missing `hh` або `mm` | Add both |
| E0215 | `all_of` / `any_of` body не non-empty array | Use `[<cond1>, <cond2>, ...]` |
| E0218 | Condition expression nested deeper than `MAX_CONDITION_DEPTH` (16) | Flatten conditions; combine equivalent expressions |
| E0220 | Action name not у `known_actions.json` (action invocation у recipe) | Add to known_actions.json or fix typo |
| E0221 | `set_state` `type` parameter не у allowed set (i32/f32/bool) | Use valid type |
| E0222 | `set_state` `value` type doesn't match declared `type` | Match value type до declared type |
| E0230 | ContinuousBehavior name unknown (Stage 1.5) | Register continuous behavior |
| E0240 | `@param:<name>` references undefined recipe param | Add param до `scenario.params` |
| E0241 | `@param` value evaluates до wrong type для context | Override value type matches usage |

### E03XX — Binary emit

Errors під час byte-level encoding.

| Code | Trigger | Fix |
|---|---|---|
| E0301 | String pool overflow (>65535 bytes) | Reduce string usage, use shorter names |
| E0302 | Action/cond pool count exceeds 65535 | Refactor recipe (rare у practice) |

### E04XX — Cross-validation

Manifest's `state` section must declare every mirror key engine writes.

| Code | Trigger | Fix |
|---|---|---|
| E0401 | Engine will write key `<recipe>.<track>_phase_name` але не declared у manifest.state | Add missing state entry |
| E0402 | Engine will write key із specific type, але manifest declares different type | Fix type у manifest |
| E0403 | Same — strict mode escalates type drift to error | (--strict only) |

### Warnings

| Code | Severity | Trigger | Fix |
|---|---|---|---|
| W0220 | Warning (--strict → error) | Action name not у known_actions.json | Add registration |
| W0230 | Warning (--strict → error) | ContinuousBehavior reference unknown | Register або remove |

## Action failure policy machine

Per plan Q12. Action returns `ActionStatus`; engine reacts based on context:

### Entry actions

| Status | Engine behavior |
|---|---|
| `OK` | Advance to next entry action; після all done, evaluate transitions |
| `PENDING` | Re-call same action next tick. Engine increments `action_pending_ticks`; реалізм escalates після ~100 ticks (1 sec @ 10ms) to FAILED_RECOVERABLE — Stage 1.5 (currently stays PENDING indefinitely; recipes should use `wait_ms` для bounded delays) |
| `FAILED_RECOVERABLE` | Skip remaining entry actions; jump straight to transition evaluation |
| `FAILED_ABORT` | Track → FAILED. Phase-scope resources released. Якщо track has `main_track` flag, scenario also FAILED |

### Exit actions

| Status | Engine behavior |
|---|---|
| `OK` | Advance to next exit action; після all done, apply latched transition |
| `PENDING` | Same як entry — defer one tick |
| `FAILED_RECOVERABLE` / `FAILED_ABORT` | Skip remaining exits; apply transition. Failure logged; doesn't block recipe progression |

### Continuous tick (Stage 1.5+)

| Status | Engine behavior |
|---|---|
| `OK` / `PENDING` | Continue continuous behavior |
| `FAILED_RECOVERABLE` | Deactivate ContinuousBehavior; track stays у RUNNING |
| `FAILED_ABORT` | Track → FAILED |

### Per-action `flags.fail_aborts` override

Action descriptor `modr_action.flags` has bits:
- `MODR_ACTION_FLAG_FAIL_ABORTS` (bit 0): treat `FAILED_RECOVERABLE` як `FAILED_ABORT`. Use для safety-critical actions
- `MODR_ACTION_FLAG_FAIL_LOGS_ONLY` (bit 1): defang `FAILED_ABORT` to `FAILED_RECOVERABLE`. Use sparingly

### Diagnostic state keys engine writes on failure

Engine writes (Stage 1.5; not yet implemented but reserved):
- `recipe_<name>.<track>_last_action_error` (int — error code)
- `recipe_<name>.<track>_last_action_name` (string)
- `recipe_<name>.<track>_failure_count` (int — increments per phase)

Recipe authors include these у `manifest.state` для UI visibility.

## HTTP API error format

JSON response shape on engine error:

```json
{"ok": false, "error": "<engine_error_string>"}
```

`engine_error_string` matches `EngineError` enum stringified:
- `ok` (typically not seen — success returns `{"ok": true, ...}`)
- `invalid_file`, `unsupported_version`, `crc_mismatch`, `buffer_overflow`
- `unknown_action`, `unknown_condition`, `invalid_transition`
- `too_many_tracks`, `name_too_long`
- `no_slot`, `not_loaded`, `invalid_handle`, `invalid_track`
- `param_out_of_range`, `param_not_overridable`
- `resource_contended`, `nvs_error`, `aborted_by_safety`

HTTP status: 400 для recipe/caller errors, 500 для engine misconfiguration
(e.g. engine pointer nullptr).

## See also

- [usage/02_writing_recipes.md](usage/02_writing_recipes.md) — common
  authoring mistakes that produce E02XX errors
- [usage/03_registering_actions.md](usage/03_registering_actions.md) —
  W0220 / E0220 mitigation (registration timing)
- `components/modesp_scenario/include/modesp/scenario/engine_error.h`
  — authoritative source
