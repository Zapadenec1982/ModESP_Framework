# ADR-0003: Tick-Order Cross-Track Synchronization (NOT Snapshot)

**Status:** placeholder. Fully written у Step 13 (test_track_synchronization).

## Decision summary

Engine ticks instances + tracks у declaration order; reads SharedState fresh кожного `state_get()`. Track A's writes ARE visible Track B якщо B ticks AFTER A у same tick. All writes visible to all tracks на наступному tick.

## Alternatives considered

- **Snapshot semantics** (writes у tick N → visible на tick N+1): more deterministic, але counter-intuitive ("чому я не бачу те, що щойно написав?"). Rejected.
- **Two-mode** (per-recipe `instant_cross_track_sync` flag): added complexity без clear benefit. Rejected.

## Consequences

- Order-dependent: recipe authors must declare tracks producer-before-consumer
- Predictable for typical sequencer mental model
- Anti-patterns documented у `usage/02_writing_recipes.md`

## References

- Plan Q6 для current spec
- `05_synchronization.md` для worked examples (filled у Step 13)
