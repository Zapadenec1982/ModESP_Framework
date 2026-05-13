# ADR-0001: Binary `.modr` Format у LittleFS, NOT C++ constexpr

**Status:** Accepted
**Date:** 2026-05-02

## Context

ModESP_v4 Sequence Engine виконує "recipes" — phased timeline scenarios з tracks, transitions, parameters. Recipes можуть бути firmware-bundled (OEM-shipped) або potentially OTA-uploadable (Stage 1.5+). Їх треба ефективно завантажити на device, валідувати, і виконати без overhead на runtime parsing.

Дві основні альтернативи:

**A. Binary blob `.modr` у LittleFS:** compile-time tool emits packed binary; runtime loader parses у fixed-size buffer; engine executes from RAM.

**B. C++ constexpr struct arrays:** compile-time tool emits `<recipe>_data.h` headers з constexpr arrays; engine references them directly у flash without runtime parsing.

ESPHome production usage demonstrates approach B works at scale. Foundation document Розділ 4.4 specifies approach A (binary blob).

## Decision

**Approach A — binary `.modr` у LittleFS** для Stage 1 MVP.

Specifically:
- `compile_scenario.py` emits `data/scenarios/<recipe_name>.modr` (single file per recipe)
- LittleFS partition image bundles `data/`
- Runtime: engine `f_read` entire blob into fixed-size buffer (`MODR_MAX_SIZE = 16 KB`)
- Loader validates magic + version + CRC32 + structure; resolves action/condition hashes
- Engine executes from RAM buffer

**Defer constexpr path** до Stage 1.5 для factory recipes якщо/коли buduть concrete benefits.

## Alternatives considered

### B (constexpr) — rejected for MVP

**Pros:**
- Smaller flash footprint (no LittleFS overhead, no parser code)
- Faster boot (no I/O при load)
- No fuzzing surface (no runtime parser)
- Industry-validated (ESPHome у production)
- Simpler memory budget

**Cons (why rejected):**
- **Recipe як firmware artifact only:** any recipe change requires firmware rebuild. Foundation document explicitly chose binary path для interop, AI-friendly tooling, future OTA-uploadable recipes.
- **Manifest pipeline integration weaker:** constexpr emit потребує separate header generation tool with C++ knowledge, не просто JSON-to-binary
- **Stage 1.5 OTA recipes** become rebuild-incompatible

### Hybrid (constexpr factory + binary OTA) — deferred

Considered. Adds complexity (two code paths у engine для recipe storage). MVP keeps single path. Stage 1.5 enhancement якщо cloud-pushed recipes стануть actual product feature.

## Consequences

### Positive
- Foundation document мandate fulfilled — JSON authoring + binary runtime
- Tooling-friendly (compile_scenario.py central, scenario_schema.json human-readable)
- Path до OTA-uploadable recipes очевидний
- AI-friendly authoring (LLMs можуть generate JSON manifests)

### Negative
- Loader is attack surface — **mitigation: libFuzzer integration у Step 8** (60s clean run у CI, ~5 min stretch локально)
- 16 KB static buffer per loaded scenario (~64 KB worst case з 4 instances; budgeted)
- Slower boot than constexpr (~1-2 ms LittleFS read; irrelevant)
- Schema versioning required (`format_version` field у header, migration на breaking changes)

### Neutral
- Foundation document Розділ 4.4 alignment maintained
- Stage 1.5 enhancement path clear (add constexpr alternative для factory recipes; both paths coexist у engine)

## References

- Foundation document Розділ 4.4 ("Authoring vs runtime format")
- Industry research: ESPHome compile-time YAML approach (constexpr alternative)
- Plan Q1 (binary format spec)
- Step 8 (libFuzzer integration mitigation)
- Stage 1.5 deliverable: hybrid recipe storage

## Revisit triggers

Reconsider if:
- Loader CVE-class bug surfaces during fuzzing → may require pivoting до constexpr-only path
- Stage 1.5 cloud-pushed recipes prove unnecessary (e.g., all OEM recipes baked at firmware level) → constexpr becomes simpler choice
- Memory pressure forces tighter footprint than 16 KB blob allows
