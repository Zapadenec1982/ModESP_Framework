# Example 02: Dual-Track Cross-Track Synchronization

Two parallel tracks coordinating через SharedState. Demonstrates ADR-0003
tick-order semantics: track 0's writes within а tick are visible to track 1's
reads same tick (declaration order matters).

## Recipe (`modules/abs_test/manifest.json`)

The Stage 1 reference recipe lives at
[modules/abs_test/manifest.json](../../../../modules/abs_test/manifest.json).
Below — the relevant fragments (full file: ~100 lines).

### Track 0: "main" — writer

```jsonc
{
  "name": "main",
  "flags": ["main_track"],
  "phases": [
    {
      "name": "phase_a",
      "timeout_ms": 10000,
      "entry": [
        {"action": "log",       "params": {"msg": "main: phase_a"}},
        {"action": "set_state", "params": {"key": "test.output_a", "type": "bool", "value": true}}
      ],
      "transitions": [{"to": "phase_b", "when": {"time_elapsed_ms": 1000}}]
    },
    {
      "name": "phase_b",
      "timeout_ms": 30000,
      "entry": [
        {"action": "set_state", "params": {"key": "test.output_a", "type": "bool", "value": false}},
        {"action": "set_state", "params": {"key": "test.output_b", "type": "bool", "value": true}}
      ],
      "transitions": [
        // Either input drives transition fast (з all_of), або 5s timer fallback
        {"to": "phase_c", "when": {"all_of": [
          {"state_key_gt": {"key": "test.input_a", "value": 10}},
          {"time_elapsed_ms": 500}
        ]}},
        {"to": "phase_c", "when": {"time_elapsed_ms": 5000}}
      ]
    },
    {
      "name": "phase_c",
      "timeout_ms": 5000,
      "entry": [
        {"action": "log", "params": {"msg": "main: completing"}},
        {"action": "set_state", "params": {"key": "test.output_b", "type": "bool", "value": false}}
      ],
      "transitions": [{"to": "$complete", "when": {"time_elapsed_ms": 500}}]
    }
  ]
}
```

### Track 1: "watcher" — reader

```jsonc
{
  "name": "watcher",
  "phases": [
    {
      "name": "watching",
      "timeout_ms": 60000,
      "entry": [{"action": "log", "params": {"msg": "watcher: started"}}],
      "transitions": [
        {"to": "$complete", "when": {"state_key_eq": {
          "key": "abs_test.main_phase_name", "value": "phase_c"
        }}}
      ]
    }
  ]
}
```

The watcher reads engine-written mirror key `abs_test.main_phase_name` —
NOT а user-written key. Engine updates mirror keys на phase advance,
making them publish-subscribe channels для track coordination.

## Tick-order semantics (ADR-0003)

Engine ticks tracks у declaration order:

```
tick N:
  ┌─ Track 0 (main) tick     — phase_a entry actions, eventually transitions to phase_b
  └─ Track 1 (watcher) tick  — reads main_phase_name (sees "phase_b" — main's update)
```

Within а tick, track 1's read happens AFTER track 0's write. Engine reads
SharedState fresh кожен time, no snapshot. Це matches developer intuition:
"наступний read after write sees the new value".

**Implication:** declare tracks у producer-then-consumer order. Якщо watcher
були declared first, it would read STALE main_phase_name on tick N (before
main updated it) і wait one tick для convergence — usually harmless but
adds latency.

## Verification

Cross-track sync verified end-to-end via host test:

```bash
python -m pytest tools/tests/test_sequence_host.py::test_track_synchronization_host
```

The test loads а minimal 2-track variant (sync_two_tracks.modr) і drives
`instance_tick` to completion: track 0 writes `test.signal=true`, track 1's
`state_key_eq` condition fires, scenario reaches COMPLETED. Bound: 200 ticks
(2 sec @ 10 ms tick).

## Adding а safety abort (global transition)

To force-abort scenario regardless of which phase tracks are у, use а
global transition:

```jsonc
"scenario": {
  "global_transitions": [
    {"when": {"state_key_eq": {"key": "safety.fault", "value": true}},
     "priority": 255, "scope": "abort_scenario"}
  ],
  // ... tracks ...
}
```

Global transitions evaluate FIRST each tick, у priority order (descending).
First match aborts scenario (всі tracks → FAILED → scenario FAILED).
`scope: abort_main_track` instead aborts only the main_track-flagged track,
allowing watchers etc. to continue.

## Common patterns

### Watchdog track

A monitor track що runs у parallel з main, fails on safety violation:

```jsonc
{
  "name": "wdog",
  "phases": [{
    "name": "monitoring",
    "timeout_ms": 0,
    "transitions": [
      {"to": "$abort", "when": {"state_key_gt": {"key": "sensor.temp", "value": 100}}}
    ]
  }]
}
```

З `completion_rule: main_track_complete`, watchdog's $abort triggers
scenario fail; main's $complete triggers scenario success.

### Producer-consumer pipeline

Track 0 produces а work item count; Track 1 consumes by waiting on count:

```jsonc
// Track 0
{"action": "set_state", "params": {"key": "pipeline.count", "type": "i32", "value": 5}}

// Track 1
{"to": "next", "when": {"state_key_ge": {"key": "pipeline.count", "value": 5}}}
```

Tick-order semantics ensure track 1 sees the count immediately on the same
tick що track 0 writes it.

## See also

- [01_minimal_3phase.md](01_minimal_3phase.md) — single-track basics
- [05_synchronization.md](../../05_synchronization.md) — full tick-order specification + edge cases
- [ADR-0003](../../adr/0003-tick-order-sync-semantics.md) — design rationale
