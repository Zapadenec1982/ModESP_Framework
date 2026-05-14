# 10 — Error Model

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/10_error_model.md](../../../uk/03-framework-reference/scenario-engine/10_error_model.md)

Catalog of error codes returned by the SequenceEngine API and loader, plus the
action failure policy machine. Use this as the lookup table when handling
EngineError in business modules or interpreting HTTP API error responses.

## `EngineError` codes

`enum class EngineError : uint8_t` defined in `engine_error.h`. Returned
through API methods (`load`, `start`, `pause`, `abort`, `unload`,
`try_recover`) and the loader (`modr_validate`).

| Code | Triggered by | Caller handling |
|---|---|---|
| `OK` (0) | Success | Continue normal flow |
| `INVALID_FILE` | Bad magic, truncated `.modr`, total_size > MODR_MAX_SIZE, malformed structures | Recipe is corrupted or in the wrong file format. Verify the path and regenerate `.modr` from the manifest |
| `UNSUPPORTED_VERSION` | `format_version` in the header does not match `MODR_FORMAT_VERSION` | Recipe was compiled with an older/newer compiler than the firmware. Recompile recipes |
| `CRC_MISMATCH` | Trailer CRC32 does not match the computed value | File corrupted (filesystem bit-flip, transmission error). Re-upload the recipe |
| `BUFFER_OVERFLOW` | Internal offset+count would read past `total_size` | Loader-detected structural corruption. Treated as a malicious-input safe-fail |
| `UNKNOWN_ACTION` | An `action_pool` entry's hash is not in ActionRegistry | Recipe references an unregistered action. Either a recipe-author typo, or the required module is not registered before engine init |
| `UNKNOWN_CONDITION` | A `cond_pool` leaf entry's hash is not in the conditions registry AND is not a composite (all_of/any_of/not) | Same — recipe references an unknown condition |
| `INVALID_TRANSITION` | Bad `kind` enum, `target_phase` out of range, or `kind=COND` with `cond_pool_idx == NO_OFFSET` | Compiler bug or corrupted recipe |
| `TOO_MANY_TRACKS` | `track_count` > MAX_TRACKS_PER_SCENARIO (6) | Recipe declares more parallel tracks than the engine supports. Refactor the recipe |
| `NAME_TOO_LONG` | Recipe name or track name exceeds budget | Use shorter names (recipe ≤ 12 chars, track ≤ 8 chars) |
| `NO_SLOT` | All MAX_SEQUENCES slots are occupied | Unload a completed/failed scenario before loading another |
| `NOT_LOADED` | Operation requires LOADED state, slot is empty/active/terminal | Verify state via `engine.state(handle)` before the operation |
| `INVALID_HANDLE` | Handle 0, or > MAX_SEQUENCES, or points to an empty slot | Caller passed a stale or fabricated handle |
| `INVALID_TRACK` | Track index ≥ recipe's track_count in a diagnostic accessor | Caller is iterating beyond `track_count(handle)` |
| `PARAM_OUT_OF_RANGE` | (Stage 2) override value not in [min, max] | WebUI editor should validate before sending |
| `PARAM_NOT_OVERRIDABLE` | (Stage 2) override targets a param without the OVERRIDABLE flag | Recipe author marks specific params overridable in the manifest |
| `RESOURCE_CONTENDED` | A scenario-scope resource is already held by a different handle | Wait for the owning scenario to complete/release, OR refactor recipes so they do not conflict |
| `NVS_ERROR` | Persist callback returned false (no data, write failure) | Check that the NVS partition is mounted and that a recovery callback is configured |
| `ABORTED_BY_SAFETY` | (Reserved) global transition fired from a safety condition | Investigate why the fault triggered |

## Compiler error codes (`tools/compile_scenario.py`)

The compiler emits errors in the format `<file>:<line>:<col>: error[<code>]: <message>`.
Tooling (CI, VSCode) can parse this format via standard error regex.

### E01XX — Schema validation

Generic JSON schema mismatches caught by jsonschema.

| Code | Trigger | Example | Fix |
|---|---|---|---|
| E0101 | Generic schema validation failure | Required field missing | Read the schema in `scenario_schema.json` for exact requirements |

### E02XX — Semantic validation

Reference and type validation that jsonschema cannot express.

| Code | Trigger | Fix |
|---|---|---|
| E0202 | Action hash in `known_actions.json` does not match djb2 of the name | Recompute hash, fix the table |
| E0203 | Action name collision in `known_actions.json` | Rename one action |
| E0208 | Duplicate phase name within a track, or duplicate track name | Rename |
| E0211 | Condition expression is not a single-key object | Fix recipe — each condition is `{"op": ...}` (one key) |
| E0212 | `time_elapsed_ms` value is not a non-negative int | Use 0 or a positive integer |
| E0213 | `state_key_changed` body missing `key` field | Add `{"key": "..."}` |
| E0214 | `time_of_day_eq` body missing `hh` or `mm` | Add both |
| E0215 | `all_of` / `any_of` body is not a non-empty array | Use `[<cond1>, <cond2>, ...]` |
| E0218 | Condition expression nested deeper than `MAX_CONDITION_DEPTH` (16) | Flatten conditions; combine equivalent expressions |
| E0220 | Action name not in `known_actions.json` (action invocation in a recipe) | Add to known_actions.json or fix typo |
| E0221 | `set_state` `type` parameter not in the allowed set (i32/f32/bool) | Use a valid type |
| E0222 | `set_state` `value` type does not match the declared `type` | Match the value type to the declared type |
| E0230 | ContinuousBehavior name unknown (Stage 1.5) | Register the continuous behavior |
| E0240 | `@param:<name>` references an undefined recipe param | Add the param to `scenario.params` |
| E0241 | `@param` value evaluates to the wrong type for the context | Override value type to match usage |

### E03XX — Binary emit

Errors during byte-level encoding.

| Code | Trigger | Fix |
|---|---|---|
| E0301 | String pool overflow (>65535 bytes) | Reduce string usage, use shorter names |
| E0302 | Action/cond pool count exceeds 65535 | Refactor the recipe (rare in practice) |

### E04XX — Cross-validation

The manifest's `state` section must declare every mirror key the engine writes.

| Code | Trigger | Fix |
|---|---|---|
| E0401 | Engine will write key `<recipe>.<track>_phase_name` but it is not declared in manifest.state | Add the missing state entry |
| E0402 | Engine will write a key with a specific type, but the manifest declares a different type | Fix type in the manifest |
| E0403 | Same — strict mode escalates type drift to an error | (--strict only) |

### Warnings

| Code | Severity | Trigger | Fix |
|---|---|---|---|
| W0220 | Warning (--strict → error) | Action name not in known_actions.json | Add registration |
| W0230 | Warning (--strict → error) | ContinuousBehavior reference unknown | Register or remove |

## Action failure policy machine

Per plan Q12. An action returns `ActionStatus`; the engine reacts based on context:

### Entry actions

| Status | Engine behavior |
|---|---|
| `OK` | Advance to the next entry action; after all are done, evaluate transitions |
| `PENDING` | Re-call the same action next tick. The engine increments `action_pending_ticks`; the runtime escalates after ~100 ticks (1 s @ 10 ms) to FAILED_RECOVERABLE — Stage 1.5 (currently stays PENDING indefinitely; recipes should use `wait_ms` for bounded delays) |
| `FAILED_RECOVERABLE` | Skip remaining entry actions; jump straight to transition evaluation |
| `FAILED_ABORT` | Track → FAILED. Phase-scope resources released. If the track has the `main_track` flag, the scenario is also FAILED |

### Exit actions

| Status | Engine behavior |
|---|---|
| `OK` | Advance to the next exit action; after all are done, apply the latched transition |
| `PENDING` | Same as entry — defer one tick |
| `FAILED_RECOVERABLE` / `FAILED_ABORT` | Skip remaining exits; apply transition. Failure logged; does not block recipe progression |

### Continuous tick (Stage 1.5+)

| Status | Engine behavior |
|---|---|
| `OK` / `PENDING` | Continue the continuous behavior |
| `FAILED_RECOVERABLE` | Deactivate ContinuousBehavior; track stays in RUNNING |
| `FAILED_ABORT` | Track → FAILED |

### Per-action `flags.fail_aborts` override

The action descriptor `modr_action.flags` has bits:
- `MODR_ACTION_FLAG_FAIL_ABORTS` (bit 0): treat `FAILED_RECOVERABLE` as `FAILED_ABORT`. Use for safety-critical actions.
- `MODR_ACTION_FLAG_FAIL_LOGS_ONLY` (bit 1): defang `FAILED_ABORT` to `FAILED_RECOVERABLE`. Use sparingly.

### Diagnostic state keys the engine writes on failure

The engine writes (Stage 1.5; not yet implemented but reserved):
- `recipe_<name>.<track>_last_action_error` (int — error code)
- `recipe_<name>.<track>_last_action_name` (string)
- `recipe_<name>.<track>_failure_count` (int — increments per phase)

Recipe authors include these in `manifest.state` for UI visibility.

## HTTP API error format

JSON response shape on engine error:

```json
{"ok": false, "error": "<engine_error_string>"}
```

`engine_error_string` matches the stringified `EngineError` enum:
- `ok` (typically not seen — success returns `{"ok": true, ...}`)
- `invalid_file`, `unsupported_version`, `crc_mismatch`, `buffer_overflow`
- `unknown_action`, `unknown_condition`, `invalid_transition`
- `too_many_tracks`, `name_too_long`
- `no_slot`, `not_loaded`, `invalid_handle`, `invalid_track`
- `param_out_of_range`, `param_not_overridable`
- `resource_contended`, `nvs_error`, `aborted_by_safety`

HTTP status: 400 for recipe/caller errors, 500 for engine misconfiguration
(e.g. engine pointer nullptr).

## See also

- [usage/02_writing_recipes.md](usage/02_writing_recipes.md) — common
  authoring mistakes that produce E02XX errors
- [usage/03_registering_actions.md](usage/03_registering_actions.md) —
  W0220 / E0220 mitigation (registration timing)
- `components/modesp_scenario/include/modesp/scenario/engine_error.h`
  — authoritative source
