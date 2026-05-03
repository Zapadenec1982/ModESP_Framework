# modr_loader fuzz harness

libFuzzer-driven fuzz testing для `modr_validate()`. Goal: prove що loader
handles ANY byte sequence без crashing, hanging, або OOB reads.

## Prerequisites

- Clang з libFuzzer (`-fsanitize=fuzzer`) and ASan/UBSAN support — any
  modern Clang ≥ 12. Verify: `clang++ --version`.
- ETL headers (auto-discovered from `managed_components/` after `idf.py build`,
  або from `~/.espressif/ComponentManager/Cache` global cache).

On Windows: native MSYS2 Clang (`pacman -S mingw-w64-ucrt-x86_64-clang`)
або WSL Linux Clang. MSVC's `clang-cl` does NOT include libFuzzer driver.

## Build і run

```bash
make                                              # → ./fuzz_modr_loader
./fuzz_modr_loader corpus/ -max_total_time=60     # 60-second smoke
```

Extended run з reasonable defaults:

```bash
./fuzz_modr_loader corpus/ \
    -max_total_time=600 \
    -max_len=16384 \
    -timeout=10
```

`-max_len=16384` matches `MODR_MAX_SIZE` (16 KB). Larger inputs не realistic
для embedded LittleFS recipes.

## On crash detection

libFuzzer writes `crash-<sha1>` reproducer when sanitizer trips, prints stack
trace. Reproduce manually:

```bash
./fuzz_modr_loader crash-abc123
```

**Triage protocol:**

1. Save reproducer file
2. Read sanitizer report (heap-buffer-overflow vs. stack-overflow vs. UBSAN)
3. Add minimized reproducer to `tests/host/test_modr_loader.cpp` як
   regression test BEFORE fixing
4. Fix `modr_loader.cpp`; verify reproducer no longer crashes
5. Re-run fuzzer to confirm

## Corpus management

`corpus/` checked into git contains seed inputs (one realistic recipe).
libFuzzer evolves corpus internally — committed seeds bootstrap exploration.

Кожен fuzz run grows corpus з interesting inputs found. Persist between CI
runs through artifact storage (Stage 1.5).

## CI integration (Stage 1.5)

Suggested workflow at `.github/workflows/fuzz.yml`:

| Trigger | Duration | Goal |
|---------|----------|------|
| Per-PR  | 60 sec   | Smoke regression — block merge on any new crash |
| Nightly | 60 min   | Deeper coverage — file issues for new findings |
| Weekly  | 8 hours  | Saturation — diminishing returns past це |

## Why fuzz only the loader?

`modr_validate()` is the trust boundary між filesystem-loaded byte buffers
і engine internals. Once validation succeeds, engine code accesses buffer
through bounded LoadedScenario accessors — those don't read untrusted bytes.
Fuzzing engine state machines would just chase recipe authoring bugs that
compile_scenario.py already catches.

Other validation layers (compile_scenario.py schema, hypothesis property tests)
cover authoring-time errors. Fuzzer covers runtime corruption / malicious
upload paths.
