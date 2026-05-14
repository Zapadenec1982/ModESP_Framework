# ADR-0006: No Built-in ContinuousBehaviors in the MVP

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/adr/0006-no-builtin-continuous-behaviors.md](../../../../uk/03-framework-reference/scenario-engine/adr/0006-no-builtin-continuous-behaviors.md)

## Status update (post-Stage 1)

Stage 2 introduced **standard continuous primitives** that ship with the framework: PID controller, hysteresis controller, ramp generator. They live in `components/modesp_scenario/include/modesp/scenario/continuous_primitives.h` and are wired through `ContinuousRegistry`. The original decision below ("0 built-ins") applied to Stage 1 MVP — domain modules can still register their own primitives via `ContinuousRegistry`, but the framework now provides standard reference implementations out of the box.

The rest of this ADR is kept verbatim as a historical record of the Stage 1 decision.

## Decision summary

The Engine ships a `ContinuousBehavior` abstract base plus `ContinuousRegistry` for extension, but **0 built-in implementations** in the Stage 1 MVP. Domain modules (multicooker, thermostat, etc.) register their own (PID, hysteresis, ramp, cron) at runtime via `ContinuousRegistry::register_factory()`.

## Alternatives considered

- **Ship PID** as built-in: rejected — the Engine is domain-agnostic; PID is not universal (some products use bang-bang, fuzzy, or model-predictive control). The user explicitly said no PID.
- **Ship cron-inside-phase** as built-in: rejected — wall-clock support is deferred; a phase-relative timer is sufficient via the `time_elapsed_ms` condition + transitions.
- **Defer the entire ContinuousBehavior interface** to Stage 1.5: rejected — interface design decisions impact the binary format (`cont_count` field, `cont_mask` per phase). Better to land the interface now even without implementations.

## Consequences

- The Engine is truly domain-agnostic (no shipped behaviors that imply a specific use case)
- ContinuousRegistry is tested via unit tests with mock implementations
- The first real ContinuousBehavior comes from a domain module post-MVP — this guides interface refinement
- The reference test recipe does not exercise continuous behaviors (`cont_count=0`)

## References

- User directive: "incorrect perception of the concept" (regarding PID being central)
- Plan Q5 for the interface signature
