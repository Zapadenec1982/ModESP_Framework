# ADR-0002: Tracks as First-Class Concept (NOT Retrofit)

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/adr/0002-tracks-as-first-class.md](../../../../uk/03-framework-reference/scenario-engine/adr/0002-tracks-as-first-class.md)

**Status:** Accepted
**Date:** 2026-05-02

## Context

The Sequence Engine runs phased timeline scenarios. Initial discussion considered single-track scenarios (one sequence of phases) as a baseline, with multi-track support potentially added later.

Real-world embedded automation **is always multi-channel** — for example, a multicooker recipe:
- Track 1 (heat control): warmup → simmer → finish
- Track 2 (stir schedule): active 10 min → idle until heat complete
- Track 3 (UI notifications): notify_at_minute_20 → notify_done
- Track 4 (safety monitor): continuous fault detection

Parallel tracks share the scenario timeline but have independent phase progression. Without first-class track support, recipe authors have to provide parallelism manually via cross-module hacks.

The user's analogy (MIDI / punch card) is an exact metaphor: a scenario is a song with multiple tracks playing simultaneously, synchronized along a shared time line.

## Decision

**Tracks are a first-class concept in the scenario engine from Stage 1.**

Specifically:
- The `.modr` header has a `track_count` field (1..6, MAX_TRACKS_PER_SCENARIO).
- Each track has its own phase array, transitions, and current state.
- A single tick task iterates instances × tracks; per-track state machines run independently.
- `completion_rule` (`all_tracks_complete` | `any_track_complete` | `main_track_complete`) defines scenario completion.
- A single-track recipe is `tracks: [{name: "main", phases: [...]}]` — the degenerate case of multi-track.
- Cross-track synchronization uses SharedState (publish/subscribe pattern, existing ModESP infrastructure).

**Do NOT defer to Stage 1.5** because the retrofit cost is high — the track concept changes binary format, API, state machine structure, and NVS layout. Building tracks-aware from day 1 is significantly cheaper.

## Alternatives considered

### A. Single-track MVP, multi-track in Stage 1.5 — rejected

**Pros:**
- Smaller MVP (~30% less code)
- Simpler reasoning early on

**Cons (why rejected):**
- Binary format breaking change required to add tracks.
- API breaking change (`engine.track_state(handle)` → `engine.track_state(handle, track)`).
- NVS layout change (per-track sub-state not present in single-track tokens).
- State machine restructuring required.
- Recipe authors would author multi-process logic via a single-track + state-machine-in-state-keys workaround in the meantime → tech debt.
- The user explicitly asked for tracks "from the very beginning".

### B. HSM (Hierarchical State Machines) instead of flat tracks — rejected

**Pros:**
- Industry-standard formalism (Samek QP, BOOST.MSM, ETL state_chart).
- Orthogonal regions formally support concurrency.

**Cons (why rejected):**
- Overkill for mostly-linear recipes with occasional branching.
- HSM hierarchy implementation overhead (~3× code).
- Recipe author cognitive load (state regions, transition lookup precedence).
- IEC 61131-3 SFC and ISA-88 — both use flat phase lists with parallel branches, not HSM.
- BehaviorTree.CPP also rejected HSM in favor of flat composable trees.

### C. Behavior trees instead of phase lists — rejected

**Pros:**
- More composable (subtrees reusable).
- ROS 2 Nav2 production validates the approach.

**Cons (why rejected):**
- Recipe authors are not robotics engineers; a phase list is a more accessible mental model.
- Authoring trees without a visual editor (which is out of engine scope) — painful in JSON.
- Multi-track parallel = SFC parallel branches, which is simpler.

## Consequences

### Positive
- Real-world recipes naturally express multi-process automation (multicooker, fermenter, lab batch).
- Cross-track sync via SharedState reuses existing infrastructure (no new IPC).
- API is consistent with the MIDI/sequencer mental model.
- ISA-88 / SFC alignment is maintained.
- The user's punch-card analogy is supported elegantly.

### Negative
- Larger MVP scope (~3000 LOC vs ~2200).
- Cross-track sync semantics require careful documentation (ADR-0003).
- More state to track in engine RAM and NVS.
- Per-track UI mirror keys deferred to Stage 1.5 (capacity constraint).

### Neutral
- Fixed-capacity arrays (`MAX_TRACKS_PER_SCENARIO = 6`) define a hard upper bound — Kconfig'd if needed.

## References

- Foundation document already mentions tracks (section 4.5, phase decomposition + concurrent behaviors)
- Industry: ROS Smach Concurrence container, BehaviorTree.CPP `Parallel` node, MIDI sequencer model
- IEC 61131-3 SFC parallel branches
- Plan Q1 (binary format track table), Q2 (API per-track methods)

## Revisit triggers

Reconsider if:
- All real-world recipes turn out to be single-track → accept the overhead but document tracks as a Stage 2 unused feature.
- Cross-track sync proves too confusing → simplify to "scenario writes only main track" semantics.
- Memory budget breach → reduce MAX_TRACKS_PER_SCENARIO to 4.
