# Docs style — writing documentation pages

> 📖 **Українською:** [documentation/uk/06-contributing/docs-style.md](../../uk/06-contributing/docs-style.md)

This is the **summary**. The full enforcement rules live у the
top-level **[STYLE.md](../../STYLE.md)** — read it перед writing your
first page.

## What we write

The framework documentation targets four audiences, у priority order:

1. **Module authors** — engineers writing business modules. Primary.
2. **Framework contributors** — engineers modifying framework code.
3. **Hardware integrators** — wiring boards AND bindings.
4. **Operators** — using WebUI / MQTT but not writing code.

Every page should be obviously written for one of these. If you can't
say which, the page lacks focus — rewrite.

## Bilingual policy

Every doc page exists у **two languages**:

```
documentation/en/<path>/<name>.md
documentation/uk/<path>/<name>.md
```

When you add or edit а page, **update both versions**. They mirror
each other у structure AND content. Differences allowed:

- Code blocks stay English (variable names, function names, log lines).
- Comments inside code blocks can be translated if instructive.
- Cross-link the twin із а banner at the top:

```markdown
> 📖 **Українською:** [path/to/uk/twin.md](../uk/path/to/twin.md)
```

Don't bilingual-mix у one page — pick а language AND stick із it.
Code snippets are the only exception.

## Page anatomy (the structural convention)

```markdown
# `<name>` — <one-line summary>

> 📖 **Українською:** [twin link]

<2-4 paragraph introduction explaining what this thing is, why it
exists, AND when а reader should care>

REQUIRES: <dependencies, if а technical doc>

## <Section 1: most important first>

...

## Common pitfalls

<bullet list of bugs/confusions readers will hit>

## Next steps

<3-5 link bullets that the reader of THIS page is likely to need next>

## Source

<links to the underlying files: code, manifests, test fixtures, ADRs>
```

The **opening paragraph** answers: "what is це? why does it exist?
who should read це?" The **Source** section at the end says where the
truth lives. Every page має both.

## Audience signals

Match tone to audience:

- **Module authors** — practical, "you write `X`, the framework does `Y`".
  Heavy on examples. Light on internals.
- **Framework contributors** — internals, design rationale, trade-offs.
  Reference architecture decisions.
- **Hardware integrators** — concrete wiring, pin assignments, voltage
  levels.
- **Operators** — UI navigation, what buttons do, no C++.

## Status markers

In README tables:

| Document | Status | Purpose |
|---|---|---|
| existing.md | ✅ | Done. |
| planned.md | ⏳ planned | Stub OR scheduled. |
| existing.md | 🚧 WIP | Active rewrite. |

When you finish а page, change ⏳ to ✅ AND make sure the link is wired.

## Cross-references

- Always link OTHER docs із relative paths: `[02-module-author-guide/manifest.md](../02-module-author-guide/manifest.md)`.
- Always link SOURCE files із deep paths from documentation root:
  `[components/modesp_core/](../../../../components/modesp_core/)`.
- Don't link from text to absolute URLs unless external. Inside repo,
  relative.

If а link is broken on rename, fix EVERY page that references it.
`grep -r 'old_name.md' documentation/` is your friend.

## Formatting

- **Markdown** (CommonMark + GitHub tables). No HTML, no MDX, no
  custom flavours.
- **Headings** start at `##` after the H1 title. Hierarchy: H1 → H2 → H3.
  Skip levels OK if it improves clarity.
- **Code blocks** із language tags: `cpp`, `bash`, `json`, `python`,
  `cmake`. Plain ``` ` ``` only for very short inline code.
- **Tables** for structured comparisons (3+ items із 2+ attributes).
- **Lists** for steps OR optionsizons. Avoid bullets longer than 2 lines.
- **Bold** sparingly — for emphasis, not decoration.

## Length

- **Module author guide pages** — 200-500 lines. Comprehensive,
  workflow-driven.
- **Component reference pages** — 200-400 lines. API + design rationale.
- **Module / driver reference pages** — 100-250 lines. Manifest fields,
  pitfalls, source pointer.
- **Tool pages** — 150-300 lines. CLI + integration + pitfalls.
- **Concept pages** — 150-300 lines. Mental models.

If а page approaches 700 lines, split it.

## Quality bar (the STYLE.md rules)

From the top-level style guide:

1. **Every page answers а concrete reader question** — stated у the
   introduction.
2. **Show, don't tell** — examples beat prose. Real code, not pseudocode.
3. **Cross-reference everything** — every concept links to its
   reference page.
4. **Source pointers are mandatory** — every page ends із а Source
   section.
5. **Bilingual parity** — EN AND UK versions tracked together.
6. **No dead links** — broken cross-references fail PR review.
7. **No marketing prose** — describe what does, not how revolutionary
   it is.
8. **Don't bury the lede** — opening paragraph says what the thing is
   AND why someone is reading the page.

## Pitfalls

**"Let me explain how it works":** describe behaviour, не journey.
"X does Y when Z" beats "first, we initialize, then we configure, then..."

**Forgotten EN/UK sync:** edit EN, forget UK. Reviewer catches; PR
gets bounced. Always edit both у the same PR.

**Stale cross-references:** rename а page, leave 5 other pages
referencing the old name. Run `grep -r 'old_path.md' documentation/`
to find them.

**Code blocks without language tags:** unreadable у renders. Always
tag.

**Vague "see also" sections:** "see the docs" — useless. Specific
links із а sentence saying why.

## Next steps

- **[STYLE.md](../../STYLE.md)** — the canonical style guide (full).
- **[development-setup.md](development-setup.md)** — env that hosts these docs.
- **[testing.md](testing.md)** — what gets verified.
- **[code-style.md](code-style.md)** — C++ style for code that ends
  up referenced from these docs.

## Source

- `documentation/STYLE.md` — top-level style guide.
- This page itself.
- Every existing documentation page — they're the reference for how
  pages look.
