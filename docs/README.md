# ⚠️ Legacy documentation

> **The official, current documentation has moved to [`documentation/`](../documentation/README.md).**
>
> Усе нове пишеться там за єдиним standard ([STYLE.md](../documentation/STYLE.md)).
> Ця папка `docs/` залишається як **legacy reference** — частина сторінок
> ще фактично коректна, частина застаріла. Жодна не авторитетна доки не
> переписана у `documentation/`.
>
> Якщо ви шукаєте інформацію — спочатку перевірте
> **[documentation/](../documentation/README.md)**. Якщо там сторінки немає —
> поверніться сюди як до fallback.
>
> 📋 Why the split? See
> [documentation/README.md → "Why а separate folder?"](../documentation/README.md#why-а-separate-folder--чому-окрема-папка).

---

## Legacy navigation / Стара навігація

The bilingual mirror tree that was set up below remains as а bridge for
content not yet rewritten у `documentation/`:

- 📖 **[English (legacy)](en/README.md)**
- 📖 **[Українська (legacy)](uk/README.md)**

### Original structure

```
docs/
├── en/                              ← Migrated English content
│   ├── 03-framework-reference/
│   │   └── scenario-engine/         ← Auto-scrubbed з sequence_engine/
│   └── ...
└── uk/                              ← Migrated Ukrainian content
    └── ...
```

### Notes

- **Project-level files stay at repo root:** [`README.md`](../README.md),
  [`CHANGELOG.md`](CHANGELOG.md), [`CLAUDE.md`](../CLAUDE.md).
- **Stale references:** if you find leftover `modesp_sequence` references
  (renamed → `modesp_scenario` у Phase 0 rebuild), please fix them OR replace
  the whole page із an entry under `documentation/` rather than patching.
- **Once `documentation/` reaches ~80% coverage**, this directory will be
  moved to `docs/archive/` or removed entirely.
