# Testing — host, HIL, fuzz

> 📖 **In English:** [documentation/en/06-contributing/testing.md](../../en/06-contributing/testing.md)

Фреймворк uses three levels of tests з distinct purposes:

| Level | Where | Що validates |
|---|---|---|
| **Host** | `components/*/tests/host/` | Pure C++ logic — engine FSM, action registry, parsing. |
| **HIL** | `tools/tests/test_hil_*.py` | Live firmware на real ESP32 — end-to-end behaviour. |
| **Fuzz** | `tools/tests/fuzz_*` (планується) | Binary format edge cases, malformed inputs. |

Кожен PR що touches framework code should land з at least host-level
test changes. PRs що change engine АБО persistence MUST pass HIL.

## Host tests

Кожен component з `tests/host/` directory has:

```
tests/host/
├── Makefile                # builds з standard gcc на host
├── common/
│   └── stub_state_backend.h  # shared fixtures
└── test_<feature>.cpp      # one file per concern
```

Run з component root:

```
cd components/modesp_scenario/tests/host
make
./test_engine
./test_actions
./test_continuous
./test_arbiter
./test_nvs_token
./test_nvs_observer
```

Або build all at once:

```
make all
make run    # builds + runs every test_* binary
```

Tests use **plain `assert()`** — no Google Test, no Catch2. Goal:
zero external dependencies, builds anywhere з C++ compiler.

Test file looks так:

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

Failing assertions abort з core dump; CI grep'ає для `Aborted`.

### Stub backend

`stub_state_backend.h` provides:

- `get_raw / set_raw` проти `std::unordered_map<std::string, StateValue>`.
- No type validation (production SharedState enforces; stub does not —
  add у test якщо need it).
- `last_writes()` helper для asserting які keys tested unit wrote.

### Коли писати host test

- ✅ Pure function — parsing, hashing, validation.
- ✅ FSM transitions — feed events, assert state.
- ✅ Registry behaviour — register, lookup, unregister.
- ❌ Hardware interaction — drivers, ADC, GPIO.
- ❌ Network protocols (HTTP, MQTT) — HIL coverage.
- ❌ WebUI rendering — manual test.

## HIL tests

`tools/tests/test_hil_scenario.py` runs проти live device. Six
tests cover scenario engine end-to-end:

1. Single-instance load + run.
2. Multi-instance (same recipe loaded twice).
3. Resource contention.
4. Global transition fault injection.
5. Power-cycle recovery.
6. WebUI live mirror updates.

Configuration через environment:

```
$env:ESP_IP="192.168.4.1"   # device IP АБО mDNS name
$env:ESP_USER="admin"
$env:ESP_PASS="modesp"
python -m pytest tools/tests/test_hil_scenario.py -v
```

(`ESP_*` env vars read by fixture. Defaults: 192.168.4.1, admin,
modesp.)

Pytest fixture:

- Authenticates через HTTP Basic.
- POSTs scenario commands.
- Polls SharedState через GET `/api/state`.
- Verifies state transitions з timing constraints.

Expected runtime: ~3-5 minutes для всі six tests.

### Коли писати HIL test

- New scenario engine feature (transitions, completion rules).
- New persistence behaviour (NVS, observer hooks).
- New HTTP endpoint що affects scenario lifecycle.
- Bug fix що can only manifest на real hardware (timing, NVS, GPIO).

### Коли НЕ писати HIL test

- Pure FSM logic — host test faster І more deterministic.
- Driver behaviour — needs physical hardware varieties, не single ESP32.

## Fuzz tests (planned)

Stage 2 буде introduce:

- `tools/tests/fuzz_modr.py` — feeds malformed `.modr` binaries до
  loader, asserts no crash І clean rejection з defined error code.
- `tools/tests/fuzz_manifests/` — corpus malformed manifests щоб
  exercise `generate_ui.py` І `compile_scenario.py` error paths.

Currently не implemented; track у roadmap.

## CI

GitHub Actions runs:

- `idf.py build` для всі supported targets (esp32, esp32s3, esp32c3).
- Host tests для всі components з `tests/host/` directory.

HIL tests run nightly АБО on-demand з PR comment. Вони require
physical device на runner — currently self-hosted machine.

## Common pitfalls

**Test passes locally, fails CI:** different gcc version або stdlib.
Host tests target C++17 з no extensions. Avoid platform-specific
intrinsics.

**Flaky HIL tests:** WiFi instability АБО slow USB-serial flash. Re-run
twice; якщо still flaky — file issue з log.

**Test assertions abort без message:** plain `assert()` doesn't print
context. Wrap з helper:

```cpp
#define ASSERT_EQ(a, b) do { \
    if (!((a) == (b))) { \
        fprintf(stderr, "FAIL %s:%d: %s != %s (%d != %d)\n", \
                __FILE__, __LINE__, #a, #b, (int)(a), (int)(b)); \
        abort(); \
    } \
} while (0)
```

**Test depends on order:** ніколи. Кожен test_*.cpp file independent.
Якщо need shared setup, put it у `common/`.

## Що далі

- **[development-setup.md](development-setup.md)** — environment що
  makes ці tests runnable.
- **[code-style.md](code-style.md)** — C++ conventions які tests follow.
- **[02-module-author-guide/debugging.md](../02-module-author-guide/debugging.md)** —
  debugging running device коли HIL fails.

## Source

- `components/*/tests/host/` — host test directories.
- `tools/tests/test_hil_scenario.py` — HIL fixture.
- `.github/workflows/` — CI configuration.
