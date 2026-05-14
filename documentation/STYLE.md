# Documentation Style Guide

The quality bar for everything in `documentation/`. Read this before
contributing or rewriting any page.

## File anatomy

Every page follows the same skeleton:

```markdown
# Topic Name

> 📖 **In <other language>:** [link to bilingual twin](../<other>/path/to/twin.md)

One-paragraph "what and why" lede. Three sentences max. Answer:
1. What does this topic cover?
2. Why does the reader care?
3. What will they be able to do after reading?

## Concepts

Mental model, definitions, terminology. Establish vocabulary before
how-to.

## How-to / Reference / Etc

The body. Sized to topic. Use sub-headings liberally — a wall of text
loses readers fast.

## Next steps

Pointer to:
- Related deeper docs.
- Examples (working code).
- Adjacent topics.

## Troubleshooting / FAQ (optional)

If common pitfalls exist, list them in "symptom → cause → fix" format.
```

## Voice and tone

- **Technical but conversational.** Write as you'd explain over coffee,
  not as you'd lecture. Use "you" rather than "the reader". Use "we"
  sparingly (only when you mean the maintainers).
- **Active voice.** Not "the engine is ticked by ModuleManager" but
  "ModuleManager ticks the engine".
- **Specific over abstract.** "100 Hz tick rate" not "frequent". "16 KB
  buffer" not "small buffer". Numbers, names, file paths — concrete
  anchors.
- **Honest about limits.** If something is a workaround, say so. If a
  feature is Stage 1.5, say so. Don't oversell.

## Code examples

**Every code block is copy-paste runnable.** No pseudo-code without an
explicit `<!-- pseudo-code -->` callout. If a command needs a value
(IP, path, credentials), use placeholder syntax that fails clearly if
not substituted, not `<your-ip>` that runs as literal text:

```bash
# Good:
curl http://192.168.1.85/api/state    # ← reader replaces with their IP

# Acceptable with explicit placeholder marker:
curl http://${ESP_IP}/api/state
```

Languages used in code blocks: `bash`, `cpp`, `c`, `python`, `json`,
`cmake`, `yaml`. Always specify the language for syntax highlighting.

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

- **200-400 lines:** focused topics (a single concept or pattern).
- **400-800 lines:** reference pages (a component API, a manifest
  section).
- **> 800 lines:** consider splitting. Big pages discourage editing.

## Bilingual rules

### General

- **EN comes first, UK second** if no native preference is stated.
  Translation is independent — UK is a separate write, not machine
  translation, but EN structure should match UK structure.
- **Every EN page has a UK twin** at the same relative path under
  `documentation/uk/`. If a topic doesn't yet have UK content, link to
  EN with an "EN only" marker until UK lands.
- **When updating content, update both languages** or explicitly defer
  one in the PR description ("UK translation pending"). Drift
  accumulates fast.

### Translation policy for Ukrainian pages

Ukrainian pages must be readable Ukrainian. Mid-sentence English
fragments ("Driver registers як sensor з hardware_type") are not
acceptable — that is not translation, that is broken text.

**Stay in English (these are code, not prose):**

- Code blocks (`cpp`, `json`, `bash`, `python`, `cmake`, `yaml`).
- File paths: `modules/abs_test/manifest.json`,
  `components/modesp_scenario/`.
- Class / method / field names: `BaseModule::on_init()`,
  `manifest_version`, `IDriver`.
- State key names: `equipment.air_temp`, `simple_thermo.output`.
- Manifest field values that appear in code: `module_type: "recipe"`,
  `category: "sensor"`.
- Technical abbreviations and protocols: GPIO, I²C, ADC, NVS, MQTT,
  HTTP, WebSocket, OTA, JSON, CRC, ESP-IDF, USB, Wi-Fi, ROM, RAM, CPU,
  SPI, UART.
- Brand and product names: ESP32, ESP32-S3, DS18B20, PCF8574, LittleFS,
  FreeRTOS, Dallas, Maxim.

**Translate to Ukrainian (this is prose):**

- All running text — descriptions, transitions, section headers,
  verbs, adjectives, conjunctions, sentence connectors.
- Common section names (use these exact forms):
  - "Common pitfalls" → "Типові помилки".
  - "Hardware notes" → "Примітки щодо обладнання".
  - "How it works" → "Як це працює".
  - "Settings" → "Налаштування".
  - "Bindings" → "Прив'язки".
  - "Source" → "Джерела".
  - "Next steps" → "Що далі".
  - "Why ..." → "Чому ..." (e.g. "Why it's a good driver to read"
    → "Чому варто прочитати цей драйвер").

**Standard vocabulary (use consistently):**

| EN | UK |
|---|---|
| driver | драйвер |
| module | модуль |
| recipe | рецепт |
| scenario | сценарій |
| engine | рушій |
| framework | фреймворк |
| manifest | маніфест |
| binding | прив'язка |
| sensor | сенсор |
| actuator | актуатор |
| output (signal) | вихід |
| input (signal) | вхід |
| payload | корисне навантаження |
| handler | обробник |
| callback | зворотний виклик |
| buffer | буфер |
| build (process) | збирання |
| build (artefact) | збірка |
| firmware | прошивка |
| to flash | прошити |
| channel | канал |
| transaction | транзакція |
| write / read | запис / читання |
| mirror keys | дзеркальні ключі |
| tick (engine tick) | такт |
| board | плата |
| pin | вивід (electrical) / контакт |
| feature | можливість / функція |
| pitfall | пастка / типова помилка |

**Examples — bad vs good:**

Bad: "Driver registers як `sensor` з `hardware_type: onewire_bus` і
drives binary output."

Good: "Драйвер реєструється як `sensor` з `hardware_type: onewire_bus`
і керує бінарним виходом."

Bad: "Recipe deliberately drives **abstract test signals** ... instead
of real hardware, тому running unmodified на будь-якій board."

Good: "Рецепт навмисно керує **абстрактними тестовими сигналами** ...
замість реального обладнання, тому працює без змін на будь-якій платі."

Bad: "Most NTCs ship із datasheet values для B і R25."

Good: "Більшість NTC постачаються зі значеннями B та R25 у даташиті."

The rule: a Ukrainian noun gets a Ukrainian preposition and a
Ukrainian verb. Only technical identifiers (in `code formatting`)
remain English.

## Heading hierarchy

- `# H1` — page title only. One per file.
- `## H2` — major sections (3-6 per page typical).
- `### H3` — subsections within H2.
- `#### H4` — sub-subsections. Rare — if you need this often, the page
  is too dense.

Don't skip levels (H2 → H4). Use bold or list items for emphasis
instead of fake headings.

## Tables, lists, callouts

**Tables** for structured comparisons or reference data (API rows,
file roles). Don't fake tables with lists.

**Lists** for unordered sets or sequential steps. Numbered for
sequence, bullet for parallel options.

**Callouts** via blockquote:

```markdown
> ⚠️ **Warning:** describes a sharp edge.
> ℹ️ **Note:** clarifies context.
> 💡 **Tip:** a shortcut or best practice.
```

Use sparingly. Two per page max — they lose impact otherwise.

## Updating the index

Every new page lands in its language's `README.md` index. Don't add a
page without updating navigation, or readers won't find it.

## What this guide doesn't cover

- **API reference auto-gen:** out of scope. We write doc pages by hand.
- **Doxygen:** considered but not adopted (decision in
  `adr/0001-*` if/when written). C++ comments stay in headers;
  reference pages summarise and link back to headers.
- **Build pipelines:** these are plain Markdown. No mdBook / MkDocs.
  GitHub renders them natively.
- **Versioned docs:** docs match the current `main` branch. Older
  versions accessible through git tags.
