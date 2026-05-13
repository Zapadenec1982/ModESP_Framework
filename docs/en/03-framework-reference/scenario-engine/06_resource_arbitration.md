# 06 — Resource Arbitration (ISA-88 §5.3)

Engine ensures що concurrent scenarios don't conflict over shared resources
(actuators, controllers, hardware modules). Adapts ISA-88 §5.3 — а chemical
batch-processing standard — to ModESP's embedded constraints.

## Two scopes

### Scenario-scope

Claimed atomically на `start()`, released on completion / abort / unload.
Used для long-held resources що а scenario "owns" for its full duration:
а dedicated heater controller, exclusive access до а sensor bank, etc.

```jsonc
"scenario": {
  "resources": [
    {"resource": "equipment.heater", "exclusive": true}
  ],
  "tracks": [...]
}
```

`start()` calls `arbiter.acquire_scenario(handle, resources, count)`.
Atomic all-or-nothing: якщо ANY listed resource is held by а different
handle, returns `RESOURCE_CONTENDED` із NO partial acquisition.

### Phase-scope

Claimed at phase entry, released at phase exit. Used для briefly-held
shared resources (e.g. greenhouse irrigation pump shared across multiple
zones, tested by а sample probe used briefly у multiple phases).

```jsonc
"tracks": [{
  "phases": [{
    "name": "watering",
    "phase_resources": [{"resource": "equipment.pump", "exclusive": true}],
    ...
  }]
}]
```

`track_tick` calls `arbiter.try_acquire_phase(handle, track, phase, claims, count)`
on phase entry. On contention, track enters `WAITING_FOR_RESOURCE` state і
retries every tick until acquired або phase timeout fires.

## Ownership tracking

`etl::flat_map<uint16_t resource_hash, OwnerInfo, MAX_RESOURCES=32>`.
Single owner per resource hash у MVP (multi-owner shared semantics — Stage 1.5).

```cpp
struct OwnerInfo {
    SequenceHandle handle;      // 1..MAX_SEQUENCES
    TrackIdx       track_idx;   // 0xFF = scenario-scope
    uint8_t        phase_idx;   // diagnostic
    uint8_t        exclusive;   // 1 = exclusive
};
```

`TRACK_IDX_SCENARIO = 0xFF` distinguishes scenario-scope ownership from
per-track. Same handle can hold both scenario-scope і phase-scope on
different resources concurrently.

## Atomic acquire algorithm

Two-phase commit (per `acquire_scenario` / `try_acquire_phase`):

```
Phase 1 (dry-run): for each resource у batch:
    if can_grant(hash, exclusive, requestor) == false:
        return RESOURCE_CONTENDED

Phase 2 (commit): for each resource:
    if already owned by це same handle:
        skip (idempotent re-grant; mark inserted[i] = false)
    if owners_.full():
        rollback: erase entries що inserted[j] == true для j < i
        return RESOURCE_CONTENDED
    insert ownership; mark inserted[i] = true

return OK
```

Bitmap `inserted[]` (added у post-review fix) ensures rollback only erases
entries що actually inserted у the failing call — preserves pre-existing
same-owner ownerships що were idempotent re-grants.

## `can_grant` rules

| Existing owner | Requestor exclusive? | Allowed? |
|---|---|---|
| None (free) | Either | Yes |
| Same (handle, track) | Either | Yes (idempotent re-grant) |
| Different + existing exclusive | Either | No |
| Different + existing shared | Exclusive | No |
| Different + existing shared | Shared | No (MVP single-owner map; Stage 1.5 multi-owner) |

**Important MVP caveat:** the "shared+shared OK" semantic typical у
`std::shared_mutex` is **not implemented** — single-owner map per resource.
Recipes що declare `exclusive: 0` get the same behavior as `exclusive: 1`
(rejected on cross-handle conflict). Documented у `resource_arbiter.h`
header.

## Recipe declaration syntax

```jsonc
"scenario": {
  "resources": [
    {"resource": "equipment.heater_zone1", "exclusive": true},
    {"resource": "equipment.shared_sensor", "exclusive": false}
  ],
  "tracks": [{
    "name": "main",
    "phases": [{
      "name": "warmup",
      "phase_resources": [
        {"resource": "equipment.fan", "exclusive": true}
      ],
      ...
    }]
  }]
}
```

Compiler computes `djb2_hash16(resource_name)` і emits hash to
`modr_resource_decl` / `modr_phase_resource_claim` records у `.modr`.

## Lifecycle integration

| Event | Arbiter call |
|-------|--------------|
| `engine.start(h)` | `acquire_scenario(h, resources, count)` |
| Track enters new phase із `phase_resource_n > 0` | `try_acquire_phase(h, track, phase_idx, claims, count)` |
| Track exits phase (transition fires або abort) | `release_phase(h, track)` |
| Track FAILED (action FAILED_ABORT, scenario-level abort, phase timeout) | `release_phase(h, track)` |
| Scenario reaches COMPLETED або FAILED | `release_scenario(h)` |
| `engine.unload(h)` | Both `release_scenario(h)` AND `release_phase(h, t)` для всіх t |

Abort path (added post-review): `instance_abort` releases phase-scope
resources for each non-terminal track BEFORE forcing it to FAILED. Без
цього, track_tick early-returns on FAILED і phase resources leak.

## Cross-module arbitration (MVP scope)

Engine arbitrates ONLY between scenarios (handles). Conflicts BETWEEN
а scenario AND а business module (e.g. simple_thermo writing до the same
SharedState request key) are **NOT** arbitrated — last-write-wins on
the underlying SharedState entry.

**Recipe author responsibility:**
- If recipe controls hardware actuator, disable conflicting business modules
  via `set_state` action на phase entry (e.g. `simple_thermo.enabled = false`)
- Re-enable on completion/abort через exit actions
- Abort handlers MUST be idempotent re-enable — engine не auto-restores
  disabled modules; recipe author tests abort path explicitly

This is а documented MVP constraint per plan Q8. Stage 1.5 enhancement:
explicit cross-module arbitration through Equipment Manager API.

## Recovery observability

Engine writes `scenario.engine_recovery_pending = true` after recovering
а scenario що was holding resources. UI surfaces це via `visible_when`
constraints; user explicitly chooses resume або abort. Не auto-recovery —
human-in-the-loop ensures hardware state matches recipe expectations.

(Stage 1.5 — currently the recovery key is reserved у engine manifest but
not yet written by engine code; добавляється at the same time as
"recovery banner" WebUI feature.)

## Worked example: greenhouse irrigation

Two recipe instances controlling 4 zones. Pump shared across zones.

```jsonc
// Recipe "irrig_a" — zones 1+2
"scenario": {
  "resources": [
    {"resource": "zone.1", "exclusive": true},
    {"resource": "zone.2", "exclusive": true}
  ],
  "tracks": [{
    "name": "main",
    "phases": [{
      "name": "water_zone_1",
      "phase_resources": [{"resource": "pump", "exclusive": true}],
      "entry": [{"action": "set_state",
                 "params": {"key": "zone.1.valve", "type": "bool", "value": true}}],
      "transitions": [{"to": "water_zone_2", "when": {"time_elapsed_ms": 30000}}]
    }, {
      "name": "water_zone_2",
      "phase_resources": [{"resource": "pump", "exclusive": true}],
      "entry": [{"action": "set_state",
                 "params": {"key": "zone.2.valve", "type": "bool", "value": true}}],
      "transitions": [{"to": "$complete", "when": {"time_elapsed_ms": 30000}}]
    }]
  }]
}

// Recipe "irrig_b" — zones 3+4 (analogous, claims pump per phase)
```

Two instances start simultaneously. Both claim zone-specific resources
scenario-scope (no conflict — different zones). Both want pump phase-scope:

- Instance A starts watering zone 1 — acquires pump.
- Instance B tries to start watering zone 3 — pump unavailable → enters
  `WAITING_FOR_RESOURCE`, phase_elapsed_ms accumulates.
- Instance A finishes zone 1 (30s) — phase exit releases pump.
- Instance B's next tick `try_acquire_phase` succeeds → exits WAITING.
- ...

Phase timeout serves as safety: якщо pump never frees (e.g. instance A
hung), instance B's phase eventually fails, scenario aborts.

## See also

- [adr/0005-isa88-resource-arbitration.md](adr/0005-isa88-resource-arbitration.md)
  — adoption rationale
- [03_api_reference.md](03_api_reference.md#resourcearbiter--isa-88-53)
  — public API
- [04_state_machines.md](04_state_machines.md) — WAITING_FOR_RESOURCE state behavior
- Source: `components/modesp_scenario/src/resource_arbiter.cpp`
