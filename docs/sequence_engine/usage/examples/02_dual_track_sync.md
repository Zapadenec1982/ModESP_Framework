# Example 02: Dual-Track Cross-Track Synchronization

**Status:** placeholder. Заповнюється у Step 17 (post-integration).

## Заповнюється

Mirrors extended `recipe_abstract_test` з 2 tracks (`main` + `watcher`):
- Track "main": phase_a → phase_b → phase_c → $complete (3-phase progression)
- Track "watcher": single phase з transition that triggers when main reaches phase_c
- Cross-track sync via `state_key_eq` reading mirror key `recipe_abstract_test.main_phase_name`
- `completion_rule: all_tracks_complete`
- Demonstrates tick-order semantics:
  - Watcher (track 1) ticks AFTER main (track 0) у same tick
  - Main's mirror key write to `main_phase_name` visible до watcher's read у same tick (declaration order)
- Global transition (safety abort): write `test.fault=true` → both tracks → TRACK_FAILED
- Verified by HIL test (Step 16)
- Compilable JSON validated by `tools/tests/test_compile_scenario.py`
