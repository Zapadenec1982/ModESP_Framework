# 04 — State Machines

**Status:** placeholder. Заповнюється у Step 11 (sequence_track) і Step 12 (sequence_instance).

## Заповнюється

- **Scenario-level FSM** (per instance): IDLE → LOADED → RUNNING ↔ PAUSED → COMPLETED/FAILED with ABORTING phase
- **Per-track FSM** (within RUNNING scenario): track_initial_phase → phase progression → TRACK_COMPLETED/TRACK_FAILED
- **Completion rules** (interaction between scenario і tracks): all_tracks_complete, any_track_complete, main_track_complete
- ASCII state diagrams з усіма transitions
- Mandatory phase timeout enforcement
- Global transitions evaluation order (each tick, sorted by priority, before phase-local)
- INVALID_TRANSITION semantics

## Reference

- Plan Q6 для current spec.
