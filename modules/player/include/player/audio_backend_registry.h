/**
 * @file audio_backend_registry.h
 * @brief Type-string → factory registry for audio sink backends.
 *
 * Audio analogue of modesp::display::DisplayBackendRegistry. Each sink backend
 * (max98357a, …) registers a factory under its type string (== "driver" field in
 * bindings.json); PlayerModule resolves the active sink by binding.driver_type —
 * no hardcoded #if-cascade. A backend disabled in menuconfig is not compiled, so
 * its register function is absent and register_all_audio_backends() skips it.
 *
 * The registry lives in the player module (not modesp_hal) because IAudioSink is
 * a domain interface. The factory gets (Binding&, HAL&) — like driver/display
 * factories — and resolves the I²S pins via HAL::find_i2s_bus (board.json = the
 * source of truth for pins). Binding/HAL are forward-declared: the factory
 * typedef takes references, so full definitions are not needed here.
 */

#pragma once

#include "player/audio_sink.h"
#include <cstddef>

namespace modesp {
struct Binding;
class  HAL;
}

namespace modesp::audio {

/// Backend factory: (binding, hal) → IAudioSink* (nullptr on error).
using AudioFactory = IAudioSink* (*)(const modesp::Binding&, modesp::HAL&);

class AudioBackendRegistry {
public:
    static constexpr size_t MAX_BACKENDS = 4;

    /// Register a factory under a type string. Idempotent (re-registering the
    /// same type is a no-op), so register-all can be called twice.
    static bool register_backend(const char* type, AudioFactory fn);

    /// Create a backend for `type`. nullptr if the type is unknown/disabled or
    /// the factory itself failed.
    static IAudioSink* create(const char* type, const modesp::Binding& b, modesp::HAL& hal);

    /// True if a factory is registered for this type.
    static bool is_known(const char* type);

    /// Test hook: clear the registry (host tests).
    static void reset();
};

/// Populate the registry with every compiled backend (explicit, no static-init —
/// mirror of modesp_register_all_drivers / register_all_display_backends).
void register_all_audio_backends();

} // namespace modesp::audio

// ─────────────────────────────────────────────────────────────────────
// Registration — once at file scope in a backend .cpp:
//   static IAudioSink* my_factory(const Binding&, HAL&) { ... }
//   MODESP_REGISTER_AUDIO(max98357a, &my_factory)
// <name> MUST match the "driver" field in bindings.json.
// ─────────────────────────────────────────────────────────────────────
#define MODESP_REGISTER_AUDIO(name, factory_fn)                                \
    extern "C" void modesp_register_audio_##name(void) {                       \
        ::modesp::audio::AudioBackendRegistry::register_backend(               \
            #name, (factory_fn));                                              \
    }
