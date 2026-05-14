# ADR-0001: Binary `.modr` Format in LittleFS, NOT C++ constexpr

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/adr/0001-binary-format-not-constexpr.md](../../../../uk/03-framework-reference/scenario-engine/adr/0001-binary-format-not-constexpr.md)

**Status:** Accepted
**Date:** 2026-05-02

## Context

The ModESP_v4 Sequence Engine runs "recipes" — phased timeline scenarios with tracks, transitions, and parameters. Recipes can be firmware-bundled (OEM-shipped) or potentially OTA-uploadable (Stage 1.5+). They need to load onto the device efficiently, be validated, and execute without runtime parsing overhead.

Two main alternatives were considered:

**A. Binary blob `.modr` in LittleFS:** a compile-time tool emits a packed binary; the runtime loader parses it into a fixed-size buffer; the engine executes from RAM.

**B. C++ constexpr struct arrays:** a compile-time tool emits `<recipe>_data.h` headers with constexpr arrays; the engine references them directly in flash with no runtime parsing.

ESPHome production usage demonstrates that approach B works at scale. The foundation document, section 4.4, specifies approach A (binary blob).

## Decision

**Approach A — binary `.modr` in LittleFS** for Stage 1 MVP.

Specifically:
- `compile_scenario.py` emits `data/scenarios/<recipe_name>.modr` (one file per recipe)
- The LittleFS partition image bundles `data/`
- Runtime: the engine `f_read`s the entire blob into a fixed-size buffer (`MODR_MAX_SIZE = 16 KB`)
- The loader validates magic + version + CRC32 + structure, and resolves action/condition hashes
- The engine executes from the RAM buffer

**Defer the constexpr path** to Stage 1.5 for factory recipes if/when there are concrete benefits.

## Alternatives considered

### B (constexpr) — rejected for MVP

**Pros:**
- Smaller flash footprint (no LittleFS overhead, no parser code)
- Faster boot (no I/O on load)
- No fuzzing surface (no runtime parser)
- Industry-validated (ESPHome in production)
- Simpler memory budget

**Cons (why rejected):**
- **Recipe as firmware artifact only:** any recipe change requires a firmware rebuild. The foundation document explicitly chose the binary path for interop, AI-friendly tooling, and future OTA-uploadable recipes.
- **Weaker manifest pipeline integration:** constexpr emission requires a separate header generation tool with C++ knowledge, not just JSON-to-binary.
- **Stage 1.5 OTA recipes** would become rebuild-incompatible.

### Hybrid (constexpr factory + binary OTA) — deferred

Considered. Adds complexity (two code paths in the engine for recipe storage). MVP keeps a single path. Stage 1.5 enhancement if cloud-pushed recipes become an actual product feature.

## Consequences

### Positive
- The foundation document mandate is fulfilled — JSON authoring + binary runtime.
- Tooling-friendly (compile_scenario.py is central, scenario_schema.json is human-readable).
- The path to OTA-uploadable recipes is obvious.
- AI-friendly authoring (LLMs can generate JSON manifests).

### Negative
- The loader is an attack surface — **mitigation: libFuzzer integration in Step 8** (60 s clean run in CI, ~5 min stretch locally).
- 16 KB static buffer per loaded scenario (~64 KB worst case with 4 instances; budgeted).
- Slower boot than constexpr (~1–2 ms LittleFS read; irrelevant).
- Schema versioning is required (`format_version` field in the header, migration on breaking changes).

### Neutral
- Foundation document section 4.4 alignment is maintained.
- The Stage 1.5 enhancement path is clear (add a constexpr alternative for factory recipes; both paths coexist in the engine).

## References

- Foundation document section 4.4 ("Authoring vs runtime format")
- Industry research: ESPHome compile-time YAML approach (constexpr alternative)
- Plan Q1 (binary format spec)
- Step 8 (libFuzzer integration mitigation)
- Stage 1.5 deliverable: hybrid recipe storage

## Revisit triggers

Reconsider if:
- A loader CVE-class bug surfaces during fuzzing → may require pivoting to a constexpr-only path.
- Stage 1.5 cloud-pushed recipes prove unnecessary (e.g., all OEM recipes are baked in at firmware level) → constexpr becomes the simpler choice.
- Memory pressure forces a tighter footprint than a 16 KB blob allows.
