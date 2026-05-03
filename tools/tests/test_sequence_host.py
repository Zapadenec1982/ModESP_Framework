"""
Pytest wrapper що compiles і runs sequence engine host tests.

Independent of project-level tests/host/ infrastructure (entangled з stripped
refrigeration modules). Tests live у components/modesp_sequence/tests/host/
з standalone build (no CMake, just g++ direct invocation).

Discovery: ETL include path resolved on first run у same way як components/
modesp_sequence/tests/host/Makefile — checks managed_components/, then
ESP-IDF Component Manager global cache.

Tests run only якщо g++ available; skipped on systems without C++17 compiler.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).parent.parent.parent
COMPONENT = REPO_ROOT / "components" / "modesp_sequence"
HOST_TEST_DIR = COMPONENT / "tests" / "host"
SOURCES = [
    COMPONENT / "src" / "action_registry.cpp",
]
INCLUDES = [
    COMPONENT / "include",
    REPO_ROOT / "components" / "modesp_core" / "include",
]


def _find_gpp() -> str | None:
    """Locate g++ executable. Returns full path or None if not found."""
    # Try PATH first
    gpp = shutil.which("g++")
    if gpp:
        return gpp
    # Try common Windows locations
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

    # Windows ESP-IDF Component Manager cache
    if os.name == "nt":
        cache_base = Path(os.environ.get("LOCALAPPDATA", "")) / "Espressif" / "ComponentManager" / "Cache"
    else:
        cache_base = Path.home() / ".espressif" / "ComponentManager" / "Cache"

    if cache_base.exists():
        # Glob for marcel-cd__etlcpp_*/etl/include
        matches = sorted(cache_base.glob("*/marcel-cd__etlcpp_*/etl/include"), reverse=True)
        if matches:
            return matches[0]

    return None


def _compile_test(name: str, gpp: str, etl_include: Path) -> Path:
    """Compile a single host test executable. Returns path to binary."""
    src = HOST_TEST_DIR / f"{name}.cpp"
    if not src.exists():
        raise FileNotFoundError(f"Test source not found: {src}")

    output = HOST_TEST_DIR / f"{name}.exe"
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
    cmd.extend(str(s) for s in SOURCES)
    cmd.extend(["-o", str(output)])

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Compilation failed:\nstdout: {result.stdout}\nstderr: {result.stderr}")
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


def test_action_registry_host(gpp, etl_include):
    """Compile і run components/modesp_sequence/tests/host/test_action_registry.cpp.
    Тестує singleton, register/find для actions і conditions, collision detection,
    djb2 invariants. ~12 internal test cases у one binary."""
    binary = _compile_test("test_action_registry", gpp, etl_include)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, (
        f"Host test FAILED:\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    # Verify expected output pattern
    assert "passed" in result.stdout
    assert "0 failed" in result.stdout, f"Some sub-tests failed:\n{result.stdout}"
