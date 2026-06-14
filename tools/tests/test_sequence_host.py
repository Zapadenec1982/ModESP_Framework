"""
Pytest wrapper that compiles and runs the scenario engine host tests.

Independent of the project-level tests/host/ infrastructure. Tests live in
components/modesp_scenario/tests/host/ with a standalone build (no CMake, just
direct g++ invocation). The source lists + includes mirror that component's
tests/host/Makefile (the source of truth) — keep them in sync if the Makefile
changes.

ETL include path is resolved on first run the same way as the Makefile:
managed_components/ first, then the ESP-IDF Component Manager global cache.

Tests run only if g++ is available; skipped on systems without a C++17 compiler.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).parent.parent.parent
COMPONENT = REPO_ROOT / "components" / "modesp_scenario"
HOST_TEST_DIR = COMPONENT / "tests" / "host"
BUILD_DIR = HOST_TEST_DIR / "build"

# Shared include set — mirrors INCLUDES in the component's tests/host/Makefile.
INCLUDES = [
    COMPONENT / "include",
    COMPONENT / "private",
    HOST_TEST_DIR / "common",
    REPO_ROOT / "components" / "modesp_core" / "include",
    REPO_ROOT / "generated",
]

# Per-test source lists — mirror REG_SRCS / NVS_TOKEN_SRCS / NVS_OBS_SRCS in the
# Makefile. Each test's <name>.cpp is compiled together with these.
_SRC = COMPONENT / "src"
TEST_SOURCES = {
    "test_action_registry": [
        _SRC / "actions" / "action_registry.cpp",
    ],
    "test_nvs_token": [
        _SRC / "observers" / "nvs_token.cpp",
        _SRC / "core" / "modr_loader.cpp",
        _SRC / "actions" / "action_registry.cpp",
    ],
    "test_nvs_observer": [
        _SRC / "observers" / "nvs_observer.cpp",
        _SRC / "observers" / "nvs_token.cpp",
        _SRC / "observers" / "mirror.cpp",
        _SRC / "core" / "engine.cpp",
        _SRC / "core" / "instance.cpp",
        _SRC / "core" / "track.cpp",
        _SRC / "core" / "modr_loader.cpp",
        _SRC / "arbiter" / "resource_arbiter.cpp",
        _SRC / "actions" / "action_registry.cpp",
        _SRC / "continuous" / "continuous_registry.cpp",
    ],
}


def _find_gpp() -> str | None:
    """Locate g++ executable. Returns full path or None if not found."""
    gpp = shutil.which("g++")
    if gpp:
        return gpp
    candidates = [
        "C:/msys64/ucrt64/bin/g++.exe",
        "C:/msys64/mingw64/bin/g++.exe",
        "/usr/bin/g++",
    ]
    for c in candidates:
        if Path(c).exists():
            return c
    return None


def _find_etl_include() -> Path | None:
    """Find ETL header path (managed_components first, then ESP-IDF cache)."""
    managed = REPO_ROOT / "managed_components" / "marcel-cd__etlcpp" / "etl" / "include"
    if managed.exists():
        return managed

    if os.name == "nt":
        cache_base = Path(os.environ.get("LOCALAPPDATA", "")) / "Espressif" / "ComponentManager" / "Cache"
    else:
        cache_base = Path.home() / ".espressif" / "ComponentManager" / "Cache"

    if cache_base.exists():
        matches = sorted(cache_base.glob("*/marcel-cd__etlcpp_*/etl/include"), reverse=True)
        if matches:
            return matches[0]

    return None


def _compile_test(name: str, gpp: str, etl_include: Path) -> Path:
    """Compile a single host test executable. Returns path to the binary."""
    src = HOST_TEST_DIR / f"{name}.cpp"
    if not src.exists():
        raise FileNotFoundError(f"Test source not found: {src}")

    sources = TEST_SOURCES.get(name, [])
    BUILD_DIR.mkdir(parents=True, exist_ok=True)

    output = BUILD_DIR / f"{name}.exe"
    cmd = [
        gpp,
        "-std=c++17", "-Wall", "-Wno-unused-parameter", "-Wno-unused-variable",
        "-DETL_NO_PROFILE_HEADER", "-DHOST_BUILD=1",
        "-O0", "-g",
    ]
    for inc in INCLUDES:
        cmd.extend(["-I", str(inc)])
    cmd.extend(["-I", str(etl_include)])
    cmd.append(str(src))
    cmd.extend(str(s) for s in sources)
    cmd.extend(["-o", str(output)])

    # IMPORTANT (Windows MSYS2 toolchain): cc1plus.exe (g++ child) needs runtime
    # DLLs (libstdc++-6.dll, libgcc_s_seh-1.dll, ...) from the same bin folder
    # as g++.exe. If that dir isn't on PATH, the child loads zero DLLs, exits
    # silently, and parent g++ reports nothing (returncode 1, no stderr). We
    # prepend g++'s directory to PATH for the subprocess so DLL resolution works.
    env = os.environ.copy()
    gpp_dir = str(Path(gpp).parent)
    env["PATH"] = gpp_dir + os.pathsep + env.get("PATH", "")

    result = subprocess.run(
        cmd, capture_output=True, text=True, stdin=subprocess.DEVNULL, env=env
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"Compilation failed (returncode={result.returncode}):\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return output


# ── Fixtures ──


@pytest.fixture(scope="session")
def gpp():
    p = _find_gpp()
    if p is None:
        pytest.skip("g++ not found — skipping C++ host tests")
    return p


@pytest.fixture(scope="session")
def etl_include():
    p = _find_etl_include()
    if p is None:
        pytest.skip("ETL not found — run idf.py build first to download dependencies")
    return p


# ── Tests ──


def _run_host_test(name: str, gpp: str, etl_include: Path):
    """Helper: compile and run a host test binary, assert success.

    The test binary also needs MSYS2 runtime DLLs on PATH (linked dynamically
    against libstdc++-6.dll, libgcc_s_seh-1.dll, etc.). Without them the binary
    fails to start, so we propagate g++'s bin dir into PATH for the run."""
    binary = _compile_test(name, gpp, etl_include)
    env = os.environ.copy()
    env["PATH"] = str(Path(gpp).parent) + os.pathsep + env.get("PATH", "")
    result = subprocess.run(
        [str(binary)], capture_output=True, text=True, stdin=subprocess.DEVNULL, env=env
    )
    assert result.returncode == 0, (
        f"Host test {name} FAILED:\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )


def test_action_registry_host(gpp, etl_include):
    """ActionRegistry: register/find for actions and conditions, collision
    detection, djb2 hash invariants."""
    _run_host_test("test_action_registry", gpp, etl_include)


def test_nvs_token_host(gpp, etl_include):
    """Persistence token: serialize/deserialize round-trip restores phase_idx +
    elapsed_ms, magic/version/CRC corruption → specific EngineError codes,
    scenario_id mismatch rejected (stale token from a previous recipe)."""
    _run_host_test("test_nvs_token", gpp, etl_include)


def test_nvs_observer_host(gpp, etl_include):
    """NvsObserver: edge-triggered persist (start/phase/terminal) with throttle,
    recovery into PAUSED, and the engine/instance/track stack it drives."""
    _run_host_test("test_nvs_observer", gpp, etl_include)
