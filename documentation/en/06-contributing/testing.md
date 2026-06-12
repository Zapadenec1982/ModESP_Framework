# Testing — host, HIL, fuzz

> 📖 **Українською:** [documentation/uk/06-contributing/testing.md](../../uk/06-contributing/testing.md)

The framework uses three levels of tests із distinct purposes:

| Level | Where | What it validates |
|---|---|---|
| **Host** | `components/*/tests/host/` | Pure C++ logic — engine FSM, action registry, parsing. |
| **HIL** | `tools/tests/test_hil_*.py` | Live firmware on real ESP32 — end-to-end behaviour. |
| **Fuzz** | `tools/tests/fuzz_*` (планується) | Binary format edge cases, malformed inputs. |

Every PR що touches framework code should land із at least host-level
test changes. PRs що change the engine OR persistence MUST pass HIL.

## Host tests

Each component із а `tests/host/` directory has:

```
tests/host/
├── Makefile                # builds із standard gcc on the host
├── common/
│   └── stub_state_backend.h  # shared fixtures
└── test_<feature>.cpp      # one file per concern
```

Run from the component root (binaries land у `build/`):

```
cd components/modesp_scenario/tests/host
make                       # builds all test binaries into build/
build/test_action_registry
build/test_nvs_token
build/test_nvs_observer
```

Or build AND run every test із once:

```
make test    # builds all, runs each binary, fails on first non-zero exit
```

Tests use **plain `assert()`** — no Google Test, no Catch2. Goal:
zero external dependencies, builds anywhere із а C++ compiler.

A test file looks like this:

```cpp
#include <cassert>
#include "modesp/scenario/engine.h"
#include "common/stub_state_backend.h"

int main() {
    StubStateBackend state;
    ActionRegistry actions;
    ContinuousRegistry cont;
    Engine engine{state, actions, cont, {}};

    // ... arrange, act, assert ...

    assert(engine.scenario_state(h) == ScenarioState::COMPLETED);
    return 0;
}
```

Failing assertions abort із core dump; CI grep'aing для `Aborted`.

### Stub backend

`stub_state_backend.h` provides:

- `get_raw / set_raw` against an `std::unordered_map<std::string, StateValue>`.
- No type validation (production SharedState enforces; stub does not —
  add із test if you need it).
- `last_writes()` helper for asserting which keys а tested unit wrote.

### When to write а host test

- ✅ Pure function — parsing, hashing, validation.
- ✅ FSM transitions — feed events, assert state.
- ✅ Registry behaviour — register, lookup, unregister.
- ❌ Hardware interaction — drivers, ADC, GPIO.
- ❌ Network protocols (HTTP, MQTT) — HIL coverage.
- ❌ WebUI rendering — manual test.

## HIL tests

`tools/tests/test_hil_scenario.py` runs against а live device. Six
tests cover the scenario engine end-to-end:

1. Single-instance load + run.
2. Multi-instance (same recipe loaded twice).
3. Resource contention.
4. Global transition fault injection.
5. Power-cycle recovery.
6. WebUI live mirror updates.

Configuration via environment:

```
$env:ESP_IP="192.168.4.1"   # device IP OR mDNS name
$env:ESP_USER="admin"
$env:ESP_PASS="modesp"
python -m pytest tools/tests/test_hil_scenario.py -v
```

(`ESP_*` env vars are read by the fixture. Defaults: 192.168.4.1, admin,
modesp.)

The pytest fixture:

- Authenticates via HTTP Basic.
- POSTs scenario commands.
- Polls SharedState через GET `/api/state`.
- Verifies state transitions із timing constraints.

Expected runtime: ~3-5 minutes для всі six tests.

### When to write а HIL test

- New scenario engine feature (transitions, completion rules).
- New persistence behaviour (NVS, observer hooks).
- New HTTP endpoint що affects scenario lifecycle.
- Bug fix що can only manifest on real hardware (timing, NVS, GPIO).

### When NOT to write а HIL test

- Pure FSM logic — host test is faster AND more deterministic.
- Driver behaviour — needs physical hardware varieties, not а single ESP32.

## Fuzz tests (planned)

Stage 2 will introduce:

- `tools/tests/fuzz_modr.py` — feeds malformed `.modr` binaries to the
  loader, asserts no crash AND clean rejection із а defined error code.
- `tools/tests/fuzz_manifests/` — corpus of malformed manifests to
  exercise `generate_ui.py` AND `compile_scenario.py` error paths.

Currently not implemented; track у the roadmap.

## CI

GitHub Actions runs:

- `idf.py build` for all supported targets (esp32, esp32s3, esp32c3).
- Host tests for all components із а `tests/host/` directory.

HIL tests run nightly OR on-demand із PR comment. They require а
physical device on the runner — currently а self-hosted machine.

## Common pitfalls

**Test passes locally, fails CI:** different gcc version or stdlib.
Host tests target C++17 із no extensions. Avoid platform-specific
intrinsics.

**Flaky HIL tests:** WiFi instability OR slow USB-serial flash. Re-run
twice; if still flaky, file an issue із the log.

**Test assertions abort із no message:** plain `assert()` doesn't print
context. Wrap із а helper:

```cpp
#define ASSERT_EQ(a, b) do { \
    if (!((a) == (b))) { \
        fprintf(stderr, "FAIL %s:%d: %s != %s (%d != %d)\n", \
                __FILE__, __LINE__, #a, #b, (int)(a), (int)(b)); \
        abort(); \
    } \
} while (0)
```

**Test depends on order:** never. Each test_*.cpp file is independent.
If you need shared setup, put it у `common/`.

## Next steps

- **[development-setup.md](development-setup.md)** — environment that
  makes these tests runnable.
- **[code-style.md](code-style.md)** — C++ conventions tests follow.
- **[02-module-author-guide/debugging.md](../02-module-author-guide/debugging.md)** —
  debugging the running device when HIL fails.

## Source

- `components/*/tests/host/` — host test directories.
- `tools/tests/test_hil_scenario.py` — HIL fixture.
- `.github/workflows/` — CI configuration.
