/**
 * @file fuzz_modr_loader.cpp
 * @brief libFuzzer harness для modr_validate() (Step 9).
 *
 * Goal: prove що loader handles ANY byte sequence без crashing, hanging,
 * or out-of-bounds reads. Output is irrelevant — ми lookup для
 * sanitizer-detected violations (ASAN heap-buffer-overflow, UBSAN signed
 * overflow, stack-overflow, infinite loops via -timeout).
 *
 * Build (requires Clang з libFuzzer support):
 *
 *     make            # → ./fuzz_modr_loader executable
 *
 * Run quick sanity (60 sec):
 *
 *     ./fuzz_modr_loader corpus/ -max_total_time=60
 *
 * Run extended із dictionary і sensible defaults:
 *
 *     ./fuzz_modr_loader corpus/ -max_total_time=600 -max_len=16384 \
 *         -dict=modr_dict.txt
 *
 * On crash: libFuzzer writes minimal reproducer, prints stack trace.
 *
 * CI integration (suggestion): run 60s smoke per PR; нічна 1-hour campaign
 * на main з results posted to artifact storage. Adjust timeouts in
 * `.github/workflows/fuzz.yml` (Stage 1.5 deliverable).
 */

#include "modesp/sequence/modr_loader.h"
#include "modesp/sequence/builtin_actions.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/) {
    // Register built-in actions/conditions ONCE per fuzzer process. Loader
    // resolves cond_pool entries проти ActionRegistry, so registry must be
    // populated. Without це, every fuzz input з conditions would trip
    // UNKNOWN_CONDITION early і miss deeper code paths.
    modesp::sequence::builtins::register_builtins();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    using namespace modesp::sequence;
    LoadedScenario ls;
    EngineError err = modr_validate(data, size, ls);
    (void)err;  // result discarded — we just need execution to complete safely

    // Якщо validation succeeded, exercise read_string з pseudo-random offsets
    // derived з input bytes. Catches OOB reads у string pool walker.
    if (err == EngineError::OK) {
        char buf[64];
        // Use first byte як offset seed (loader tolerates any value, returns false)
        uint16_t off = (size > 0) ? static_cast<uint16_t>(data[0]) : 0u;
        ls.read_string(off, buf, sizeof(buf));
    }
    return 0;
}
