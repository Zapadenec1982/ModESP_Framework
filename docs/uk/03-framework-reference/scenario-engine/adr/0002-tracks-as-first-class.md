# ADR-0002: Tracks as First-Class Concept (NOT Retrofit)

**Status:** Accepted
**Date:** 2026-05-02

## Context

Sequence Engine виконує phased timeline scenarios. Initial discussion considered single-track scenarios (одна послідовність phases) як baseline, з multi-track support potentially added later.

Real-world embedded automation **завжди multi-channel** — приклад мультиварочного recipe:
- Track 1 (heat control): warmup → simmer → finish
- Track 2 (stir schedule): active 10min → idle until heat complete
- Track 3 (UI notifications): notify_at_minute_20 → notify_done
- Track 4 (safety monitor): continuous fault detection

Parallel tracks share scenario timeline але мають independent phase progression. Без first-class track support, recipe authors забезпечують parallelism manually через cross-module hacks.

User analogy (MIDI / punch card) — точна метафора: scenario = song з multiple tracks playing simultaneously, синхронізовані по shared time line.

## Decision

**Tracks — first-class concept у scenario engine з Stage 1.**

Specifically:
- `.modr` header has `track_count` field (1..6, MAX_TRACKS_PER_SCENARIO)
- Each track has own phase array, transitions, current state
- Single tick task iterates instances × tracks; per-track state machines run independently
- `completion_rule` (`all_tracks_complete` | `any_track_complete` | `main_track_complete`) defines scenario completion
- Single-track recipe = `tracks: [{name: "main", phases: [...]}]` — degenerate case of multi-track
- Cross-track synchronization via SharedState (publish/subscribe pattern, existing ModESP infrastructure)

**NOT defer to Stage 1.5** because retrofit cost is high — track concept changes binary format, API, state machine structure, NVS layout. Building tracks-aware from day 1 is significantly cheaper.

## Alternatives considered

### A. Single-track MVP, multi-track Stage 1.5 — rejected

**Pros:**
- Smaller MVP (~30% less code)
- Simpler reasoning early

**Cons (why rejected):**
- Binary format breaking change required to add tracks
- API breaking change (`engine.track_state(handle)` → `engine.track_state(handle, track)`)
- NVS layout change (per-track sub-state not present у single-track tokens)
- State machine restructuring
- Recipe authors authoring multi-process logic via single-track + state-machine-in-state-keys workaround у meantime → tech debt
- User explicitly asked for tracks "from very beginning"

### B. HSM (Hierarchical State Machines) instead of flat tracks — rejected

**Pros:**
- Industry-standard formalism (Samek QP, BOOST.MSM, ETL state_chart)
- Orthogonal regions support concurrency formally

**Cons (why rejected):**
- Overkill для mostly-linear recipes з occasional branching
- HSM hierarchy implementation overhead (~3x code)
- Recipe author cognitive load (state regions, transition lookup precedence)
- IEC 61131-3 SFC і ISA-88 — both flat phase lists з parallel branches, не HSM
- BehaviorTree.CPP also rejected HSM in favor of flat composable trees

### C. Behavior trees instead of phase lists — rejected

**Pros:**
- More composable (subtrees reusable)
- ROS 2 Nav2 production validates approach

**Cons (why rejected):**
- Recipe authors are not robotics engineers; phase list is more accessible mental model
- Tree authoring без visual editor (which is poza engine scope) — painful у JSON
- Multi-track parallel = SFC parallel branches, simpler

## Consequences

### Positive
- Real-world recipes naturally express multi-process automation (multicooker, fermenter, lab batch)
- Cross-track sync via SharedState reuses existing infrastructure (no new IPC)
- API consistent з MIDI/sequencer mental model
- ISA-88 / SFC alignment maintained
- Punch-card analogy користувача supported elegantly

### Negative
- Larger MVP scope (~3000 LOC vs ~2200)
- Cross-track sync semantics requires careful documentation (ADR-0003)
- More state to track у engine RAM і NVS
- Per-track UI mirror keys deferred до Stage 1.5 (capacity constraint)

### Neutral
- Fixed-capacity arrays (`MAX_TRACKS_PER_SCENARIO = 6`) define hard upper bound — Kconfig'd if needed

## References

- Foundation document вже згадує tracks (Розділ 4.5 phase decomposition + concurrent behaviors)
- Industry: ROS Smach Concurrence container, BehaviorTree.CPP `Parallel` node, MIDI sequencer model
- IEC 61131-3 SFC parallel branches
- Plan Q1 (binary format track table), Q2 (API per-track methods)

## Revisit triggers

Reconsider if:
- All real-world recipes turn out to be single-track → accept overhead but document tracks as Stage 2 unused feature
- Cross-track sync proves too confusing → simplify to "scenario writes only main track" semantic
- Memory budget breach → reduce MAX_TRACKS_PER_SCENARIO до 4
