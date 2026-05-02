# 03 — C++ API Reference

**Status:** placeholder. Заповнюється incrementally на steps 5 (ActionRegistry), 6 (ContinuousRegistry), 14 (SequenceEngine).

## Заповнюється

- `SequenceEngine` public methods (load, start, pause, resume, abort, unload, introspection)
- Constants: `MAX_SEQUENCES`, `MAX_TRACKS_PER_SCENARIO`, `MAX_TOTAL_TRACKS`, `MAX_RECIPE_NAME_LEN`, `MAX_TRACK_NAME_LEN`
- Types: `SequenceHandle`, `TrackIdx`, `EngineError`, `ActionStatus`, `State`
- `ActionRegistry::instance()` API
- `ContinuousRegistry::instance()` API
- `ParamOverride` struct і use patterns
- Error codes detailed table

## Reference

- See plan Q2 (API), Q3 (state key namespace), Q4 (ActionRegistry signature), Q5 (ContinuousBehavior), Q12 (action failure policy).
