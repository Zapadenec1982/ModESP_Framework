# ModESP v4 — Documentation / Документація

This is the documentation root. Pick а language:

- 📖 **[English](en/README.md)** — full table of contents.
- 📖 **[Українська](uk/README.md)** — повний зміст.

## Structure / Структура

```
docs/
├── en/                              ← English documentation
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

## Notes / Примітки

- **Project-level files stay at repo root:** [`README.md`](../README.md),
  [`CHANGELOG.md`](CHANGELOG.md), [`CLAUDE.md`](../CLAUDE.md).
- **Bilingual mirror:** every page у `en/` has а UK twin у `uk/` (and vice
  versa) at the same relative path. Translations are independent — update
  both when content changes.
- **Format:** plain Markdown. No build step. Edit files і send PR.
- **Stale references:** if you find leftover `modesp_sequence` references
  (renamed → `modesp_scenario` у Phase 0 rebuild), please fix them — see
  CHANGELOG entries under "Phase 0..4" для context.

## Quick links / Швидкі посилання

| Topic | EN | UK |
|---|---|---|
| Quickstart / Швидкий старт | [link](en/01-getting-started/quickstart.md) | [link](uk/01-getting-started/quickstart.md) |
| Module Author Guide | [link](en/02-module-author-guide/overview.md) | [link](uk/02-module-author-guide/overview.md) |
| Manifest reference | [link](en/02-module-author-guide/manifest.md) | [link](uk/02-module-author-guide/manifest.md) |
| Scenario Engine | [link](en/03-framework-reference/scenario-engine/) | [link](uk/03-framework-reference/scenario-engine/) |
| Architecture | [link](en/03-framework-reference/architecture.md) | [link](uk/03-framework-reference/architecture.md) |
