# 06 — Resource Arbitration (ISA-88 §5.3)

**Status:** placeholder. Заповнюється у Step 10 (resource_arbiter.cpp).

## Заповнюється

- ISA-88 §5.3 model adaptation для embedded ModESP
- Resource declaration syntax у recipe manifest
- Acquire-before-start atomic semantics
- `RESOURCE_CONTENDED` error path
- Ownership map storage (`etl::flat_map<u16 hash, SequenceHandle, 32>`)
- Resource crash recovery via NVS owner mask
- Cross-module arbitration MVP semantic (last-write-wins; recipe author responsibility for module disable/re-enable)
- Stage 1.5 path: explicit Equipment Manager API integration
- Worked examples з recipe yaml snippets

## Reference

- ADR-0005 для ISA-88 adoption rationale
- Plan Q8 для current spec
