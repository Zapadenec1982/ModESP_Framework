#!/usr/bin/env python3
"""
run_host_tests.py — run the pure-Python host tests in tools/tests/.

Why a wrapper: the ESP-IDF Python venv auto-loads the ``idf_ci`` / ``pytest-embedded``
plugins, whose ``pytest_configure`` reads a ``--target`` option meant for on-target/HIL
runs and crashes plain unit-test collection ("no option named 'target'"). Setting
PYTEST_DISABLE_PLUGIN_AUTOLOAD restores a clean pytest for the generator/validation suite.

Run it with a python that has pytest + jsonschema — the simplest is the ESP-IDF venv
python (inside an ESP-IDF shell), e.g.::

    python tools/run_host_tests.py                 # ~379 passed, ~65 skipped
    python tools/run_host_tests.py --cpp-host      # also the native C++ host tests
    python tools/run_host_tests.py -k mqtt -v      # extra args pass through to pytest

test_cpp_host.py builds a native C++ unit-test binary via CMake and needs a host
compiler, so it is skipped unless --cpp-host is given.
"""
import os
import sys
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
TESTS = HERE / "tests"


def main() -> int:
    args = sys.argv[1:]
    cpp_host = "--cpp-host" in args
    if cpp_host:
        args.remove("--cpp-host")

    cmd = [sys.executable, "-m", "pytest", str(TESTS), "-q"]
    if not cpp_host:
        cmd.append(f"--ignore={TESTS / 'test_cpp_host.py'}")
    cmd += args

    env = dict(os.environ, PYTEST_DISABLE_PLUGIN_AUTOLOAD="1")
    print("running:", " ".join(cmd))
    return subprocess.call(cmd, env=env)


if __name__ == "__main__":
    raise SystemExit(main())
