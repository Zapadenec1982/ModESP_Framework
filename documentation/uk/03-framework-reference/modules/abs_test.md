# `abs_test` — reference сценарний recipe

> 📖 **In English:** [documentation/en/03-framework-reference/modules/abs_test.md](../../../en/03-framework-reference/modules/abs_test.md)

`abs_test` — **reference scenario recipe** фреймворку. Він exercises
майже кожну feature scenario engine у одному fixture: два parallel tracks,
time-based і conditional transitions, `all_of` composite, cross-track
synchronisation через mirror keys, `$complete` terminator, і
`all_tracks_complete` completion rule. Використовується як HIL parity test,
golden file для `compile_scenario.py`, і перший recipe який ви повинні
прочитати після `02-module-author-guide/recipe-authoring.md`.

Recipe deliberately drives **abstract test signals** (`test.output_a`,
`test.output_b`, `test.counter`, `test.input_a`) instead of real hardware,
тому running unmodified на будь-якій board.

REQUIRES: `modesp_scenario`. Recipe name **8 chars** щоб fit 32-char
mirror key budget (recipe ≤ 12, track ≤ 8 — див. `recipe-authoring.md`).

## Topology

```
abs_test (scenario)
├── track "main"  (main_track flag — drives mirror keys, NVS-persisted)
│   ├── phase_a → phase_b (через 1 с)
│   ├── phase_b → phase_c (коли test.input_a > 10 AND elapsed 500 ms, АБО через 5 с)
│   └── phase_c → $complete (через 500 ms)
└── track "watcher"
    └── watching → $complete (коли abs_test.main_phase_name == "phase_c")
```

`completion_rule: all_tracks_complete` — scenario terminates тільки після
коли **обидва** tracks reach `$complete`. Total runtime bounded by
`scenario_timeout_max_ms = 120 000 ms` (2 minutes); typical run completes
за менш ніж 7 seconds.

## Що це exercises

| Feature | Де |
|---|---|
| Multiple parallel tracks | `tracks: [main, watcher]` |
| `main_track` flag | track.flags drives mirror `abs_test.main_*` keys |
| Entry actions | `log`, `set_state` у кожній phase |
| Time-based transition | `time_elapsed_ms` predicate |
| Composite condition | `all_of: [state_key_gt, time_elapsed_ms]` |
| Multiple transitions per phase | phase_b має 2 (race wins) |
| Cross-track sync | watcher reads `abs_test.main_phase_name` mirror |
| `$complete` terminator | обидва tracks end на `$complete` |
| `all_tracks_complete` rule | scenario ends when both tracks done |
| Per-phase і per-scenario timeouts | `timeout_ms`, `scenario_timeout_max_ms` |

## Mirror keys (контракт під 32-char budget)

Recipe `main_track` автоматично publishes mirror keys named
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

Longest key: `abs_test.scenario_elapsed_s` = 27 chars — у межах 32-byte
SharedState limit. Це і є чому recipe name deliberately chosen 8 chars.

## Test signals (driven by entry actions)

| Key | Phase | Direction |
|---|---|---|
| `test.output_a` | phase_a → true, phase_b → false | written |
| `test.output_b` | phase_b → true, phase_c → false | written |
| `test.counter` | phase_b → 1 | written |
| `test.input_a` | phase_b transition guard | read |

Щоб force `state_key_gt` branch, write `test.input_a > 10` через
`/api/settings` під час phase_b. Інакше 5 s fallback transition
wins.

## HIL pytest coverage

`tools/tests/test_hil_scenario.py` runs проти real ESP32 і drives
це recipe through:

1. **Single-instance load + run** — verifies all 6 phases і final state.
2. **Multi-instance** — load `abs_test` twice; instances stay independent.
3. **Resource contention** — driving conflicting outputs, expects
   second-loaded instance бути denied.
4. **Global transition** — inject fault key; main track jumps до
   `$fail`.
5. **Power-cycle recovery** — `start()`, hard reboot, `try_recover()`
   resumes з `PAUSED`.
6. **WebUI mirror updates** — phase_name visible у real time.

Всі 6 tests must pass щоб engine rebuild вважався green.

## Compiling

```
python tools/compile_scenario.py \
    --manifest modules/abs_test/manifest.json \
    --out data/scenarios/abs_test.modr
```

Compiled `.modr` ships у `data/scenarios/` і is flashed на LittleFS
data partition. Use `python tools/dump_modr.py data/scenarios/abs_test.modr`
щоб inspect binary token stream.

## UI surface

Manifest declares one card "Abstract test scenario" із
`visible_when` gating — appears тільки коли scenario is loaded і
у одному з `running`/`paused`/`completed`/`failed`. Card shows scenario
state, elapsed seconds, і live phase names from both tracks.

Page **"Тест"** — Ukrainian default because recipe is а test
fixture; rename via `ui.page` якщо fork it.

## Чому це good reference

- **Compact** — 109 lines manifest cover ~10 distinct engine features.
- **Hardware-free** — runs identically на кожній board, no driver setup.
- **Multi-track** — єдиний fixture що exercises track-order tick
  semantics і cross-track sync.
- **Composite transitions** — єдиний fixture з `all_of` + multiple
  transitions per phase.
- **HIL-anchored** — будь-який change у engine must keep це recipe green.

Fork its щоб prototype власні recipes:

```
cp -r modules/abs_test modules/my_recipe
sed -i 's/abs_test/my_recipe/g' modules/my_recipe/manifest.json
```

(Watch 12-char recipe name budget — див. `recipe-authoring.md`.)

## Що далі

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
