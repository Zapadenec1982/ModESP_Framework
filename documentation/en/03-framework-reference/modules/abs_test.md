# `abs_test` — reference scenario recipe

> 📖 **Українською:** [documentation/uk/03-framework-reference/modules/abs_test.md](../../../uk/03-framework-reference/modules/abs_test.md)

`abs_test` is the framework's **reference scenario recipe**. It exercises
nearly every scenario engine feature у one fixture: two parallel tracks,
time-based і conditional transitions, the `all_of` composite, cross-track
synchronisation through mirror keys, `$complete` terminator, і
`all_tracks_complete` completion rule. Used as the HIL parity test, the
golden file for `compile_scenario.py`, і the first recipe you should
read after `02-module-author-guide/recipe-authoring.md`.

The recipe deliberately drives **abstract test signals** (`test.output_a`,
`test.output_b`, `test.counter`, `test.input_a`) instead of real hardware,
so it runs unmodified on any board.

REQUIRES: `modesp_scenario`. Recipe name **8 chars** to fit the 32-char
mirror key budget (recipe ≤ 12, track ≤ 8 — see `recipe-authoring.md`).

## Topology

```
abs_test (scenario)
├── track "main"  (main_track flag — drives mirror keys, NVS-persisted)
│   ├── phase_a → phase_b (after 1 s)
│   ├── phase_b → phase_c (when test.input_a > 10 AND 500 ms elapsed, OR after 5 s)
│   └── phase_c → $complete (after 500 ms)
└── track "watcher"
    └── watching → $complete (when abs_test.main_phase_name == "phase_c")
```

`completion_rule: all_tracks_complete` — scenario terminates only after
**both** tracks reach `$complete`. Total runtime is bounded by
`scenario_timeout_max_ms = 120 000 ms` (2 minutes); typical run completes
у under 7 seconds.

## What it exercises

| Feature | Where |
|---|---|
| Multiple parallel tracks | `tracks: [main, watcher]` |
| `main_track` flag | track.flags drives mirror `abs_test.main_*` keys |
| Entry actions | `log`, `set_state` у each phase |
| Time-based transition | `time_elapsed_ms` predicate |
| Composite condition | `all_of: [state_key_gt, time_elapsed_ms]` |
| Multiple transitions per phase | phase_b has 2 (race wins) |
| Cross-track sync | watcher reads `abs_test.main_phase_name` mirror |
| `$complete` terminator | both tracks end на `$complete` |
| `all_tracks_complete` rule | scenario ends when both tracks done |
| Per-phase і per-scenario timeouts | `timeout_ms`, `scenario_timeout_max_ms` |

## Mirror keys (the contract під 32-char budget)

Recipe `main_track` automatically publishes mirror keys named
`<recipe>.<key>`:

| Key | Source |
|---|---|
| `abs_test.scenario_state` | engine — `running`/`paused`/`completed`/`failed` |
| `abs_test.scenario_elapsed_s` | engine — seconds since `start()` |
| `abs_test.last_error` | engine — last `EngineError` numeric code |
| `abs_test.main_state` | main track — `running`/`completed`/... |
| `abs_test.main_phase_name` | main track — current phase name |
| `abs_test.main_phase_idx` | main track — current phase index |
| `abs_test.main_elapsed_s` | main track — seconds у current phase |
| `abs_test.watcher_state` | watcher track — same shape |
| `abs_test.watcher_phase_name` | watcher track |
| `abs_test.watcher_phase_idx` | watcher track |
| `abs_test.watcher_elapsed_s` | watcher track |

Longest key: `abs_test.scenario_elapsed_s` = 27 chars — within the 32-byte
SharedState limit. This is why the recipe name was deliberately chosen
to be 8 chars.

## Test signals (driven by entry actions)

| Key | Phase | Direction |
|---|---|---|
| `test.output_a` | phase_a → true, phase_b → false | written |
| `test.output_b` | phase_b → true, phase_c → false | written |
| `test.counter` | phase_b → 1 | written |
| `test.input_a` | phase_b transition guard | read |

To force the `state_key_gt` branch, write `test.input_a > 10` через
`/api/settings` during phase_b. Otherwise the 5 s fallback transition
wins.

## HIL pytest coverage

`tools/tests/test_hil_scenario.py` runs against а real ESP32 і drives
this recipe through:

1. **Single-instance load + run** — verifies all 6 phases і final state.
2. **Multi-instance** — load `abs_test` twice; instances stay independent.
3. **Resource contention** — driving conflicting outputs, expects
   second-loaded instance to be denied.
4. **Global transition** — inject а fault key; main track jumps to
   `$fail`.
5. **Power-cycle recovery** — `start()`, hard reboot, `try_recover()`
   resumes from `PAUSED`.
6. **WebUI mirror updates** — phase_name visible у real time.

All 6 tests must pass for the engine rebuild to be considered green.

## Compiling

```
python tools/compile_scenario.py \
    --manifest modules/abs_test/manifest.json \
    --out data/scenarios/abs_test.modr
```

Compiled `.modr` ships у `data/scenarios/` і is flashed onto the LittleFS
data partition. Use `python tools/dump_modr.py data/scenarios/abs_test.modr`
to inspect the binary token stream.

## UI surface

Manifest declares one card "Abstract test scenario" із
`visible_when` gating — appears only when the scenario is loaded і
у one of `running`/`paused`/`completed`/`failed`. Card shows scenario
state, elapsed seconds, і live phase names from both tracks.

Page **"Тест"** — Ukrainian default because the recipe is а test
fixture; rename via `ui.page` if you fork it.

## Why це а good reference

- **Compact** — 109 lines of manifest cover ~10 distinct engine features.
- **Hardware-free** — runs identically on every board, no driver setup.
- **Multi-track** — only fixture that exercises track-order tick
  semantics і cross-track sync.
- **Composite transitions** — only fixture із `all_of` + multiple
  transitions per phase.
- **HIL-anchored** — any change you make to the engine must keep this
  recipe green.

Fork it to prototype your own recipes:

```
cp -r modules/abs_test modules/my_recipe
sed -i 's/abs_test/my_recipe/g' modules/my_recipe/manifest.json
```

(Watch the 12-char recipe name budget — see `recipe-authoring.md`.)

## Next steps

- **[02-module-author-guide/recipe-authoring.md](../../02-module-author-guide/recipe-authoring.md)** —
  full recipe grammar reference.
- **[02-module-author-guide/recipe-actions.md](../../02-module-author-guide/recipe-actions.md)** —
  catalog of built-in actions і conditions used here.
- **[components/modesp_scenario.md](../components/modesp_scenario.md)** —
  high-level engine overview.

## Source

- [`modules/abs_test/manifest.json`](../../../../modules/abs_test/manifest.json)
- [`data/scenarios/abs_test.modr`](../../../../data/scenarios/abs_test.modr)
  (compiled artifact).
- [`tools/tests/test_hil_scenario.py`](../../../../tools/tests/test_hil_scenario.py)
  — HIL parity suite.
