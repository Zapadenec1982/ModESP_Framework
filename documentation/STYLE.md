# Documentation Style Guide

This is the quality bar для everything у `documentation/`. Read це перед
contributing or rewriting.

## File anatomy

Every page follows the same skeleton:

```markdown
# Topic Name

> 📖 **In <other language>:** [link to bilingual twin](../<other>/path/to/twin.md)

One-paragraph "what і why" lede. Three sentences max. Answer:
1. What does це topic cover?
2. Why does the reader care?
3. What will they be able to do after reading?

## Concepts

Mental model, definitions, terminology. Establish vocabulary before how-to.

## How-to / Reference / Etc

The body. Sized to topic. Use sub-headings liberally — wall of text loses
readers fast.

## Next steps

Pointer to:
- Related deeper docs.
- Examples (working code).
- Adjacent topics.

## Troubleshooting / FAQ (optional)

If common pitfalls exist, list them із "symptom → cause → fix" format.
```

## Voice і tone

- **Technical but conversational.** Write як you'd explain over coffee, not
  як you'd lecture. Use "you" not "the reader". Use "we" sparingly (only коли
  you mean the maintainers).
- **Active voice.** Не "the engine is ticked by ModuleManager" but
  "ModuleManager ticks the engine".
- **Specific over abstract.** "100 Hz tick rate" not "frequent". "16 KB
  buffer" not "small buffer". Numbers, names, file paths — concrete anchors.
- **Honest about limits.** If something is а workaround, say so. If а
  feature is Stage 1.5, say so. Don't oversell.

## Code examples

**Every code block is copy-paste runnable.** No pseudo-code without explicit
`<!-- pseudo-code -->` callout. If а command needs а value (IP, path,
credentials), use **placeholder syntax that fails clearly** if not
substituted, не `<your-ip>` що runs as literal text:

```bash
# Good:
curl http://192.168.1.85/api/state    # ← reader replaces з their IP

# Acceptable із explicit placeholder marker:
curl http://${ESP_IP}/api/state
```

Languages used у code blocks: `bash`, `cpp`, `c`, `python`, `json`, `cmake`,
`yaml`. Always specify language for syntax highlighting.

## Cross-references

**Always relative paths**, not absolute, not raw GitHub URLs:

```markdown
Good: [shared-state.md](shared-state.md)
Good: [Engine class](../03-framework-reference/scenario-engine/03_api_reference.md#engine)
Good: [components/modesp_core.md](../03-framework-reference/components/modesp_core.md)

Bad:  [link](https://github.com/.../docs/...)
Bad:  [link](/docs/en/...)
```

Reference code lines as `path/to/file.cpp:42` — VS Code-friendly format.

## Page size targets

- **200-400 lines:** focused topics (а single concept or pattern).
- **400-800 lines:** reference pages (а component API, а manifest section).
- **> 800 lines:** consider splitting. Big pages discourage editing.

## Bilingual rules

- **EN comes first, UK second** if no native preference is stated.
  Translation is independent — UK is а separate write, not а machine
  translation, але EN structure should match UK structure.
- **EN code blocks and identifiers stay English** у UK pages too. Translate
  prose, не identifiers / API names / file paths.
- **Every EN page has а UK twin at the same relative path** under
  `documentation/uk/`. If а topic doesn't yet have UK content, link to EN із
  а "EN only" marker until UK lands.
- **When updating content, update both languages** или explicitly defer one
  у the PR description ("UK translation pending"). Drift accumulates fast.

## Heading hierarchy

- `# H1` — page title only. One per file.
- `## H2` — major sections (3-6 per page typical).
- `### H3` — subsections within H2.
- `#### H4` — sub-subsections. Rare — if you need це often, page is too dense.

Don't skip levels (H2 → H4). Use bold or list items для emphasis instead of
fake headings.

## Tables, lists, callouts

**Tables** для structured comparisons or reference data (API rows, file
roles). Don't fake tables with lists.

**Lists** for unordered sets or sequential steps. Numbered for sequence,
bullet for parallel options.

**Callouts** via blockquote:
```markdown
> ⚠️ **Warning:** describes а sharp edge.
> ℹ️ **Note:** clarifies context.
> 💡 **Tip:** shortcut або best practice.
```

Use sparingly. Two per page max — they lose impact otherwise.

## Updating the index

Every new page lands у its language's `README.md` index. Don't add а page
without updating navigation, or readers won't find it.

## What this guide doesn't cover

- **API reference auto-gen:** out of scope. We write doc pages by hand.
- **Doxygen:** considered але not adopted (decision у adr/0001-* if/when
  written). С++ comments stay у headers; reference pages summarise і link
  back to headers.
- **Build pipelines:** these are plain Markdown. No mdBook / MkDocs.
  GitHub renders them natively.
- **Versioned docs:** docs match current `main` branch. Older versions
  accessible через git tags.
