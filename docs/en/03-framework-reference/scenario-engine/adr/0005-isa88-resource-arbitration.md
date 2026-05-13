# ADR-0005: ISA-88 §5.3 Resource Arbitration (NOT Mutex)

**Status:** placeholder. Fully written у Step 10 (resource_arbiter.cpp).

## Decision summary

Recipe declares required resources at TWO scopes (revised post Step 0.75 paper pilot):

### Scenario-scope resources
Claimed atomically at `start(handle)`, released on completion/abort/unload. Use cases: dedicated controller per scenario, long-held HW.
- `RESOURCE_CONTENDED` returned without partial acquisition
- Ownership map (scenario): `etl::flat_map<u16 hash, OwnerInfo, 32>` де OwnerInfo {handle, track=0xFF, phase=0xFF}

### Phase-scope resources (NEW post-pilot)
Claimed at phase entry by specific track, released at phase exit. Use cases: shared pump між zones (greenhouse), shared sensor read slot.
- If unavailable at phase entry: phase enters WAITING_FOR_RESOURCE sub-state, engine retries each tick
- Phase timeout still applies (waiting too long → timeout transition)
- Ownership map (phase): same map як scenario, але OwnerInfo має track + phase indices

NOT mutex-based locking — mutex held across reboot = deadlock. Owner field у NVS reclaimable deterministically.

**Stage 1 MVP:** scenario + phase scopes implemented. Cross-module conflict (engine vs `simple_thermo` writing same key) — last-write-wins з recipe author responsibility.

**Stage 1.5:** explicit Equipment Manager API integration.

## Alternatives considered

- Raw FreeRTOS mutexes: rejected (crash deadlock, no priority inversion safety)
- Reactive arbitration (let modules write whenever): rejected (no ownership clarity)
- Two-phase commit: rejected (overkill для embedded scope)

## Consequences

- ISA-88 alignment maintained
- Compatible з existing Equipment Manager priority pattern (Protection > Defrost > Thermostat у foundation doc)
- Crash-safe (owner field deterministically restorable)
- Recipe authors responsible для disabling conflicting business modules (Q8)

## References

- ANSI/ISA-88.01-2010 §5.3 ("Equipment entities")
- Plan Q8 для current spec
- `06_resource_arbitration.md` для full mapping
