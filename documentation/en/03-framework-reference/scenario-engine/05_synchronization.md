# 05 — Cross-Track Synchronization (Tick-Order Semantics)

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/05_synchronization.md](../../../uk/03-framework-reference/scenario-engine/05_synchronization.md)

The engine's contract for how parallel tracks share state. The choice is fixed
in ADR-0003: **tick-order, no snapshot.**

## Primary contract

1. The engine ticks instances (handles) in declaration order (handle 1, 2, ...).
2. Within an instance, tracks tick in declaration order (track 0, 1, ...).
3. Each `SharedState::get()` reads live (no snapshot, no buffering).
4. Track A's `set()` is visible to track B's `get()` if track B ticks
   LATER in the same tick (B has a higher track index).
5. All track writes from tick N are visible to all tracks during tick N+1.

## Worked example: producer → consumer

Recipe with track 0 ("main") writing a signal AND track 1 ("watcher")
reading it:

```jsonc
"tracks": [
  { "name": "main",                          // declared first → ticks first
    "phases": [{
      "name": "signal_phase",
      "entry": [
        {"action": "set_state",
         "params": {"key": "test.flag", "type": "bool", "value": true}}
      ],
      "transitions": [{"to": "$complete"}]
    }]
  },
  { "name": "watcher",                       // declared second → ticks second
    "phases": [{
      "name": "wait",
      "transitions": [
        {"to": "$complete",
         "when": {"state_key_eq": {"key": "test.flag", "value": true}}}
      ]
    }]
  }
]
```

Trace:

```
Tick 1 (10 ms):
  Track 0 (main) tick:
    - phase "signal_phase" entry actions:
      - invoke_action("set_state") → ctx.state->set("test.flag", true)
                                       ↑
                                       SharedState immediately updated;
                                       version bump; delta tracked

    - entry actions complete; evaluate transition {"to": "$complete"}
      → unconditional kind, fires immediately
      → latch target = MODR_TARGET_COMPLETE; running_exit_actions = true

  Track 1 (watcher) tick:
    - phase "wait" — no entry actions
    - evaluate transition {"to": "$complete", "when": {state_key_eq...}}
      → call evaluate_condition for state_key_eq{test.flag, true}
      → ctx.state->get("test.flag") returns true (← from track 0's write THIS tick)
      → condition holds → transition fires
      → track 1 → COMPLETED

  Completion check: completion_rule == all_tracks_complete
    → main is ABORTING (running exits), watcher is COMPLETED → not all yet

Tick 2 (20 ms):
  Track 0:
    - exit actions complete (none in this phase)
    - apply latched transition → state = COMPLETED

  Track 1:
    - state = COMPLETED → early return

  Completion check: all_tracks_complete satisfied → scenario COMPLETED.
```

Watcher transitioned ON THE SAME TICK that main wrote — this is the tick-order
benefit. Producer-before-consumer declaration order is required for this
guarantee.

## Worked example: incorrect declaration order (consumer first)

Same recipe with the tracks reordered:

```jsonc
"tracks": [
  { "name": "watcher", ... },
  { "name": "main",    ... }
]
```

Trace:

```
Tick 1 (10 ms):
  Track 0 (watcher) tick:
    - evaluate transition state_key_eq(test.flag, true)
      → ctx.state->get("test.flag") returns nullopt (not set yet)
      → condition false → no transition

  Track 1 (main) tick:
    - entry: set_state("test.flag", true) — write happens NOW
    - latches transition

Tick 2 (20 ms):
  Track 0 (watcher) tick:
    - evaluate transition state_key_eq → reads true → fires
    - watcher → COMPLETED

  Track 1 (main) tick:
    - exit actions (none) → apply latched COMPLETE
```

Same end state but watcher took 1 extra tick to converge. Not a bug — just
1-tick latency. Recipe authors may ignore this if 10ms latency is
acceptable; for tighter coupling, fix the declaration order.

## Race condition pattern (avoid)

Two tracks writing the same key based on shared state — last writer wins
within a tick. Avoid:

```jsonc
"tracks": [
  { "name": "alpha",
    "phases": [{ "entry": [
      {"action": "set_state",
       "params": {"key": "shared.counter", "type": "i32", "value": 1}}
    ], ... }]
  },
  { "name": "beta",
    "phases": [{ "entry": [
      {"action": "set_state",
       "params": {"key": "shared.counter", "type": "i32", "value": 2}}
    ], ... }]
  }
]
```

Result: `shared.counter` is always == 2 (beta ticks last, overwrites alpha).
Probably not what the author intended. Solution: use distinct keys per track.

## Multi-tick fan-out (deterministic)

Track 0 → Track 1 → Track 2 chain. Author declares 0, 1, 2:

| Tick | Track 0 | Track 1 | Track 2 |
|------|---------|---------|---------|
| 1 | writes A | reads A → fires → writes B | reads B → fires → writes C |
| 2 | (idle)  | (idle) | (idle) — all complete from tick 1 |

All three transitions cascade in one tick because each track ticks AFTER
its producer in the same engine tick.

## Edge case: track reads its own write

Track 0 invokes `set_state` (writes key X), THEN evaluates a condition that
reads X within the same phase. Trace:

```
Track 0 tick N:
  phase entry actions:
    - invoke_action set_state → write X = true
    - return ActionStatus::OK
    - ++entry_action_progress
    - return (one action per tick)

Track 0 tick N+1:
  phase entry actions:
    - all entry actions done (from tick N)
    - evaluate transitions
    - state_key_eq{X, true} → reads X = true → fires
```

A track sees its own write on the NEXT tick, not the same tick. This is a
consequence of the one-action-per-tick policy: phase transitions evaluate AFTER
all entry actions finished, which spans ≥ entry_action_n ticks.

For instant self-sync, use:
- A composite condition inside the entry action's response logic
- Or, simply: structure the phase so entry sets state AND transitions check
  a separate condition (not the just-set key)

## Anti-patterns

### 1. Circular wait

Track A waits for track B's signal; track B waits for track A's signal.
Both tracks stuck forever. The engine doesn't detect this; `phase_timeout_ms`
serves as a safety fallback (eventually one or both phases time out → FAILED).

Resolution: refactor the recipe so that at least one track has a time-based
or self-sufficient transition.

### 2. Instant visibility in same-tick write-then-read

```jsonc
"entry": [
  {"action": "set_state", "params": {"key": "x", "type": "bool", "value": true}}
],
"transitions": [
  {"to": "next", "when": {"state_key_eq": {"key": "x", "value": true}}}
]
```

The author expects the transition to fire immediately in the same tick. It does NOT —
the entry action runs on tick N, the transition evaluates on tick N+1 (or later, if
multiple entry actions). Use an unconditional transition (`{"to": "next"}`)
if immediate advancement is intended.

### 3. Cross-instance reads with handle race

Recipe instance 1 and instance 2 of the SAME recipe both write to
`<recipe>.foo` (the same mirror key, since both have the same recipe name).
Last writer wins — instance 2's tick happens after instance 1's, so
instance 1's writes are clobbered immediately.

Resolution: don't load multiple instances of recipes that share output
keys. Or make output keys instance-scoped (Stage 1.5 — no current API
for this).

## See also

- [adr/0003-tick-order-sync-semantics.md](adr/0003-tick-order-sync-semantics.md)
  — design rationale (why tick-order, not snapshot)
- [usage/examples/02_dual_track_sync.md](usage/examples/02_dual_track_sync.md)
  — runnable cross-track sync example
- Source test: `components/modesp_scenario/tests/host/test_track_synchronization.cpp`
- [04_state_machines.md](04_state_machines.md) — track tick algorithm
