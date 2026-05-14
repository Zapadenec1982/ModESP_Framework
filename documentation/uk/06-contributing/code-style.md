# Code style — C++ conventions

> 📖 **In English:** [documentation/en/06-contributing/code-style.md](../../en/06-contributing/code-style.md)

Style rules для framework C++ code. Не всі auto-enforced; list
у order у якому reviewer typically applies it.

## Language baseline

- **C++17** — fully. C++20 features не used (managed components
  often have older compilers).
- **No exceptions** (`-fno-exceptions`). ESP-IDF default; framework
  matches.
- **No RTTI** (`-fno-rtti`). Same reason.
- **`constexpr` over `#define`** для constants visible у headers.
- **`[[nodiscard]]`** на getters що allocate АБО could be silently
  ignored (returns `bool` indicating success — classic case).

## Naming

| Kind | Convention | Example |
|---|---|---|
| Types | `PascalCase` | `SharedState`, `IDriver`, `ScenarioEngine`. |
| Variables, functions | `snake_case` | `read_interval_ms`, `on_init()`. |
| Private members | `snake_case_` (trailing underscore) | `heating_`, `last_tick_ms_`. |
| Constants, enums | `SCREAMING_SNAKE_CASE` | `MODR_MAGIC`, `STATE_KEY_COUNT`. |
| Namespaces | `lowercase`, nested | `modesp::scenario::detail`. |
| Macros (rare!) | `MODESP_` prefix | `MODESP_LOG_TAG`. |

State keys у SharedState — `<module>.<key>` з 32-char total
budget. Не C++ — це runtime contract — але shapes як ми name
corresponding C++ identifiers.

## File і directory layout

```
components/modesp_<name>/
├── CMakeLists.txt
├── idf_component.yml
├── include/modesp/<name>/    ← PUBLIC headers (other components #include these)
│   └── *.h
├── private/                  ← PRIV_INCLUDE_DIRS (component-internal)
│   └── *.h
└── src/
    └── *.cpp
```

- Public headers include each other з angle brackets:
  `#include <modesp/scenario/engine.h>`.
- Source files include private headers з quotes: `#include "track.h"`.
- One class per public header. Multiple closely-related types у one
  header OK якщо user буде use them together.

## Headers

- Use `#pragma once`. No include guards.
- Forward-declare aggressively у headers. Pull full definitions у
  source files.
- No header-only implementations larger than ~10 lines (forces full
  rebuilds для trivial changes).
- Public headers include only що their interface requires.

## Memory

Фреймворк targets MCUs з limited RAM. Hard rules:

- **No `new` / `delete`** outside of one-time init code. Static
  allocation АБО `etl::*` containers з bounded capacity.
- **No `std::string`** у hot paths. Use `etl::string<N>` АБО string
  views.
- **No `std::vector`** у hot paths. Use `etl::vector<T, N>` (fixed
  capacity) АБО raw arrays.
- Heap allocations OK у: HTTP request bodies, JSON parsing buffers,
  one-time init (everything must be statically reserved by Phase 3 end).

## Const correctness

- Functions що don't modify their object: `const` method.
- References що don't get reassigned: `const T&`.
- Pointers що don't escape І don't modify: `const T*`.

Якщо function has 5+ parameters де const-ness varies, refactor —
take struct.

## Error handling

- **No exceptions.** Use `bool` returns АБО rich error types:

```cpp
[[nodiscard]] EngineError start(SequenceHandle h);
```

- `EngineError` — scoped enum, ніколи raw `int`. Add new variants
  до enum коли needed; ніколи bake error codes у mass `int`s.
- Для "couldn't find / not present" cases use `bool get(key, T& out)`
  pattern. `std::optional<T>` OK у public APIs де не cost RTTI.

## Logging

```cpp
ESP_LOGI(TAG, "scenario started: handle=%d", h);
ESP_LOGW(TAG, "throttled NVS write: dt=%dms", dt);
ESP_LOGE(TAG, "load failed: %s", error_name(err));
```

- `TAG` per-file: `static const char* TAG = "scenario";`.
- Level discipline:
  - `ESP_LOGE`: failure user must fix.
  - `ESP_LOGW`: degraded behaviour, recovered.
  - `ESP_LOGI`: lifecycle (init, start, stop). Quiet during steady state.
  - `ESP_LOGD`: per-tick noise. Off у production.

Avoid `printf` direct. ESP-IDF logging level filtering doesn't apply.

## Initialization, destructors, lifetime

- **Two-phase init.** Constructors stay trivial (just record dependencies).
  Heavy work goes у `on_init()` called by `ModuleManager`.
- **Destructors usually trivial** because objects static І live
  до program exit. Don't allocate у constructors що destructors must
  release.
- **No singletons** для framework state. Pass references through
  constructors. Framework's only "singleton-like" exceptions —
  `App` І `Logger` — both injected, не `::instance()`.

## Concurrency

- One main task (~100 Hz) drives module updates. Module code runs
  synchronously у цій task. Don't block.
- HTTP, MQTT, WiFi tasks run separately. State access з other
  tasks goes through SharedState lock.
- `std::mutex` не існує у ESP-IDF; use `SemaphoreHandle_t` АБО
  framework's `Lock` helper.

## Formatting

Goal: consistent enough що automated tooling can apply it. Не yet
auto-enforced.

- 4-space indentation. No tabs.
- 100-column soft limit. Break перед line wraps look ugly.
- Opening brace на same line: `if (cond) {` не `\n{`.
- Завжди brace single-statement `if`/`while`/`for`. Без exceptions.
- Pointer/reference asterisks attached до type: `int* ptr`, not
  `int *ptr`.

Pending: add `.clang-format` config file based на вищенаведеному. PR
welcome.

## Documentation comments

- **Public headers** get doc comments на every type І function:

```cpp
/// Reads the typed value into `out`. Returns false if key missing
/// or type mismatch.
template <typename T>
bool get(const char* key, T& out) const;
```

- Triple-slash `///` для public-API docs (Doxygen-compatible).
- `//` для inline implementation comments.
- `/* */` block comments тільки для license headers І large multi-line
  context blocks.

Не doc obvious things ("constructor: constructs the thing"). Не
mirror function name у comment. Say WHY АБО document
preconditions/invariants/units.

## Anti-patterns to avoid

- ❌ Singletons з `::instance()` access. Use injection.
- ❌ Long `if/else` chains для dispatch. Use registries з hash lookup.
- ❌ Magic numbers у hot paths. Name constants.
- ❌ Global state outside of `App` / `Logger`.
- ❌ Heap allocations у `on_update`. Pre-allocate.
- ❌ Recursive functions з unbounded depth.
- ❌ Logging `ESP_LOGI` every tick. Choose level правильно АБО rate-limit.

## Що далі

- **[development-setup.md](development-setup.md)** — env для building
  І testing.
- **[testing.md](testing.md)** — як style enforcement interacts з
  test discipline.
- **[docs-style.md](docs-style.md)** — documentation style guide.

## Source

Framework codebase сам — reference. Коли у doubt, copy
nearest existing pattern.
