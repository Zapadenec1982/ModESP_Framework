"""
test_cpp_host.py — C++ host unit tests via cmake + clang/MinGW.

Builds the C++ test runner (doctest-based) on Windows using clang++ from ESP-IDF
or MinGW g++, then runs each test suite and reports results back to pytest.

Usage:
    python -m pytest tools/tests/test_cpp_host.py -v
    python -m pytest tools/tests/test_cpp_host.py -v -k thermostat
"""

import subprocess
import sys
import os
import re
import glob

import pytest

# ── Project paths ──
ROOT      = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BUILD_DIR = os.path.join(ROOT, 'tests', 'host', 'build')
SRC_DIR   = os.path.join(ROOT, 'tests', 'host')

# ── IDF_TOOLS_PATH (може бути задано середовищем або за замовчуванням) ──
IDF_TOOLS_PATH = os.environ.get('IDF_TOOLS_PATH', r'C:\Espressif\tools')

# ── scoop home (зазвичай %USERPROFILE%\scoop) ──
_SCOOP_ROOT = os.path.join(os.path.expanduser('~'), 'scoop', 'apps', 'mingw', 'current', 'bin')

# ── Compiler search — MinGW g++ або Clang++ (в порядку пріоритету) ──
COMPILER_CANDIDATES = [
    # scoop install mingw  (рекомендовано: найпростіше встановлення на Windows)
    {'gxx': os.path.join(_SCOOP_ROOT, 'g++.exe'),
     'gcc': os.path.join(_SCOOP_ROOT, 'gcc.exe'),
     'make': os.path.join(_SCOOP_ROOT, 'mingw32-make.exe'),
     'generator': 'MinGW Makefiles'},
    # MSYS2 UCRT64 (winget install MSYS2.MSYS2 + pacman -S mingw-w64-ucrt-x86_64-gcc)
    {'gxx': r'C:\msys64\ucrt64\bin\g++.exe',   'gcc': r'C:\msys64\ucrt64\bin\gcc.exe',   'make': r'C:\msys64\ucrt64\bin\mingw32-make.exe',   'generator': 'MinGW Makefiles'},
    # MSYS2 MinGW64
    {'gxx': r'C:\msys64\mingw64\bin\g++.exe',  'gcc': r'C:\msys64\mingw64\bin\gcc.exe',  'make': r'C:\msys64\mingw64\bin\mingw32-make.exe',  'generator': 'MinGW Makefiles'},
    # Standalone MinGW
    {'gxx': r'C:\mingw64\bin\g++.exe',          'gcc': r'C:\mingw64\bin\gcc.exe',          'make': r'C:\mingw64\bin\mingw32-make.exe',          'generator': 'MinGW Makefiles'},
    {'gxx': r'C:\mingw32\bin\g++.exe',          'gcc': r'C:\mingw32\bin\gcc.exe',          'make': r'C:\mingw32\bin\mingw32-make.exe',          'generator': 'MinGW Makefiles'},
]

# Ninja (від ESP-IDF) для використання з Clang
NINJA_PATHS = [
    os.path.join(IDF_TOOLS_PATH, r'ninja\1.12.1\ninja.exe'),
    r'C:\Espressif\tools\ninja\1.12.1\ninja.exe',
    r'C:\Espressif\tools\ninja\1.11.1\ninja.exe',
]

# Clang++ варіанти (в порядку пріоритету)
CLANG_PATHS = [
    r'C:\Program Files\LLVM\bin\clang++.exe',   # winget install LLVM.LLVM
    r'C:\msys64\ucrt64\bin\clang++.exe',        # pacman -S mingw-w64-ucrt-x86_64-clang
    r'C:\msys64\mingw64\bin\clang++.exe',
]


def _find_esp_idf_clang():
    """Знайти clang++ з ESP-IDF (esp-clang директорія з версійним підкаталогом)."""
    esp_clang_base = os.path.join(IDF_TOOLS_PATH, 'esp-clang')
    if not os.path.isdir(esp_clang_base):
        return None
    pattern = os.path.join(esp_clang_base, '*', 'esp-clang', 'bin', 'clang++.exe')
    matches = sorted(glob.glob(pattern), reverse=True)
    return matches[0] if matches else None


def _find_cmake():
    """Знайти cmake: спочатку в PATH, потім в IDF_TOOLS_PATH."""
    import shutil
    cmake_in_path = shutil.which('cmake')
    if cmake_in_path:
        return cmake_in_path
    cmake_base = os.path.join(IDF_TOOLS_PATH, 'cmake')
    if os.path.isdir(cmake_base):
        pattern = os.path.join(cmake_base, '*', 'bin', 'cmake.exe')
        matches = sorted(glob.glob(pattern), reverse=True)
        if matches:
            return matches[0]
    return None


def _msys2_bin_for(gxx_path):
    """
    Для MSYS2 g++ повертає директорію bin (потрібна в PATH для cc1plus, ld тощо).
    Повертає None якщо не MSYS2.
    """
    if 'msys64' in gxx_path.lower():
        return os.path.dirname(gxx_path)
    return None


def find_compiler():
    """
    Return dict with {'gxx', 'gcc', 'make', 'generator', 'extra_path'} for the first
    available compiler, or None if no compiler is found.

    'extra_path' — додаткові директорії що треба додати в PATH при запуску cmake/g++
    (потрібно для MSYS2 g++ щоб знаходив cc1plus.exe, ld.exe тощо).
    """
    import shutil

    ninja = next((n for n in NINJA_PATHS if os.path.exists(n)), shutil.which('ninja'))

    # 1. MinGW g++ (найнадійніший на Windows)
    for cand in COMPILER_CANDIDATES:
        if os.path.exists(cand['gxx']):
            make = cand['make'] if os.path.exists(cand['make']) else None
            extra = _msys2_bin_for(cand['gxx'])
            if not make and ninja:
                return {**cand, 'make': ninja, 'generator': 'Ninja', 'extra_path': extra}
            if make:
                return {**cand, 'extra_path': extra}

    # 2. ESP-IDF clang++ (встановлений разом з IDF)
    esp_clang = _find_esp_idf_clang()
    if esp_clang and ninja:
        return {
            'gxx': esp_clang,
            'gcc': esp_clang.replace('clang++.exe', 'clang.exe'),
            'make': ninja,
            'generator': 'Ninja',
            'is_clang': True,
            'extra_path': None,
        }

    # 3. Standalone clang++
    for cp in CLANG_PATHS:
        if os.path.exists(cp):
            return {
                'gxx': cp, 'gcc': cp.replace('clang++', 'clang'),
                'make': ninja or '', 'generator': 'Ninja' if ninja else 'Unix Makefiles',
                'is_clang': True,
                'extra_path': _msys2_bin_for(cp),
            }

    # 4. PATH fallback
    if shutil.which('g++'):
        return {'gxx': '', 'gcc': '', 'make': '', 'generator': 'Unix Makefiles', 'extra_path': None}
    return None


def _make_env(compiler):
    """
    Будує env dict для subprocess із доданим extra_path (якщо MSYS2 g++).
    MSYS2 g++ потребує свій bin у PATH щоб знаходив cc1plus.exe, ld.exe тощо.
    """
    env = os.environ.copy()
    extra = compiler.get('extra_path')
    if extra and extra not in env.get('PATH', ''):
        env['PATH'] = extra + os.pathsep + env.get('PATH', '')
    return env


def _to_cmake_path(p):
    """Convert Windows backslash path to forward-slash for CMake."""
    return p.replace('\\', '/')


# ── Cache the build environment so _run_suite can reuse MSYS2 PATH ──
_build_env = None


@pytest.fixture(scope='session')
def built_binary():
    """
    Build the C++ test runner once per session.

    Runs cmake configure + build, then returns the path to the binary.
    Fails the session if configure or build fails.
    """
    os.makedirs(BUILD_DIR, exist_ok=True)

    compiler = find_compiler()
    if not compiler:
        pytest.skip(
            "Не знайдено C++ компілятор для Windows. Встановіть MinGW одним із способів:\n"
            "  1) scoop install mingw          (рекомендовано — scoop вже встановлений)\n"
            "  2) winget install MSYS2.MSYS2\n"
            "     C:\\msys64\\usr\\bin\\bash -lc 'pacman -S --noconfirm mingw-w64-ucrt-x86_64-gcc'\n"
        )

    cmake_exe = _find_cmake()
    if not cmake_exe:
        pytest.skip(
            "Не знайдено cmake. Встановіть ESP-IDF або додайте cmake в PATH."
        )

    cmake_args = [
        cmake_exe, '-S', SRC_DIR, '-B', BUILD_DIR,
        '-G', compiler.get('generator', 'MinGW Makefiles'),
    ]
    if compiler.get('gxx'):
        cmake_args += [f'-DCMAKE_CXX_COMPILER={_to_cmake_path(compiler["gxx"])}']
    if compiler.get('gcc'):
        cmake_args += [f'-DCMAKE_C_COMPILER={_to_cmake_path(compiler["gcc"])}']
    if compiler.get('make'):
        cmake_args += [f'-DCMAKE_MAKE_PROGRAM={_to_cmake_path(compiler["make"])}']

    env = _make_env(compiler)

    # Cache env so _run_suite can pass MSYS2 PATH when running the binary.
    # MSYS2-built binaries need ucrt64/bin in PATH to find DLLs at runtime.
    global _build_env
    _build_env = env

    print(f"\n[C++] Compiler: {compiler.get('gxx', '(PATH g++)')}")
    print(f"[C++] CMake:    {cmake_exe}")
    print(f"[C++] Generator: {compiler.get('generator')}")
    if compiler.get('extra_path'):
        print(f"[C++] Extra PATH: {compiler['extra_path']}")

    # ── Configure ──
    r = subprocess.run(cmake_args, capture_output=True, text=True, cwd=ROOT, env=env)
    if r.returncode != 0:
        pytest.fail(
            f"CMake configure failed (returncode={r.returncode}):\n"
            f"STDOUT:\n{r.stdout}\n"
            f"STDERR:\n{r.stderr}"
        )

    # ── Build ──
    r = subprocess.run(
        [cmake_exe, '--build', BUILD_DIR, '--parallel'],
        capture_output=True, text=True, cwd=ROOT, env=env
    )
    if r.returncode != 0:
        pytest.fail(
            f"CMake build failed (returncode={r.returncode}):\n"
            f"STDOUT:\n{r.stdout}\n"
            f"STDERR:\n{r.stderr}"
        )

    # ── Locate binary ──
    exe_win  = os.path.join(BUILD_DIR, 'test_runner.exe')
    exe_unix = os.path.join(BUILD_DIR, 'test_runner')
    if os.path.exists(exe_win):
        return exe_win
    if os.path.exists(exe_unix):
        return exe_unix
    pytest.fail(f"test_runner binary not found in {BUILD_DIR}")


def _run_suite(binary, filter_str=None):
    """
    Run doctest binary with optional --test-case filter.
    Uses _build_env so MSYS2-built binaries can find their runtime DLLs.
    Returns (combined_output, returncode).
    """
    cmd = [binary]
    if filter_str:
        cmd.append(f'--test-case={filter_str}')
    r = subprocess.run(cmd, capture_output=True, text=True, env=_build_env)
    return r.stdout + r.stderr, r.returncode


# ════════════════════════════════════════════════════════════════
# Tests
# ════════════════════════════════════════════════════════════════

def test_build(built_binary):
    """Verify the C++ test binary was built successfully."""
    assert os.path.exists(built_binary), f"Binary not found: {built_binary}"
    size = os.path.getsize(built_binary)
    assert size > 1024, f"Binary suspiciously small ({size} bytes)"
    print(f"\n[C++] Binary: {built_binary} ({size // 1024} KB)")


def test_thermostat_suite(built_binary):
    """Run all thermostat test cases."""
    out, rc = _run_suite(built_binary, '*thermostat*')
    assert rc == 0, f"Thermostat tests failed:\n{out}"


def test_defrost_suite(built_binary):
    """Run all defrost test cases."""
    out, rc = _run_suite(built_binary, '*defrost*')
    assert rc == 0, f"Defrost tests failed:\n{out}"


def test_protection_suite(built_binary):
    """Run all protection test cases."""
    out, rc = _run_suite(built_binary, '*protection*')
    assert rc == 0, f"Protection tests failed:\n{out}"


def test_all_pass(built_binary):
    """Run the full test suite and verify zero failures."""
    out, rc = _run_suite(built_binary)
    assert rc == 0, f"C++ test runner exited with code {rc}:\n{out}"

    passed = 0
    failed = 0

    m = re.search(r'(\d+)\s+passed', out)
    if m:
        passed = int(m.group(1))

    m = re.search(r'(\d+)\s+failed', out)
    if m:
        failed = int(m.group(1))

    no_cases = 'No test cases' in out or passed + failed == 0

    assert failed == 0, f"{failed} C++ test(s) failed!\n{out}"

    if no_cases:
        print(f"\n[C++] No test cases registered yet (placeholders only) — OK")
    else:
        print(f"\n[C++] {passed} test(s) passed, {failed} failed")
