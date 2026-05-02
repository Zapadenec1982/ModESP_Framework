# 10 — Error Model

**Status:** placeholder. Заповнюється incrementally на step 7 (action failure policy table) і step 8 (loader errors).

## Заповнюється

### `EngineError` codes (повна таблиця)

Для кожного code:
- Numeric value
- Trigger conditions
- Recommended caller handling
- Logged message format

Codes (per plan Q2):
- `OK`
- `INVALID_FILE`, `UNSUPPORTED_VERSION`, `CRC_MISMATCH`
- `UNKNOWN_ACTION`, `UNKNOWN_CONDITION`
- `PARAM_OUT_OF_RANGE`, `PARAM_NOT_OVERRIDABLE`
- `TOO_MANY_TRACKS`, `NAME_TOO_LONG`
- `NO_SLOT`, `NOT_LOADED`, `INVALID_HANDLE`, `INVALID_TRACK`
- `INVALID_TRANSITION`, `BUFFER_OVERFLOW`
- `NVS_ERROR`
- `RESOURCE_CONTENDED`
- `ABORTED_BY_SAFETY`

### Compiler error codes (`E01XX`-`E04XX`)

Для кожного code:
- Numeric ID
- Trigger description
- Example trigger snippet
- Fix instruction

### Action failure policy machine (Q12)

Повна 4×3 таблиця: `OK`/`PENDING`/`FAILED_RECOVERABLE`/`FAILED_ABORT` × entry/exit/continuous → engine behavior.

`flags.fail_aborts` per-action override semantics.

PENDING escalation timeout (1 sec default).

Diagnostic state keys engine writes on failure:
- `recipe_<name>.<track>_last_action_error`
- `recipe_<name>.<track>_last_action_name`
- `recipe_<name>.<track>_failure_count`

## Reference

- Plan Q12 для current spec
