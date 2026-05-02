# ADR-0005: ISA-88 §5.3 Resource Arbitration (NOT Mutex)

**Status:** placeholder. Fully written у Step 10 (resource_arbiter.cpp).

## Decision summary

Recipe declares required resources у scenario header. Engine arbitrates через "acquire-before-start + ownership transfer" pattern (ISA-88 §5.3 idiom):
- `start(handle)` — atomic acquire-all-or-fail
- `RESOURCE_CONTENDED` returned without partial acquisition
- Ownership map: `etl::flat_map<u16 hash, SequenceHandle, 32>`
- Crash recovery via NVS owner mask

NOT mutex-based locking — mutex held across reboot = deadlock. Owner field у NVS reclaimable deterministically.

**Stage 1 MVP:** scenario-scoped only. Cross-module conflict (engine vs `simple_thermo` writing same key) — last-write-wins з recipe author responsibility.

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
