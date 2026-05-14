# ADR-0005: ISA-88 §5.3 Resource Arbitration (NOT Mutex)

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/adr/0005-isa88-resource-arbitration.md](../../../../uk/03-framework-reference/scenario-engine/adr/0005-isa88-resource-arbitration.md)

**Status:** placeholder. Fully written in Step 10 (resource_arbiter.cpp).

## Decision summary

A recipe declares required resources at TWO scopes (revised after the Step 0.75 paper pilot):

### Scenario-scope resources
Claimed atomically at `start(handle)`, released on completion/abort/unload. Use cases: a dedicated controller per scenario, long-held hardware.
- `RESOURCE_CONTENDED` is returned without partial acquisition
- Ownership map (scenario): `etl::flat_map<u16 hash, OwnerInfo, 32>` where `OwnerInfo {handle, track=0xFF, phase=0xFF}`

### Phase-scope resources (NEW after the pilot)
Claimed at phase entry by a specific track, released at phase exit. Use cases: a shared pump between zones (greenhouse), a shared sensor read slot.
- If unavailable at phase entry: the phase enters the WAITING_FOR_RESOURCE sub-state; the engine retries each tick
- The phase timeout still applies (waiting too long → timeout transition)
- Ownership map (phase): same map as scenario, but `OwnerInfo` carries the track and phase indices

NOT mutex-based locking — a mutex held across reboot is a deadlock. The owner field in NVS is deterministically reclaimable.

**Stage 1 MVP:** scenario and phase scopes implemented. Cross-module conflict (engine vs `simple_thermo` writing the same key) — last-write-wins, recipe author's responsibility.

**Stage 1.5:** explicit Equipment Manager API integration.

## Alternatives considered

- Raw FreeRTOS mutexes: rejected (crash deadlock, no priority-inversion safety)
- Reactive arbitration (let modules write whenever): rejected (no ownership clarity)
- Two-phase commit: rejected (overkill for the embedded scope)

## Consequences

- ISA-88 alignment maintained
- Compatible with the existing Equipment Manager priority pattern (Protection > Defrost > Thermostat in the foundation doc)
- Crash-safe (the owner field is deterministically restorable)
- Recipe authors are responsible for disabling conflicting business modules (Q8)

## References

- ANSI/ISA-88.01-2010 §5.3 ("Equipment entities")
- Plan Q8 for the current spec
- `06_resource_arbitration.md` for the full mapping
