# ADR-0003: Tick-Order Cross-Track Synchronization (NOT Snapshot)

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/adr/0003-tick-order-sync-semantics.md](../../../../uk/03-framework-reference/scenario-engine/adr/0003-tick-order-sync-semantics.md)

**Status:** placeholder. Fully written in Step 13 (test_track_synchronization).

## Decision summary

The Engine ticks instances and tracks in declaration order; it reads SharedState fresh on each `state_get()`. Writes from Track A ARE visible to Track B if B ticks AFTER A in the same tick. All writes become visible to all tracks on the next tick.

## Alternatives considered

- **Snapshot semantics** (writes in tick N → visible in tick N+1): more deterministic, but counter-intuitive ("why don't I see what I just wrote?"). Rejected.
- **Two-mode** (per-recipe `instant_cross_track_sync` flag): added complexity without clear benefit. Rejected.

## Consequences

- Order-dependent: recipe authors must declare tracks producer-before-consumer
- Predictable for the typical sequencer mental model
- Anti-patterns documented in `usage/02_writing_recipes.md`

## References

- Plan Q6 for the current spec
- `05_synchronization.md` for worked examples (filled in Step 13)
