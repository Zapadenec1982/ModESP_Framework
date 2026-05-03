# 04 — State Machines

The engine has two layered state machines: **per-scenario** (instance-level)
і **per-track** (within а scenario). Both are implemented у `sequence_track.cpp`
і `sequence_instance.cpp`.

## Scenario state machine

`SequenceRuntime::State` (defined у `sequence_state.h`):

```
                                  ┌──────────────────────────┐
                                  │                          │
                                  │     pause()              │
                                  │     ◀──────              │
                                  │                          │
              load_buffer/        │                          │
              load_path           │                          │
   ┌───────┐  ──────────▶ ┌───────┴────┐  start()  ┌─────────▼──┐
   │ IDLE  │              │   LOADED   │ ────────▶ │  RUNNING   │
   └───────┘              └────────────┘            └────┬───────┘
       ▲                        │                       │
       │                        │ unload()              │ resume()
       │                        ▼                       │
       │                  ┌─────────────┐               │
       │                  │   IDLE      │               │
       │                  └─────────────┘               │
       │                                                │
       │ unload()                                       │
       ├─────────────────────────────────────┐          │
       │                                     │          ▼
   ┌───┴────────┐  abort() / global trans     │   ┌──────────┐
   │ COMPLETED  │     ◀────────              ┌─┤  ABORTING  │
   └────────────┘                            │ └──────────┘
                                             │
                                             │ all tracks terminal
                                             ▼
                                       ┌─────────┐
                                       │ FAILED  │
                                       └─────────┘
                                             │
                                             │ unload()
                                             ▼
                                          IDLE
```

### Transition table

| From | Event | To | Side effects |
|------|-------|-----|--------------|
| IDLE | `load_buffer` / `load_path` succeeds | LOADED | Buffer copied to slot, `modr_validate` passes |
| LOADED | `start()` succeeds | RUNNING | Scenario-scope resources acquired атомарно; tracks set to RUNNING from `initial_phase` |
| LOADED | `start()` resource conflict | LOADED | No state change; returns `RESOURCE_CONTENDED` |
| RUNNING | `pause()` | PAUSED | `instance_tick` no-ops для це slot |
| PAUSED | `resume()` | RUNNING | Tracks resume from same `phase_idx` + `phase_elapsed_ms` |
| RUNNING | `abort()` / global transition | ABORTING | Tracks forced to FAILED; phase-scope resources released per-track |
| RUNNING | `completion_rule` satisfied | COMPLETED | Scenario-scope resources released |
| RUNNING | Main track FAILED (з flag) | FAILED | Scenario-scope resources released |
| ABORTING | All tracks terminal | FAILED | Scenario-scope resources released |
| Any non-IDLE | `unload()` | IDLE | All resources released, slot cleared |

### `completion_rule` evaluation

Set у recipe header via `scenario.completion_rule`:

| Value | Trigger condition |
|-------|------------------|
| `all_tracks_complete` (0) | Every track у COMPLETED state |
| `any_track_complete` (1) | At least one track у COMPLETED state |
| `main_track_complete` (2) | The track із `MODR_TRACK_FLAG_MAIN` is COMPLETED |

Engine evaluates после every `instance_tick`. Якщо main track is FAILED,
scenario → FAILED regardless of completion_rule (safety property —
main track failure is always terminal для scenario).

## Per-track state machine

`TrackRuntime::State`:

```
   ┌───────┐  instance_start()
   │ IDLE  │ ──────────────▶ ┌─────────────┐
   └───────┘                 │   RUNNING   │ ◀────────┐
                             └──────┬──────┘          │
                                    │                 │
                          phase_resource_n > 0         │ acquire success
                          AND try_acquire fails        │
                                    │                 │
                                    ▼                 │
                         ┌──────────────────────┐     │
                         │ WAITING_FOR_RESOURCE │ ────┘
                         └─────────┬────────────┘
                                    │
                                    │ phase timeout
                                    ▼
                                ┌─────────┐
                                │ FAILED  │
                                └─────────┘

   RUNNING ──────────────▶ ┌──────────────┐
   (per-phase $abort         │   ABORTING   │
    transition fires)         └──────┬───────┘
                                     │
                                     │ all exit actions complete
                                     ▼
                                 ┌─────────┐
                                 │ FAILED  │
                                 └─────────┘

   RUNNING ───────────────────▶ ┌────────────┐
   (transition target == COMPLETE) │ COMPLETED  │
                                  └────────────┘

   RUNNING ───────────────────▶ FAILED
   (action returns FAILED_ABORT)

   Any state ─────────────────▶ FAILED
   (scenario-level abort: instance_abort sets directly to FAILED, releasing
    phase-scope resources first)
```

### Per-tick algorithm (track_tick)

Pseudocode матча `sequence_track.cpp::track_tick`:

```
if state ∈ {IDLE, COMPLETED, FAILED}: return  # terminal або dormant

phase_elapsed_ms += dt_ms (saturating)

if state == WAITING_FOR_RESOURCE:
    if phase has phase_resources:
        if try_acquire_phase(...):
            state = RUNNING                # fall through
        else:
            if phase_elapsed_ms >= timeout: state = FAILED
            return
    else:
        state = RUNNING

# Now state ∈ {RUNNING, ABORTING}
if running_exit_actions:
    if exit_action_progress < phase.exit_action_n:
        invoke exit action
        return  # one action per tick
    apply latched transition (advance phase / complete / abort)
    return

# Run entry actions one per tick
if entry_action_progress < phase.entry_action_n:
    s = invoke entry action
    handle (s) per Q12 policy (OK advance, PENDING retry, FAILED abort/recover)
    return  # one action per tick

# All entry actions done — evaluate transitions
for trans у phase.transitions:
    if transition_fires(trans):
        latch trans.target_phase, set running_exit_actions = true
        return

# No transition fired — check phase timeout
if timeout != 0 AND phase_elapsed_ms >= timeout:
    state = FAILED
    release phase-scope resources
```

### Action progression — one per tick

Both entry і exit actions process at most ONE per engine tick. With 10ms
tick period (default), а phase із 5 entry actions takes 5 ticks (50ms)
to begin transition evaluation. Це bounds per-tick CPU cost predictably.

`PENDING` action keeps track parked у same action across ticks (e.g.
DS18B20 conversion ~750ms = 75 PENDING ticks). Long PENDING actions
should be replaced із explicit `wait_ms` time-based transition followed
by а quick action.

## Cross-track sync semantics

Engine ticks tracks within а scenario у DECLARATION ORDER. Within а tick:

```
tick N:
  ┌─ Scenario instance N (handle 1) tick:
  │  ├─ Global transitions evaluated (priority order)
  │  ├─ Track 0 tick (writes to SharedState visible to track 1)
  │  ├─ Track 1 tick (reads SharedState fresh — sees track 0's writes)
  │  └─ Completion rule checked
  │
  ├─ Scenario instance N+1 (handle 2) tick: ...
  └─ ...
```

Reads are **live** (no snapshot). Recipe authors must declare producer
tracks before consumer tracks для deterministic same-tick visibility.
See [05_synchronization.md](05_synchronization.md) для worked examples.

## Global transitions

Evaluated FIRST each tick, before per-track ticks. Sorted by priority
(descending — higher priority fires first). On match:

| Scope | Effect |
|-------|--------|
| `abort_scenario` (0) | `instance_abort` — all non-terminal tracks → FAILED, phase resources released |
| `abort_main_track` (1) | Only main_track-flagged track → FAILED + phase release |

Global transitions don't have time thresholds — only condition expressions.
Unconditional global transitions (`kind == UNCONDITIONAL`) fire on first
tick, useful для one-shot abort triggers controlled by SharedState flags.

## Cross-references

- [05_synchronization.md](05_synchronization.md) — full tick-order semantics
- [06_resource_arbitration.md](06_resource_arbitration.md) — when WAITING_FOR_RESOURCE entered/exited
- [10_error_model.md](10_error_model.md#action-failure-policy-machine) — action failure policy details
- [adr/0003-tick-order-sync-semantics.md](adr/0003-tick-order-sync-semantics.md) — design rationale
- [adr/0007-mandatory-phase-timeouts.md](adr/0007-mandatory-phase-timeouts.md) — why timeouts are required
- Source: `components/modesp_sequence/src/sequence_track.cpp`,
  `sequence_instance.cpp`
