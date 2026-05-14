# Docs style — writing documentation pages

> 📖 **In English:** [documentation/en/06-contributing/docs-style.md](../../en/06-contributing/docs-style.md)

Це **summary**. Full enforcement rules live у top-level
**[STYLE.md](../../STYLE.md)** — read it перед writing вашої
першої сторінки.

## Що ми пишемо

Документація фреймворку targets чотири audiences, у priority order:

1. **Module authors** — engineers writing business modules. Primary.
2. **Framework contributors** — engineers modifying framework code.
3. **Hardware integrators** — wiring boards і bindings.
4. **Operators** — using WebUI / MQTT але не writing code.

Кожна page should be obviously written для one of these. Якщо не
можете сказати який — page lacks focus — rewrite.

## Bilingual policy

Кожна doc page exists у **two languages**:

```
documentation/en/<path>/<name>.md
documentation/uk/<path>/<name>.md
```

Коли додаєте або edit page, **update both versions**. Вони mirror
each other у structure І content. Differences allowed:

- Code blocks залишаються English (variable names, function names, log lines).
- Comments inside code blocks можна translate якщо instructive.
- Cross-link twin з banner на top:

```markdown
> 📖 **In English:** [path/to/en/twin.md](../en/path/to/twin.md)
```

Не bilingual-mix у одній page — pick одну language І stick з нею.
Code snippets — only exception.

## Page anatomy (структурна convention)

```markdown
# `<name>` — <one-line summary>

> 📖 **In English:** [twin link]

<2-4 paragraph introduction explaining what this thing is, why it
exists, AND when а reader should care>

REQUIRES: <dependencies, if а technical doc>

## <Section 1: most important first>

...

## Common pitfalls

<bullet list of bugs/confusions readers will hit>

## Що далі

<3-5 link bullets that the reader of THIS page is likely to need next>

## Source

<links to the underlying files: code, manifests, test fixtures, ADRs>
```

**Opening paragraph** answers: "що це? чому існує? хто should read?"
**Source** section на end says де truth lives. Кожна page має both.

## Audience signals

Match tone до audience:

- **Module authors** — practical, "ви write `X`, framework does `Y`".
  Heavy на examples. Light на internals.
- **Framework contributors** — internals, design rationale, trade-offs.
  Reference architecture decisions.
- **Hardware integrators** — concrete wiring, pin assignments, voltage
  levels.
- **Operators** — UI navigation, що buttons do, no C++.

## Status markers

У README tables:

| Document | Status | Purpose |
|---|---|---|
| existing.md | ✅ | Done. |
| planned.md | ⏳ planned | Stub АБО scheduled. |
| existing.md | 🚧 WIP | Active rewrite. |

Коли finish page, change ⏳ на ✅ І make sure link wired.

## Cross-references

- Завжди link OTHER docs з relative paths: `[02-module-author-guide/manifest.md](../02-module-author-guide/manifest.md)`.
- Завжди link SOURCE files з deep paths з documentation root:
  `[components/modesp_core/](../../../../components/modesp_core/)`.
- Не link з text до absolute URLs unless external. Inside repo —
  relative.

Якщо link broken на rename, fix EVERY page що references it.
`grep -r 'old_name.md' documentation/` — ваш друг.

## Formatting

- **Markdown** (CommonMark + GitHub tables). No HTML, no MDX, no
  custom flavours.
- **Headings** start at `##` після H1 title. Hierarchy: H1 → H2 → H3.
  Skip levels OK якщо improves clarity.
- **Code blocks** з language tags: `cpp`, `bash`, `json`, `python`,
  `cmake`. Plain ``` ` ``` тільки для very short inline code.
- **Tables** для structured comparisons (3+ items з 2+ attributes).
- **Lists** для steps АБО options. Avoid bullets longer than 2 lines.
- **Bold** sparingly — для emphasis, не decoration.

## Length

- **Module author guide pages** — 200-500 lines. Comprehensive,
  workflow-driven.
- **Component reference pages** — 200-400 lines. API + design rationale.
- **Module / driver reference pages** — 100-250 lines. Manifest fields,
  pitfalls, source pointer.
- **Tool pages** — 150-300 lines. CLI + integration + pitfalls.
- **Concept pages** — 150-300 lines. Mental models.

Якщо page approaches 700 lines, split it.

## Quality bar (правила STYLE.md)

З top-level style guide:

1. **Кожна page answers concrete reader question** — stated у
   introduction.
2. **Show, don't tell** — examples beat prose. Real code, не pseudocode.
3. **Cross-reference everything** — кожен concept links to його
   reference page.
4. **Source pointers mandatory** — кожна page ends з Source
   section.
5. **Bilingual parity** — EN І UK versions tracked together.
6. **No dead links** — broken cross-references fail PR review.
7. **No marketing prose** — describe що does, не how revolutionary
   it is.
8. **Don't bury the lede** — opening paragraph says що thing is
   І чому хтось reads page.

## Pitfalls

**"Let me explain how it works":** describe behaviour, не journey.
"X does Y коли Z" beats "first, ми initialize, then ми configure, then..."

**Forgotten EN/UK sync:** edit EN, forget UK. Reviewer catches; PR
gets bounced. Завжди edit both у same PR.

**Stale cross-references:** rename page, leave 5 other pages
referencing old name. Run `grep -r 'old_path.md' documentation/`
щоб знайти them.

**Code blocks without language tags:** unreadable у renders. Завжди
tag.

**Vague "see also" sections:** "see the docs" — useless. Specific
links з sentence saying why.

## Що далі

- **[STYLE.md](../../STYLE.md)** — canonical style guide (full).
- **[development-setup.md](development-setup.md)** — env що hosts ці docs.
- **[testing.md](testing.md)** — що gets verified.
- **[code-style.md](code-style.md)** — C++ style для code що ends
  up referenced з ціх docs.

## Source

- `documentation/STYLE.md` — top-level style guide.
- Ця page сама.
- Кожна existing documentation page — вони reference для як
  pages look.
