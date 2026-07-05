# ModESP v4 — Documentation / Документація

This is the **canonical** documentation for ModESP v4. Pick a language:

- 📖 **[English](en/README.md)** — full table of contents.
- 📖 **[Українська](uk/README.md)** — повний зміст.

## Structure / Структура

```
documentation/
├── STYLE.md                        ← documentation style guide
├── en/                             ← English (international target audience)
│   └── CHANGELOG.md                ← project changelog (EN)
│   ├── 01-getting-started/
│   ├── 02-module-author-guide/    ← Primary audience: module authors
│   ├── 03-framework-reference/
│   │   ├── components/             ← Per-component refs
│   │   ├── modules/                ← Per-module refs
│   │   ├── drivers/                ← Per-driver refs
│   │   ├── scenario-engine/        ← Scenario engine deep dive (architecture + ADRs + usage)
│   │   └── web-ui.md
│   ├── 04-hardware/
│   ├── 05-tools/
│   └── 06-contributing/
└── uk/                             ← Дзеркало українською
    └── (та сама структура)
```

## Quality bar / Стандарт якості

Every page in this directory follows the conventions in [`STYLE.md`](STYLE.md):

- Bilingual mirror (EN + UK at matching paths).
- One file == one topic.
- Opens with a 1-paragraph "what and why" lede.
- Code examples are copy-paste runnable (real IPs, real paths, real commands).
- Cross-references explicit (relative paths or `file:line`).
- Target size: 200-800 lines per page.

**See [STYLE.md](STYLE.md) before contributing.**

## Quick links / Швидкі посилання

| Topic | EN | UK |
|---|---|---|
| Quickstart / Швидкий старт | [link](en/01-getting-started/quickstart.md) | [link](uk/01-getting-started/quickstart.md) |
| Installation / Встановлення | [link](en/01-getting-started/installation.md) | [link](uk/01-getting-started/installation.md) |
| Concepts / Концепції | [link](en/01-getting-started/concepts.md) | [link](uk/01-getting-started/concepts.md) |
| Module Author Guide | [link](en/02-module-author-guide/overview.md) | [link](uk/02-module-author-guide/overview.md) |
| ⭐ Rules / Звід правил | [link](en/03-framework-reference/rules.md) | [link](uk/03-framework-reference/rules.md) |
| Project hierarchy / Ієрархія | — 🇺🇦 | [link](uk/03-framework-reference/project-hierarchy.md) |
| Architecture | [link](en/03-framework-reference/architecture.md) | [link](uk/03-framework-reference/architecture.md) |
| Scenario engine deep dive | [link](en/03-framework-reference/scenario-engine/README.md) | [link](uk/03-framework-reference/scenario-engine/README.md) |
| CHANGELOG | [en/CHANGELOG.md](en/CHANGELOG.md) | [uk/CHANGELOG.md](uk/CHANGELOG.md) |

## History

`documentation/` was a clean-slate strategic rewrite following the
`modesp_sequence` → `modesp_scenario` engine rebuild. The previous
`docs/` directory has been removed; its content was either migrated
into `documentation/` under a single quality standard or dropped as
superseded.
