# ModESP v4 — Documentation / Документація

This is the **official, current** documentation for ModESP v4. Pick а language:

- 📖 **[English](en/README.md)** — full table of contents.
- 📖 **[Українська](uk/README.md)** — повний зміст.

> ℹ️ **Legacy docs:** The previous `docs/` directory contains pre-rebuild
> documentation that's being replaced by this directory. See
> [`docs/README.md`](../docs/README.md) for context. Once `documentation/`
> achieves comprehensive coverage, `docs/` will be archived.

## Structure / Структура

```
documentation/
├── en/                              ← English (international target audience)
│   ├── 01-getting-started/
│   ├── 02-module-author-guide/      ← Primary audience: module authors
│   ├── 03-framework-reference/
│   │   ├── components/              ← Per-component refs
│   │   ├── modules/                 ← Per-module refs
│   │   └── scenario-engine/         ← Scenario engine deep dive
│   ├── 04-hardware/
│   ├── 05-tools/
│   ├── 06-contributing/
│   └── adr/                         ← Architecture Decision Records
└── uk/                              ← Дзеркало українською
    └── (та сама структура)
```

## Quality bar / Стандарт якості

Every page у це directory follows the conventions у [`STYLE.md`](STYLE.md):

- Bilingual mirror (EN + UK at matching paths).
- One file == one topic.
- Opens із а 1-paragraph "what і why" lede.
- Code examples are copy-paste runnable (real IPs, real paths, real commands).
- Cross-references explicit (relative paths or `file:line`).
- Target size: 200-800 lines per page.

**See [STYLE.md](STYLE.md) before contributing.**

## Quick links / Швидкі посилання

| Topic | EN | UK |
|---|---|---|
| Quickstart / Швидкий старт | [link](en/01-getting-started/quickstart.md) | [link](uk/01-getting-started/quickstart.md) |
| Module Author Guide | [link](en/02-module-author-guide/overview.md) | [link](uk/02-module-author-guide/overview.md) |
| (more pages landing as written) | | |

## Why а separate folder? / Чому окрема папка?

Previous `docs/` was а half-migrated mix: some pages written for older engine
versions (`modesp_sequence`), some freshly-written для new engine
(`modesp_scenario`), some auto-scrubbed but not reviewed. Quality varied page
to page.

`documentation/` is the clean-slate strategic rewrite. Every page meets the
same bar from day one. Migrated legacy pages remain accessible у `docs/` для
reference but are not authoritative.

Coverage rolls out progressively over many sessions. Pages that don't yet
exist у `documentation/` will be linked back до `docs/` as а bridge until
their rewrite lands.
