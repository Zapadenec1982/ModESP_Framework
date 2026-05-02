# 07 — NVS Persistence

**Status:** placeholder. Заповнюється у Step 15 (nvs_token.cpp).

## Заповнюється

- NVS namespace (`seqstate`) і key derivation (`t<slot>`)
- `seq_token` struct (96 bytes) з per-track sub-state
- Schema version field і migration policy
- Write policy: phase boundaries + 5-min checkpoint + state changes
- **Throttling rules:**
  - 1-second minimum interval applied to: checkpoints, side-track phase boundaries, scenario state changes
  - **Exception (immediate commit):** main_track phase boundaries, scenario start/abort/complete
- Wear budget calculation (worst case ~17k writes/day для 6-track distillation; 100k cycle/sector → ~6 days continuous; в реалі inverval-of-use → місяці)
- Recovery on boot: load token → restore phase_idx + elapsed_ms → reclaim resources → enter PAUSED (manual resume)
- Recovery observability: WARNING log + `scenario.engine_recovery_pending` state key
- Bad CRC handling: erase slot, log, treat as IDLE

## Reference

- Plan Q7 для current spec.
