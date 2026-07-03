#!/usr/bin/env python3
"""drivers_sync.py — reconcile menuconfig driver toggles with the active board.

Reads the active board's data/board.json + data/bindings.json, all
drivers/*/manifest.json, and sdkconfig, then:
  * ENABLES drivers the board binds but that are disabled in menuconfig
    (otherwise the binding silently no-ops at runtime — and `idf.py build`
    already FATAL-errors on this; this tool is the one-command fix).
  * with --prune, DISABLES drivers that are enabled but unused by this board,
    EXCLUDING discovery-capable drivers (e.g. ds18b20) which are needed to scan
    for devices before any binding exists.

It edits sdkconfig DIRECTLY (writing the value, not a Kconfig default), so a
plain `idf.py build` applies the change — no `fullclean` needed for value edits.
Validation (the same checks `idf.py build` runs) is performed first; a broken
bindings set is reported and nothing is changed.

Usage:
    python tools/drivers_sync.py                 # interactive
    python tools/drivers_sync.py --dry-run       # show, change nothing
    python tools/drivers_sync.py --fix --yes     # enable bound-but-disabled, no prompts
    python tools/drivers_sync.py --fix --prune --yes
    python tools/drivers_sync.py --fix --yes --rebuild   # then run idf.py build (needs IDF shell)
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from generate_ui import validate_bindings, load_all_driver_manifests, unused_drivers  # noqa: E402


def cfg_symbol(name: str) -> str:
    return f"CONFIG_MODESP_DRIVER_{name.upper()}"


# Transport master switches — mirror of HW_TYPE_KCONFIG_DEPS in generate_ui.py:
# a driver whose manifest hardware_type appears here gets 'depends on <symbol>'
# in the generated Kconfig, so with the master OFF its own symbol is HIDDEN
# (absent from sdkconfig) and setting it =y alone would be ignored by kconfig.
# Value: (symbol, Kconfig default when the symbol is absent from sdkconfig).
MASTER_SWITCHES = {"ble": ("CONFIG_MODESP_BLE_ENABLE", False)}


def read_symbol(sdk_path: Path, symbol: str, default: bool) -> bool:
    """Resolve an arbitrary bool symbol from sdkconfig (absent → its default)."""
    if not sdk_path.exists():
        return default
    for raw in sdk_path.read_text(encoding="utf-8").splitlines():
        s = raw.strip()
        if s == f"{symbol}=y":
            return True
        if s == f"# {symbol} is not set":
            return False
    return default


def read_driver_config(sdk_path: Path) -> dict:
    """{symbol: True/False} for explicit MODESP_DRIVER_* lines in sdkconfig.

    A symbol absent from sdkconfig is at its Kconfig default (y) — callers treat
    `None` (absent) as enabled.
    """
    state = {}
    if not sdk_path.exists():
        return state
    for raw in sdk_path.read_text(encoding="utf-8").splitlines():
        s = raw.strip()
        if s.startswith("CONFIG_MODESP_DRIVER_") and s.endswith("=y"):
            state[s[:-2]] = True
        elif s.startswith("# CONFIG_MODESP_DRIVER_") and s.endswith(" is not set"):
            state[s[2:-len(" is not set")]] = False
    return state


def is_enabled(state: dict, name: str) -> bool:
    """Enabled if explicitly =y OR absent (Kconfig default y). Disabled only if
    explicitly 'is not set'."""
    return state.get(cfg_symbol(name)) is not False


def set_config(sdk_path: Path, symbol: str, enable: bool) -> bool:
    """Edit sdkconfig so `symbol` is enabled/disabled. Returns True if file changed."""
    enabled_line = f"{symbol}=y"
    disabled_line = f"# {symbol} is not set"
    want = enabled_line if enable else disabled_line

    before = sdk_path.read_text(encoding="utf-8")
    out, found = [], False
    for raw in before.splitlines():
        s = raw.strip()
        if s == enabled_line or s == disabled_line:
            out.append(want)
            found = True
        else:
            out.append(raw)
    if not found:
        out.append(want)
    after = "\n".join(out) + "\n"
    if after != before:
        sdk_path.write_text(after, encoding="utf-8")
        return True
    return False


def load_json(path: Path):
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception as e:
        print(f"ERROR: invalid JSON in {path}: {e}", file=sys.stderr)
        sys.exit(1)


def confirm(prompt: str, assume_yes: bool) -> bool:
    if assume_yes:
        return True
    try:
        return input(f"{prompt} [y/N] ").strip().lower() in ("y", "yes")
    except EOFError:
        return False


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description="Reconcile driver menuconfig with the active board.")
    p.add_argument("--data-dir", type=Path, default=ROOT / "data")
    p.add_argument("--drivers-dir", type=Path, default=ROOT / "drivers")
    p.add_argument("--sdkconfig", type=Path, default=ROOT / "sdkconfig")
    p.add_argument("--fix", action="store_true", help="enable bound-but-disabled drivers")
    p.add_argument("--prune", action="store_true", help="also disable enabled-but-unused drivers")
    p.add_argument("--yes", action="store_true", help="apply without prompting")
    p.add_argument("--dry-run", action="store_true", help="report only, change nothing")
    p.add_argument("--rebuild", action="store_true", help="run 'idf.py build' after (needs IDF shell)")
    args = p.parse_args(argv)

    board = load_json(args.data_dir / "board.json")
    bindings = load_json(args.data_dir / "bindings.json")
    if not bindings:
        print("No data/bindings.json — nothing to reconcile (build a board first).")
        return 0

    warnings = []
    drivers = load_all_driver_manifests(args.drivers_dir, warnings)
    for w in warnings:
        print(f"WARNING: {w}")

    # Validate first — never reconcile a broken bindings set.
    # project.json for the orphaned-binding check (binding → module not in
    # project). File absent → None sentinel: SKIP the check (an empty set
    # would falsely flag EVERY binding as orphaned), and say so.
    project = load_json(ROOT / "project.json")
    if project is None:
        print(f"WARNING: {ROOT / 'project.json'} not found — "
              "skipping the orphaned-binding (module in project) check.")
    project_modules = set(project.get("modules", [])) if project else None
    errors, vwarn = [], []
    validate_bindings(board, bindings, drivers, errors, vwarn,
                      project_modules=project_modules)
    if errors:
        print("Bindings are invalid — fix these before reconciling:")
        for e in errors:
            print(f"  ERROR: {e}")
        return 1

    state = read_driver_config(args.sdkconfig)
    bound = []
    for b in bindings.get("bindings", []):
        d = b.get("driver")
        if d in drivers and d not in bound:
            bound.append(d)

    # Transport master switches: with 'depends on' unmet the driver symbol is
    # ABSENT from sdkconfig (kconfig hides it) — which is_enabled() would
    # misread as "enabled by default". Resolve the masters explicitly so a
    # bound BLE driver with BLE off is reported (and fixed) via its master.
    masters_off = {}   # symbol -> [driver, ...]
    for d in bound:
        hw = (drivers.get(d) or {}).get("hardware_type", "")
        if hw in MASTER_SWITCHES:
            sym, default = MASTER_SWITCHES[hw]
            if not read_symbol(args.sdkconfig, sym, default):
                masters_off.setdefault(sym, []).append(d)

    bound_but_disabled = [d for d in bound if not is_enabled(state, d)]
    for ds in masters_off.values():
        for d in ds:
            if d not in bound_but_disabled:
                bound_but_disabled.append(d)
    unused = unused_drivers(bindings, drivers)          # already excludes discovery
    enabled_but_unused = [d for d in unused if is_enabled(state, d)]

    if not bound_but_disabled and not enabled_but_unused and not masters_off:
        print("In sync: every bound driver is enabled and no unused driver is compiled.")
        return 0

    print(f"Active board: {board.get('board', '?') if board else '?'}")
    for sym, ds in masters_off.items():
        print(f"  master switch {sym} is OFF — required by bound driver(s): {', '.join(ds)}")
    if bound_but_disabled:
        print(f"  bound but DISABLED (will enable): {', '.join(bound_but_disabled)}")
    if enabled_but_unused:
        print(f"  enabled but UNUSED (--prune to disable): {', '.join(enabled_but_unused)}")

    if args.dry_run:
        print("(dry-run — no changes)")
        return 0

    changed = False
    # Enable master switches first — the drivers' own symbols only become
    # visible to kconfig once their master is on.
    if masters_off and (args.fix or not args.prune):
        for sym in masters_off:
            if confirm(f"Enable master switch '{sym}'?", args.yes):
                changed |= set_config(args.sdkconfig, sym, True)
    # Enable bound-but-disabled (default action / --fix).
    if bound_but_disabled and (args.fix or not args.prune):
        for d in bound_but_disabled:
            if confirm(f"Enable driver '{d}'?", args.yes):
                changed |= set_config(args.sdkconfig, cfg_symbol(d), True)
    # Disable enabled-but-unused (--prune only).
    if enabled_but_unused and args.prune:
        for d in enabled_but_unused:
            if confirm(f"Disable unused driver '{d}'?", args.yes):
                changed |= set_config(args.sdkconfig, cfg_symbol(d), False)

    if not changed:
        print("No changes written.")
        return 0

    print(f"Updated {args.sdkconfig.name}.")
    if args.rebuild:
        print("Rebuilding (idf.py build)...")
        return subprocess.call(["idf.py", "build"], cwd=str(ROOT))
    print("Run 'idf.py build' to apply (no fullclean needed for value changes).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
