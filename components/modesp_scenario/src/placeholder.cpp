/**
 * @file placeholder.cpp
 * @brief Phase 0 scaffold — empty TU so component has a valid build target.
 *
 * Replaced by real sources у Phase 1 (core lift):
 *   src/core/engine.cpp
 *   src/core/instance.cpp
 *   src/core/track.cpp
 *   src/core/modr_loader.cpp
 *   ...
 */

namespace modesp::scenario {

// Anchor symbol so linker doesn't warn about empty translation unit.
extern "C" const char* modesp_scenario_phase0_marker() {
    return "modesp_scenario: Phase 0 scaffold";
}

}  // namespace modesp::scenario
