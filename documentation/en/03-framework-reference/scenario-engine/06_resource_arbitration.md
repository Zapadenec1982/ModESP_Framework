# 06 — Resource Arbitration (ISA-88 §5.3)

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/06_resource_arbitration.md](../../../uk/03-framework-reference/scenario-engine/06_resource_arbitration.md)

The engine ensures that concurrent scenarios do not conflict over shared
resources (actuators, controllers, hardware modules). It adapts ISA-88
§5.3 — a chemical batch-processing standard — to ModESP's embedded
constraints.

## Two scopes

### Scenario-scope

Claimed atomically on `start()`, released on completion, abort, or
unload. Used for long-held resources that a scenario "owns" for its full
duration: a dedicated heater controller, exclusive access to a sensor
bank, and so on.

```jsonc
"scenario": {
  "resources": [
    {"resource": "equipment.heater", "exclusive": true}
  ],
  "tracks": [...]
}
```

`start()` calls `arbiter.acquire_scenario(handle, resources, count)`.
Atomic all-or-nothing: if ANY listed resource is held by a different
handle, it returns `RESOURCE_CONTENDED` with NO partial acquisition.

### Phase-scope

Claimed at phase entry, released at phase exit. Used for briefly-held
shared resources (for example, a greenhouse irrigation pump shared
across multiple zones, or a sample probe used briefly in multiple
phases).

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
on phase entry. On contention, the track enters `WAITING_FOR_RESOURCE`
state and retries every tick until acquired, or until the phase timeout
fires.

## Ownership tracking

`etl::flat_map<uint16_t resource_hash, OwnerInfo, MAX_RESOURCES=32>`.
Single owner per resource hash in the MVP (multi-owner shared
semantics — Stage 1.5).

```cpp
struct OwnerInfo {
    SequenceHandle handle;      // 1..MAX_SEQUENCES
    TrackIdx       track_idx;   // 0xFF = scenario-scope
    uint8_t        phase_idx;   // diagnostic
    uint8_t        exclusive;   // 1 = exclusive
};
```

`TRACK_IDX_SCENARIO = 0xFF` distinguishes scenario-scope ownership from
per-track ownership. The same handle can hold both scenario-scope and
phase-scope ownership on different resources concurrently.

## Atomic acquire algorithm

Two-phase commit (per `acquire_scenario` / `try_acquire_phase`):

```
Phase 1 (dry-run): for each resource in batch:
    if can_grant(hash, exclusive, requestor) == false:
        return RESOURCE_CONTENDED

Phase 2 (commit): for each resource:
    if already owned by this same handle:
        skip (idempotent re-grant; mark inserted[i] = false)
    if owners_.full():
        rollback: erase entries where inserted[j] == true for j < i
        return RESOURCE_CONTENDED
    insert ownership; mark inserted[i] = true

return OK
```

The `inserted[]` bitmap (added in a post-review fix) ensures rollback
only erases entries actually inserted in the failing call — preserving
pre-existing same-owner ownerships that were idempotent re-grants.

## `can_grant` rules

| Existing owner | Requestor exclusive? | Allowed? |
|---|---|---|
| None (free) | Either | Yes |
| Same (handle, track) | Either | Yes (idempotent re-grant) |
| Different + existing exclusive | Either | No |
| Different + existing shared | Exclusive | No |
| Different + existing shared | Shared | No (MVP single-owner map; Stage 1.5 multi-owner) |

**Important MVP caveat:** the "shared+shared OK" semantic typical of
`std::shared_mutex` is **not implemented** — single-owner map per
resource. Recipes that declare `exclusive: 0` get the same behavior as
`exclusive: 1` (rejected on cross-handle conflict). Documented in the
`resource_arbiter.h` header.

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

The compiler computes `djb2_hash16(resource_name)` and emits the hash to
`modr_resource_decl` / `modr_phase_resource_claim` records in the
`.modr` file.

## Lifecycle integration

| Event | Arbiter call |
|-------|--------------|
| `engine.start(h)` | `acquire_scenario(h, resources, count)` |
| Track enters new phase with `phase_resource_n > 0` | `try_acquire_phase(h, track, phase_idx, claims, count)` |
| Track exits phase (transition fires or abort) | `release_phase(h, track)` |
| Track FAILED (action FAILED_ABORT, scenario-level abort, phase timeout) | `release_phase(h, track)` |
| Scenario reaches COMPLETED or FAILED | `release_scenario(h)` |
| `engine.unload(h)` | Both `release_scenario(h)` AND `release_phase(h, t)` for all t |

Abort path (added post-review): `instance_abort` releases phase-scope
resources for each non-terminal track BEFORE forcing it to FAILED.
Without this, `track_tick` early-returns on FAILED and phase resources
leak.

## Cross-module arbitration (MVP scope)

The engine arbitrates ONLY between scenarios (handles). Conflicts
BETWEEN a scenario AND a business module (for example, simple_thermo
writing to the same SharedState request key) are **NOT** arbitrated —
last-write-wins on the underlying SharedState entry.

**Recipe author responsibility:**
- If a recipe controls a hardware actuator, disable conflicting business
  modules via the `set_state` action on phase entry (e.g.
  `simple_thermo.enabled = false`).
- Re-enable on completion or abort via exit actions.
- Abort handlers MUST be idempotent re-enable — the engine does not
  auto-restore disabled modules; the recipe author tests the abort path
  explicitly.

This is a documented MVP constraint per plan Q8. Stage 1.5 enhancement:
explicit cross-module arbitration through the Equipment Manager API.

## Recovery observability

The engine writes `scenario.engine_recovery_pending = true` after
recovering a scenario that was holding resources. The UI surfaces this
via `visible_when` constraints; the user explicitly chooses resume or
abort. No auto-recovery — a human-in-the-loop ensures hardware state
matches recipe expectations.

(Stage 1.5 — currently the recovery key is reserved in the engine
manifest but not yet written by engine code; it will be added at the
same time as the "recovery banner" WebUI feature.)

## Worked example: greenhouse irrigation

Two recipe instances controlling 4 zones. The pump is shared across
zones.

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
scenario-scope (no conflict — different zones). Both want the pump
phase-scope:

- Instance A starts watering zone 1 — acquires the pump.
- Instance B tries to start watering zone 3 — pump unavailable → enters
  `WAITING_FOR_RESOURCE`, `phase_elapsed_ms` accumulates.
- Instance A finishes zone 1 (30s) — phase exit releases the pump.
- Instance B's next tick `try_acquire_phase` succeeds → exits WAITING.
- ...

The phase timeout serves as a safety net: if the pump never frees
(for example, instance A hung), instance B's phase eventually fails and
the scenario aborts.

## See also

- [adr/0005-isa88-resource-arbitration.md](adr/0005-isa88-resource-arbitration.md)
  — adoption rationale
- [03_api_reference.md](03_api_reference.md#resourcearbiter--isa-88-53)
  — public API
- [04_state_machines.md](04_state_machines.md) — WAITING_FOR_RESOURCE state behavior
- Source: `components/modesp_scenario/src/resource_arbiter.cpp`
