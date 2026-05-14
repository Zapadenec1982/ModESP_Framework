# ADR-0007: Mandatory Per-Phase Timeouts

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/adr/0007-mandatory-phase-timeouts.md](../../../../uk/03-framework-reference/scenario-engine/adr/0007-mandatory-phase-timeouts.md)

**Status:** placeholder. Fully written in Step 8 (modr_loader validation rules).

## Decision summary

Every phase MUST have a timeout — either an explicit `phase.timeout_ms` value, or a fallback to `header.default_phase_timeout_ms` (mandatory in the scenario header). The Engine synthesizes an implicit transition to `$abort` if no explicit time-based transition catches the timeout first.

**No "infinite phases."** A recipe author may set very high values (e.g., 24 hours), but a mandatory bound always exists.

## Alternatives considered

- **Optional timeouts:** rejected — universal in PLC sequencers (ISA-88 explicitly specifies a "phase failure timeout"). A recipe could hang indefinitely without a bound.
- **Single scenario-wide timeout only:** rejected — too coarse; a phase-level timeout enables phase-specific safety.
- **Timeout in a separate "safety contract" file:** rejected — over-engineering; embedding directly in `.modr` is simpler.

## Consequences

- The loader validates timeout presence (or fallback availability) — compile error if missing
- Recipe authors cannot accidentally create hang-forever phases
- An implicit `$abort` transition is synthesized — safety net
- All recipes have a predictable maximum runtime (sum of phase timeouts, capped by `scenario_timeout_max_ms`)

## References

- ISA-88 phase failure handling
- Industrial sequencer best practices (Codesys, TwinCAT)
- Plan Q6 for implementation details
