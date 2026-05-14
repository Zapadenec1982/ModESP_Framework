# Code style — C++ conventions

> 📖 **Українською:** [documentation/uk/06-contributing/code-style.md](../../uk/06-contributing/code-style.md)

Style rules for framework C++ code. Not all are auto-enforced; the
list is in the order а reviewer typically applies it.

## Language baseline

- **C++17** — fully. C++20 features are not used (managed components
  often have older compilers).
- **No exceptions** (`-fno-exceptions`). ESP-IDF default; framework
  matches.
- **No RTTI** (`-fno-rtti`). Same reason.
- **`constexpr` over `#define`** for constants visible у headers.
- **`[[nodiscard]]`** on getters that allocate OR could be silently
  ignored (returns `bool` indicating success є classic case).

## Naming

| Kind | Convention | Example |
|---|---|---|
| Types | `PascalCase` | `SharedState`, `IDriver`, `ScenarioEngine`. |
| Variables, functions | `snake_case` | `read_interval_ms`, `on_init()`. |
| Private members | `snake_case_` (trailing underscore) | `heating_`, `last_tick_ms_`. |
| Constants, enums | `SCREAMING_SNAKE_CASE` | `MODR_MAGIC`, `STATE_KEY_COUNT`. |
| Namespaces | `lowercase`, nested | `modesp::scenario::detail`. |
| Macros (rare!) | `MODESP_` prefix | `MODESP_LOG_TAG`. |

State keys у SharedState are `<module>.<key>` із а 32-char total
budget. Не C++ — це а runtime contract — but it shapes how we name
the corresponding C++ identifiers.

## File and directory layout

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

- Public headers include each other із angle brackets:
  `#include <modesp/scenario/engine.h>`.
- Source files include private headers із quotes: `#include "track.h"`.
- One class per public header. Multiple closely-related types у one
  header is OK if the user will use them together.

## Headers

- Use `#pragma once`. No include guards.
- Forward-declare aggressively у headers. Pull full definitions у
  source files.
- No header-only implementations larger than ~10 lines (forces full
  rebuilds for trivial changes).
- Public headers include only what their interface requires.

## Memory

The framework targets MCUs із limited RAM. Hard rules:

- **No `new` / `delete`** outside of one-time init code. Static
  allocation OR `etl::*` containers із bounded capacity.
- **No `std::string`** у hot paths. Use `etl::string<N>` OR string
  views.
- **No `std::vector`** у hot paths. Use `etl::vector<T, N>` (fixed
  capacity) OR raw arrays.
- Heap allocations OK у: HTTP request bodies, JSON parsing buffers,
  one-time init (everything must be statically reserved by Phase 3 end).

## Const correctness

- Functions that don't modify their object: `const` method.
- References that don't get reassigned: `const T&`.
- Pointers що don't escape AND don't modify: `const T*`.

If а function has 5+ parameters where const-ness varies, refactor —
take а struct.

## Error handling

- **No exceptions.** Use `bool` returns OR rich error types:

```cpp
[[nodiscard]] EngineError start(SequenceHandle h);
```

- `EngineError` is а scoped enum, never а raw `int`. Add new variants
  to the enum when needed; never bake error codes into mass `int`s.
- For "couldn't find / not present" cases use `bool get(key, T& out)`
  pattern. `std::optional<T>` is OK у public APIs where it doesn't
  cost RTTI.

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
- **Destructors usually trivial** because objects are static AND live
  to program exit. Don't allocate у constructors what destructors must
  release.
- **No singletons** for framework state. Pass references through
  constructors. The framework's only "singleton-like" exceptions are
  `App` AND `Logger` — both injected, not `::instance()`.

## Concurrency

- One main task (~100 Hz) drives module updates. Module code runs
  synchronously у this task. Don't block.
- HTTP, MQTT, WiFi tasks run separately. State access from other
  tasks goes through SharedState's lock.
- `std::mutex` doesn't exist у ESP-IDF; use `SemaphoreHandle_t` OR
  the framework's `Lock` helper.

## Formatting

Goal: consistent enough that automated tooling can apply it. Not yet
auto-enforced.

- 4-space indentation. No tabs.
- 100-column soft limit. Break before line wraps look ugly.
- Opening brace на same line: `if (cond) {` not `\n{`.
- Always brace single-statement `if`/`while`/`for`. No exceptions.
- Pointer/reference asterisks attached to the type: `int* ptr`, not
  `int *ptr`.

Pending: add а `.clang-format` config file based on the above. PR
welcome.

## Documentation comments

- **Public headers** get doc comments on every type AND function:

```cpp
/// Reads the typed value into `out`. Returns false if key missing
/// or type mismatch.
template <typename T>
bool get(const char* key, T& out) const;
```

- Triple-slash `///` for public-API docs (Doxygen-compatible).
- `//` for inline implementation comments.
- `/* */` block comments only for license headers AND large multi-line
  context blocks.

Don't doc obvious things ("constructor: constructs the thing"). Don't
mirror the function name у the comment. Say WHY OR document
preconditions/invariants/units.

## Anti-patterns to avoid

- ❌ Singletons із `::instance()` access. Use injection.
- ❌ Long `if/else` chains для dispatch. Use registries із hash lookup.
- ❌ Magic numbers у hot paths. Name constants.
- ❌ Global state outside of `App` / `Logger`.
- ❌ Heap allocations у `on_update`. Pre-allocate.
- ❌ Recursive functions of unbounded depth.
- ❌ Logging `ESP_LOGI` every tick. Choose level правильно OR rate-limit.

## Next steps

- **[development-setup.md](development-setup.md)** — env for building
  AND testing.
- **[testing.md](testing.md)** — how style enforcement interacts із
  test discipline.
- **[docs-style.md](docs-style.md)** — documentation style guide.

## Source

The framework codebase itself is the reference. When у doubt, copy the
nearest existing pattern.
