# ADR-0006: No Built-in ContinuousBehaviors у MVP

**Status:** placeholder. Fully written у Step 6 (continuous_registry).

## Decision summary

Engine ships `ContinuousBehavior` abstract base + `ContinuousRegistry` для extension, але **0 built-in implementations** у Stage 1 MVP. Domain modules (multicooker, thermostat, etc.) register their own (PID, hysteresis, ramp, cron) at runtime через `ContinuousRegistry::register_factory()`.

## Alternatives considered

- **Ship PID** як built-in: rejected — engine domain-agnostic; PID не universal (some products use bang-bang, fuzzy, model-predictive). User explicitly said no PID.
- **Ship cron-inside-phase** як built-in: rejected — wall-clock support deferred; phase-relative timer is enough through `time_elapsed_ms` condition + transitions.
- **Defer entire ContinuousBehavior interface** до Stage 1.5: rejected — interface design decisions impact binary format (`cont_count` field, `cont_mask` per phase). Better to land interface now even without implementations.

## Consequences

- Engine truly domain-agnostic (no shipped behaviors that imply specific use case)
- ContinuousRegistry tested via unit tests з mock implementations
- First real ContinuousBehavior comes from domain module post-MVP — guides interface refinement
- Reference test recipe doesn't exercise continuous (cont_count=0)

## References

- User directive: "не вірне сприйняття концепції" (про PID being central)
- Plan Q5 для interface signature
