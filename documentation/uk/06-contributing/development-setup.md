# Development setup

> 📖 **In English:** [documentation/en/06-contributing/development-setup.md](../../en/06-contributing/development-setup.md)

Ця сторінка для **framework contributors**. Якщо пишете business
modules на top of фреймворку, див. **[01-getting-started/installation.md](../01-getting-started/installation.md)**
instead.

## Prerequisites — same як module author setup

- ESP-IDF v5.x toolchain (див. installation guide).
- Python 3.8+ з `tools/requirements.txt` installed.
- Git з submodule support.
- Optional: clang-format для C++ style enforcement.

Additional contributor needs:

- ESP32 device для HIL testing (required щоб validate engine changes).
- USB-serial reliably connected (cheap CH340 adapters can fail at high
  baud rates — див. deployment.md).
- Linux/macOS host strongly recommended для running host tests; test
  harness builds з standard `gcc` rather than xtensa toolchain.

## Repository layout для contributors

```
modesp-v4/
├── components/         ← framework libraries (тут ви будете working)
│   ├── modesp_core/
│   ├── modesp_hal/
│   ├── modesp_services/
│   ├── modesp_net/
│   ├── modesp_mqtt/
│   ├── modesp_aws/
│   ├── modesp_json/
│   └── modesp_scenario/
├── modules/            ← reference business modules (rarely touched)
├── drivers/            ← hardware drivers (touch при adding new device)
├── main/               ← module wiring; touch при adding new system service
├── tools/              ← build-time generators І host test fixtures
├── data/               ← static assets (WebUI, recipes)
├── documentation/      ← docs які ви reading
└── docs/               ← legacy reference (read-only; do not modify)
```

**Не add new files до `docs/`** — це pre-rewrite directory.
Вся нова documentation goes у `documentation/`.

## Branching і PR workflow

- `main` — release branch. Direct pushes blocked. Only fast-forward
  merges з feature branches АБО fully-tested rebuilds.
- Feature branches: `claude/<descriptive-name>` для AI-assisted work
  АБО `<your-handle>/<descriptive-name>` для manual.
- Один PR = one feature АБО one bug fix. Avoid mixing.
- PR title format: `<area>: <imperative-summary>` (наприклад
  `scenario: NVS observer + magic bump`).

## Build цикл

Two builds matter:

**Firmware build:**

```
idf.py build
idf.py -p COM4 flash flash_data monitor
```

Touches один ESP-IDF component → rebuilds той component plus
dependents. Usually 5-30 s incremental.

**Host tests build:**

```
cd components/modesp_scenario/tests/host
make
./test_engine
```

Runs entirely на host. No device needed. Кожен component з
`tests/host/` directory has own Makefile І `stub_state_backend.h`
fixture. Use ці для fast iteration на logic-only changes.

## HIL tests

Required для PRs що touch scenario engine, persistence, АБО HTTP
surface. Runs проти live device:

```
$env:ESP_IP="192.168.4.1"     # PowerShell
$env:ESP_USER="admin"
$env:ESP_PASS="modesp"
python -m pytest tools/tests/test_hil_scenario.py -v
```

Six tests cover scenario engine end-to-end. Усі must pass перед merge.

## Adding new framework component

1. Create `components/modesp_<name>/` з `CMakeLists.txt`,
   `idf_component.yml`, `include/modesp/<name>/`, І `src/`.
2. Declare dependencies у `idf_component.yml`.
3. Wire instantiation у `main/main.cpp` (manual DI; no auto-registration).
4. Add documentation page у `documentation/{en,uk}/03-framework-reference/components/modesp_<name>.md`.
5. Add host tests у `components/modesp_<name>/tests/host/`.

Module Author Guide's **writing-a-module.md** для module authors,
не framework contributors. Framework components не subclass
`BaseModule` necessarily — вони full peers existing components.

## Adding new driver

1. Create `drivers/<name>/` з `manifest.json`,
   `include/<name>_driver.h`, І `src/<name>_driver.cpp`.
2. Implement `IDriver` (typically `ISensorDriver` АБО `IActuatorDriver`).
3. Register у `project.json` `drivers:` list.
4. Add documentation у `documentation/{en,uk}/03-framework-reference/drivers/<name>.md`.
5. Test із physical device — HAL drivers can't be host-tested meaningfully.

## Dependencies І component registry

External components live у `managed_components/` І pulled by
ESP-IDF component manager з manifest declarations. Не add files
до `managed_components/` manually. Щоб pin version:

```yaml
# idf_component.yml
dependencies:
  marcel-cd/etlcpp:
    version: "1.0.1"
```

Run `idf.py reconfigure` після changing dependencies.

## Common pitfalls

**Forgot rebuild after manifest change:** CMake invokes generators
automatically на incremental builds, але cached state can
become stale після major edits. Run `idf.py fullclean` якщо бачите
inconsistent state.

**Host tests pass але device fails:** stub backend permissive.
Real SharedState rejects type changes; check `test_engine` І HIL pytest
together.

**Pull request з unstaged WebUI changes:** WebUI bundle (`data/www/`)
committed до git але built separately. Якщо не run WebUI
build, leave `data/www/` alone.

**Submodule drift:** якщо `git status` shows submodules як modified,
likely unintended ESP-IDF/managed_components state change. Run
`git submodule update --init --recursive`.

## Що далі

- **[testing.md](testing.md)** — host І HIL test details.
- **[code-style.md](code-style.md)** — C++ conventions.
- **[docs-style.md](docs-style.md)** — documentation style guide.

## Source

Ця page — roll-up operational knowledge gathered з ModESP rebuild.
Файли:

- `tools/tests/test_hil_scenario.py` — HIL pytest fixture.
- `components/*/tests/host/Makefile` — host test pattern.
- ESP-IDF [Build System docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html).
