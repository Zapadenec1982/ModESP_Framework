# `drivers_sync.py` — reconcile menuconfig with the board

> 📖 **Українською:** [documentation/uk/05-tools/drivers_sync.md](../../uk/05-tools/drivers_sync.md)

A deliberately-run helper that aligns the per-driver menuconfig toggles
(`CONFIG_MODESP_DRIVER_<NAME>`) with what the **active board's** `bindings.json`
actually uses. It is the one-command fix for the build error you get when a board
binds a driver that's disabled in menuconfig.

It does **not** run during `idf.py build` — the build is a non-interactive
fail-fast gate. This tool is where the interactive enable/disable + auto-edit
lives, run on purpose.

## What it does

1. Reads `data/board.json`, `data/bindings.json`, all `drivers/*/manifest.json`,
   and `sdkconfig`.
2. **Validates** the bindings first (same checks as the build); if they're
   invalid, it reports and changes nothing.
3. Computes two sets:
   - **Bound but disabled** — the board binds the driver, but
     `CONFIG_MODESP_DRIVER_<NAME>` is off → proposes to **enable**.
   - **Enabled but unused** — compiled but no binding uses it → proposes to
     **disable** (shrinks the binary). **Discovery-capable drivers** (e.g.
     `ds18b20`, which you scan *before* adding bindings) are excluded.
4. Applies the change by editing `sdkconfig` **directly** (the value, not a
   Kconfig default). A plain `idf.py build` then applies it — **no `fullclean`**
   needed for a value change.

## Usage

```bash
python tools/drivers_sync.py                 # interactive: prompt per change
python tools/drivers_sync.py --dry-run       # show the diff, change nothing
python tools/drivers_sync.py --fix --yes     # enable bound-but-disabled, no prompts
python tools/drivers_sync.py --fix --prune --yes   # also disable unused
python tools/drivers_sync.py --fix --yes --rebuild # then run idf.py build
```

| Flag | Effect |
|---|---|
| `--fix` | enable drivers the board binds but are disabled |
| `--prune` | additionally disable drivers enabled but unused by the board |
| `--yes` | apply without prompting (for scripts/CI) |
| `--dry-run` | report only; never writes |
| `--rebuild` | run `idf.py build` after applying (needs an ESP-IDF-activated shell) |
| `--data-dir`, `--drivers-dir`, `--sdkconfig` | override default paths |

With no flags it is interactive; `--fix` is implied if neither `--fix` nor
`--prune` is given.

## Relationship to the build

| Layer | Behaviour |
|---|---|
| `idf.py build` (gate) | If a bound driver is disabled → **FATAL** with a message naming this tool. Validates bindings. Prints an advisory `INFO:` list of unused-but-enabled drivers. |
| `drivers_sync.py` (fix) | Interactive/auto enable+disable, edits `sdkconfig`, optional rebuild. |

The gate lives in `components/modesp_hal/CMakeLists.txt` (after
`idf_component_register`, so it runs only in the real config phase where
`CONFIG_*` are defined). The bound-driver set comes from
`generated/required_drivers.cmake`, which the generator derives from the active
board's bindings.

## Notes

- `sdkconfig` is gitignored and per-developer; this tool edits *your* copy.
- A *value* change (enable/disable an existing driver) needs only `idf.py build`.
  `fullclean` is only needed when a **brand-new driver folder** is added (it
  introduces a new component Kconfig file that ESP-IDF caches).
- The tool reuses `validate_bindings`/`unused_drivers` from
  [`generate_ui.py`](generate_ui.md), so its rules match the build exactly.

## Source

- [`tools/drivers_sync.py`](../../../tools/drivers_sync.py)
- [04-hardware/bindings.md](../04-hardware/bindings.md) — the validation rules.
- [02-module-author-guide/writing-a-driver.md](../02-module-author-guide/writing-a-driver.md) — drivers + menuconfig.
