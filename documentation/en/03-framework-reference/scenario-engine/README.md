# Scenario Engine Documentation

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/README.md](../../../uk/03-framework-reference/scenario-engine/README.md)

`components/modesp_scenario/` — a track-based time-dependent algorithm engine for ModESP_v4.

## Architectural docs

| File | Topic |
|---|---|
| [00_overview.md](00_overview.md) | What/why/who |
| [01_architecture.md](01_architecture.md) | High-level diagrams + components |
| [02_binary_format.md](02_binary_format.md) | `.modr` byte-by-byte spec |
| [03_api_reference.md](03_api_reference.md) | C++ public API |
| [04_state_machines.md](04_state_machines.md) | Scenario + per-track FSMs |
| [05_synchronization.md](05_synchronization.md) | Tick-order cross-track sync |
| [06_resource_arbitration.md](06_resource_arbitration.md) | ISA-88 §5.3 mapping |
| [07_persistence.md](07_persistence.md) | NVS layout + write policy |
| [08_lifecycle.md](08_lifecycle.md) | Build-time + runtime lifecycle |
| [09_manifest_integration.md](09_manifest_integration.md) | Recipe-as-manifest pipeline |
| [10_error_model.md](10_error_model.md) | EngineError codes + action failure machine |

## Usage docs (developer guide)

| File | Topic |
|---|---|
| [usage/01_quickstart.md](usage/01_quickstart.md) | 5-min hands-on |
| [usage/02_writing_recipes.md](usage/02_writing_recipes.md) | Authoring guide |
| [usage/03_registering_actions.md](usage/03_registering_actions.md) | Custom actions in domain modules |
| [usage/examples/01_minimal_3phase.md](usage/examples/01_minimal_3phase.md) | Single-track example |
| [usage/examples/02_dual_track_sync.md](usage/examples/02_dual_track_sync.md) | Multi-track sync example |

## Architecture Decision Records

| ADR | Title | Status |
|---|---|---|
| [0001](adr/0001-binary-format-not-constexpr.md) | Binary `.modr` format in LittleFS, NOT C++ constexpr | Accepted |
| [0002](adr/0002-tracks-as-first-class.md) | Tracks as a first-class concept (NOT retrofit) | Accepted |
| [0003](adr/0003-tick-order-sync-semantics.md) | Tick-order cross-track sync (NOT snapshot) | Accepted |
| [0004](adr/0004-recipe-as-manifest.md) | Recipe = manifest with `scenario` section | Accepted |
| [0005](adr/0005-isa88-resource-arbitration.md) | ISA-88 §5.3 acquire-before-start | Accepted |
| [0006](adr/0006-no-builtin-continuous-behaviors.md) | 0 built-in continuous behaviors in MVP | Accepted (superseded in Stage 2) |
| [0007](adr/0007-mandatory-phase-timeouts.md) | Mandatory per-phase timeouts | Accepted |
| [0008](adr/0008-expressiveness-paper-pilot.md) | Expressiveness paper pilot validation (Step 0.75) | Accepted |

## Reading order

For new contributors / consumers:
1. [00_overview.md](00_overview.md) — what this is
2. [usage/01_quickstart.md](usage/01_quickstart.md) — minimal example
3. [usage/02_writing_recipes.md](usage/02_writing_recipes.md) — authoring guide
4. [09_manifest_integration.md](09_manifest_integration.md) — pipeline integration
5. [03_api_reference.md](03_api_reference.md) — C++ API for business modules
6. ADRs `0001-0008` — critical decisions with rationale

For engine maintainers:
1. ADRs — prevent re-litigation of settled decisions
2. Architectural docs (01-10) — authoritative reference for the current engine
