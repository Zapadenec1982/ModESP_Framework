# Development setup

> 📖 **Українською:** [documentation/uk/06-contributing/development-setup.md](../../uk/06-contributing/development-setup.md)

This page is for **framework contributors**. If you're writing business
modules on top of the framework, see **[01-getting-started/installation.md](../01-getting-started/installation.md)**
instead.

## Prerequisites — same as the module author setup

- ESP-IDF v5.x toolchain (see installation guide).
- Python 3.8+ із `tools/requirements.txt` installed.
- Git із submodule support.
- Optional: clang-format для C++ style enforcement.

Additional contributor needs:

- An ESP32 device для HIL testing (required to validate engine changes).
- USB-serial reliably connected (cheap CH340 adapters can fail at high
  baud rates — see deployment.md).
- А Linux/macOS host strongly recommended для running host tests; the
  test harness builds із standard `gcc` rather than xtensa toolchain.

## Repository layout for contributors

```
modesp-v4/
├── components/         ← framework libraries (this is where you'll work)
│   ├── modesp_core/
│   ├── modesp_hal/
│   ├── modesp_services/
│   ├── modesp_net/
│   ├── modesp_mqtt/
│   ├── modesp_aws/
│   ├── modesp_json/
│   └── modesp_scenario/
├── modules/            ← reference business modules (rarely touched)
├── drivers/            ← hardware drivers (touch when adding а new device)
├── main/               ← module wiring; touch when adding а new system service
├── tools/              ← build-time generators AND host test fixtures
├── data/               ← static assets (WebUI, recipes)
├── documentation/      ← the docs you're reading
└── docs/               ← legacy reference (read-only; do not modify)
```

**Don't add new files to `docs/`** — it's the pre-rewrite directory.
All new documentation goes у `documentation/`.

## Branching і PR workflow

- `main` — release branch. Direct pushes blocked. Only fast-forward
  merges from feature branches OR fully-tested rebuilds.
- Feature branches: `claude/<descriptive-name>` for AI-assisted work
  OR `<your-handle>/<descriptive-name>` for manual.
- One PR = one feature OR one bug fix. Avoid mixing.
- PR title format: `<area>: <imperative-summary>` (e.g.
  `scenario: NVS observer + magic bump`).

## Build цикл

Two builds matter:

**Firmware build:**

```
idf.py build
idf.py -p COM4 flash flash_data monitor
```

Touches one ESP-IDF component → rebuilds that component plus
dependents. Usually 5-30 s incremental.

**Host tests build:**

```
cd components/modesp_scenario/tests/host
make
./test_engine
```

Runs entirely on the host. No device needed. Each component із а
`tests/host/` directory has its own Makefile AND `stub_state_backend.h`
fixture. Use these for fast iteration on logic-only changes.

## HIL tests

Required для PRs that touch the scenario engine, persistence, OR HTTP
surface. Runs against а live device:

```
$env:ESP_IP="192.168.4.1"     # PowerShell
$env:ESP_USER="admin"
$env:ESP_PASS="modesp"
python -m pytest tools/tests/test_hil_scenario.py -v
```

Six tests cover scenario engine end-to-end. All must pass before merge.

## Adding а new framework component

1. Create `components/modesp_<name>/` із `CMakeLists.txt`,
   `idf_component.yml`, `include/modesp/<name>/`, AND `src/`.
2. Declare dependencies у `idf_component.yml`.
3. Wire instantiation у `main/main.cpp` (manual DI; no auto-registration).
4. Add documentation page у `documentation/{en,uk}/03-framework-reference/components/modesp_<name>.md`.
5. Add host tests у `components/modesp_<name>/tests/host/`.

The Module Author Guide's **writing-a-module.md** is for module authors,
not framework contributors. Framework components don't subclass
`BaseModule` necessarily — they're full peers of existing components.

## Adding а new driver

1. Create `drivers/<name>/` із `manifest.json`,
   `include/<name>_driver.h`, AND `src/<name>_driver.cpp`.
2. Implement `IDriver` (typically `ISensorDriver` OR `IActuatorDriver`).
3. Register у `project.json` `drivers:` list.
4. Add documentation у `documentation/{en,uk}/03-framework-reference/drivers/<name>.md`.
5. Test із а physical device — HAL drivers can't be host-tested meaningfully.

## Dependencies AND component registry

External components live у `managed_components/` AND are pulled by
ESP-IDF's component manager from manifest declarations. Don't add files
to `managed_components/` manually. To pin а version:

```yaml
# idf_component.yml
dependencies:
  marcel-cd/etlcpp:
    version: "1.0.1"
```

Run `idf.py reconfigure` після changing dependencies.

## Common pitfalls

**Forgot to rebuild after manifest change:** CMake invokes the
generators automatically on incremental builds, but cached state can
become stale after major edits. Run `idf.py fullclean` if you see
inconsistent state.

**Host tests pass but device fails:** the stub backend is permissive.
Real SharedState rejects type changes; check `test_engine` AND HIL pytest
together.

**Pull request із unstaged WebUI changes:** the WebUI bundle (`data/www/`)
is committed to git but built separately. If you didn't run the WebUI
build, leave `data/www/` alone.

**Submodule drift:** if `git status` shows submodules как modified,
likely an unintended ESP-IDF/managed_components state change. Run
`git submodule update --init --recursive`.

## Next steps

- **[testing.md](testing.md)** — host AND HIL test details.
- **[code-style.md](code-style.md)** — C++ conventions.
- **[docs-style.md](docs-style.md)** — documentation style guide.

## Source

This page is а roll-up of operational knowledge gathered from the
ModESP rebuild. Файли:

- `tools/tests/test_hil_scenario.py` — HIL pytest fixture.
- `components/*/tests/host/Makefile` — host test pattern.
- ESP-IDF [Build System docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html).
