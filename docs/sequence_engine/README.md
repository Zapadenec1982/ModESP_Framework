# Sequence Engine Documentation

`components/modesp_sequence/` — track-based time-dependent algorithm engine для ModESP_v4.

> **Status:** Stage 0 (specifications + skeleton) — implementation in progress. Most files are placeholders that fill out incrementally per implementation step. See plan at `.claude/plans/quirky-imagining-lake.md`.

## Architectural docs

| File | Topic | Filled у Step |
|---|---|---|
| [00_overview.md](00_overview.md) | What/why/who | 0 (now) |
| [01_architecture.md](01_architecture.md) | High-level diagrams + components | 1+ |
| [02_binary_format.md](02_binary_format.md) | `.modr` byte-by-byte spec | 1 |
| [03_api_reference.md](03_api_reference.md) | C++ public API | 5, 6, 14 |
| [04_state_machines.md](04_state_machines.md) | Scenario + per-track FSMs | 11, 12 |
| [05_synchronization.md](05_synchronization.md) | Tick-order cross-track sync | 13 |
| [06_resource_arbitration.md](06_resource_arbitration.md) | ISA-88 §5.3 mapping | 10 |
| [07_persistence.md](07_persistence.md) | NVS layout + write policy | 15 |
| [08_lifecycle.md](08_lifecycle.md) | Build-time + runtime lifecycle | 14 |
| [09_manifest_integration.md](09_manifest_integration.md) | Recipe-as-manifest pipeline | 2, 4 |
| [10_error_model.md](10_error_model.md) | EngineError codes + action failure machine | 7, 8 |

## Usage docs (developer guide)

| File | Topic | Filled у Step |
|---|---|---|
| [usage/01_quickstart.md](usage/01_quickstart.md) | 5-min hands-on | 16 |
| [usage/02_writing_recipes.md](usage/02_writing_recipes.md) | Authoring guide | 2, 7, 13 |
| [usage/03_registering_actions.md](usage/03_registering_actions.md) | Custom actions у domain modules | 5 |
| [usage/examples/01_minimal_3phase.md](usage/examples/01_minimal_3phase.md) | Single-track example | 16 |
| [usage/examples/02_dual_track_sync.md](usage/examples/02_dual_track_sync.md) | Multi-track sync example | 17 |

## Architecture Decision Records

| ADR | Title | Status |
|---|---|---|
| [0001](adr/0001-binary-format-not-constexpr.md) | Binary `.modr` format у LittleFS, NOT C++ constexpr | Accepted |
| [0002](adr/0002-tracks-as-first-class.md) | Tracks first-class concept (NOT retrofit) | Accepted |
| [0003](adr/0003-tick-order-sync-semantics.md) | Tick-order cross-track sync (NOT snapshot) | placeholder, filled у Step 13 |
| [0004](adr/0004-recipe-as-manifest.md) | Recipe = manifest з `scenario` section | placeholder, filled у Step 2 |
| [0005](adr/0005-isa88-resource-arbitration.md) | ISA-88 §5.3 acquire-before-start | placeholder, filled у Step 10 |
| [0006](adr/0006-no-builtin-continuous-behaviors.md) | 0 built-in continuous behaviors у MVP | placeholder, filled у Step 6 |
| [0007](adr/0007-mandatory-phase-timeouts.md) | Mandatory per-phase timeouts | placeholder, filled у Step 8 |
| [0008](adr/0008-expressiveness-paper-pilot.md) | Expressiveness paper pilot validation (Step 0.75) | Accepted |

## Deferred to Stage 1.5

These docs are NOT у Stage 1 deliverable, planned для Stage 1.5 коли real value emerges:
- `usage/04_custom_continuous.md`, `usage/05_resource_management.md`, `usage/06_persistence_and_recovery.md`, `usage/07_testing_recipes.md`
- `usage/troubleshooting.md`
- `usage/examples/03_resource_contention.md`, `04_long_running_with_resume.md`, `05_greenhouse_irrigation.md` (recipe paper-piloted у Step 0.75)
- `maint/01_contributing.md`, `02_binary_format_versioning.md`, `03_adding_builtin_action.md`, `04_test_strategy.md`, `05_release_checklist.md`
- Doc-validation tests (code-in-docs, link checker, API ref completeness lint)

## Reading order

For new contributors / consumers:
1. [00_overview.md](00_overview.md) — what is this
2. [usage/01_quickstart.md](usage/01_quickstart.md) — minimal example
3. [usage/02_writing_recipes.md](usage/02_writing_recipes.md) — authoring guide
4. [09_manifest_integration.md](09_manifest_integration.md) — pipeline integration
5. [03_api_reference.md](03_api_reference.md) — C++ API for business modules
6. ADRs `0001-0007` — critical decisions з rationale

For implementers:
1. Plan `.claude/plans/quirky-imagining-lake.md` — single source of truth для architecture
2. ADRs — prevent re-litigation of settled decisions
3. Architectural docs (01-10) — fill them as code lands
