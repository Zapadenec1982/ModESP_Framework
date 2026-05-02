# ADR-0007: Mandatory Per-Phase Timeouts

**Status:** placeholder. Fully written у Step 8 (modr_loader validation rules).

## Decision summary

Кожна phase MUST мати timeout — або explicit `phase.timeout_ms` value, або fallback to `header.default_phase_timeout_ms` (mandatory у scenario header). Engine synthesizes implicit transition to `$abort` if no explicit time-based transition catches the timeout first.

**No "infinite phases."** Recipe author може set very high values (e.g., 24 hours), але mandatory bound exists.

## Alternatives considered

- **Optional timeouts:** rejected — universal у PLC sequencers (ISA-88 explicitly specifies "phase failure timeout"). Recipe can hang indefinitely without bound.
- **Single scenario-wide timeout only:** rejected — too coarse; phase-level timeout enables phase-specific safety.
- **Timeout у separate "safety contract" file:** rejected — over-engineering; embedded directly у `.modr` simpler.

## Consequences

- Loader validates timeout presence (or fallback availability) — compile error if missing
- Recipe authors can't accidentally create hang-forever phases
- Implicit `$abort` transition synthesized — safety net
- All recipes have predictable maximum runtime (sum of phase timeouts + scenario_timeout_max_ms upper bound)

## References

- ISA-88 phase failure handling
- Industrial sequencer best practices (Codesys, TwinCAT)
- Plan Q6 для implementation details
