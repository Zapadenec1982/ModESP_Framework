@echo off
REM ═══════════════════════════════════════════════════════════════
REM  ModESP Host C++ Tests — Build & Run
REM  Usage: run_tests.bat [--rebuild]
REM ═══════════════════════════════════════════════════════════════

setlocal

REM ── Tool paths ──
set "MSYS2_BIN=C:\msys64\ucrt64\bin"
set "CMAKE_EXE=C:\Espressif\tools\cmake\3.30.2\bin\cmake.exe"
set "NINJA_EXE=C:\Espressif\tools\ninja\1.12.1\ninja.exe"
set "GCC=%MSYS2_BIN%\gcc.exe"
set "GXX=%MSYS2_BIN%\g++.exe"

REM ── Add MSYS2 to PATH (needed for libmpc-3.dll, libmpfr, libgmp, etc.) ──
set "PATH=%MSYS2_BIN%;%PATH%"

REM ── Directories ──
set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%build"

REM ── Check tools ──
if not exist "%GXX%" (
    echo ERROR: g++ not found at %GXX%
    echo Install MSYS2 ucrt64 toolchain: pacman -S mingw-w64-ucrt-x86_64-gcc
    exit /b 1
)
if not exist "%CMAKE_EXE%" (
    echo ERROR: cmake not found at %CMAKE_EXE%
    exit /b 1
)
if not exist "%NINJA_EXE%" (
    echo ERROR: ninja not found at %NINJA_EXE%
    exit /b 1
)

REM ── Clean rebuild if --rebuild flag ──
if "%1"=="--rebuild" (
    echo Cleaning build directory...
    if exist "%BUILD_DIR%" rmdir /S /Q "%BUILD_DIR%"
)

REM ── Configure (only if not yet configured) ──
if not exist "%BUILD_DIR%\build.ninja" (
    echo Configuring CMake...
    if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
    "%CMAKE_EXE%" -G "Ninja" ^
        -DCMAKE_C_COMPILER="%GCC%" ^
        -DCMAKE_CXX_COMPILER="%GXX%" ^
        -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
        -S "%SCRIPT_DIR%" ^
        -B "%BUILD_DIR%"
    if errorlevel 1 (
        echo ERROR: CMake configuration failed
        exit /b 1
    )
)

REM ── Build ──
echo Building...
"%NINJA_EXE%" -C "%BUILD_DIR%"
if errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)

REM ── Run tests ──
echo.
echo ══════════════════════════════════════════════
echo  Running tests...
echo ══════════════════════════════════════════════
echo.
"%BUILD_DIR%\test_runner.exe"
exit /b %errorlevel%
