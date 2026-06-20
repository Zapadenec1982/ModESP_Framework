/**
 * @file audio_backend_registry.cpp
 * @brief AudioBackendRegistry — implementation + list of compiled backends.
 */

#include "player/audio_backend_registry.h"

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#include <cstring>

namespace modesp::audio {

namespace {
struct Entry { const char* type; AudioFactory fn; };
Entry  s_entries[AudioBackendRegistry::MAX_BACKENDS];
size_t s_count = 0;

int find_index(const char* type) {
    for (size_t i = 0; i < s_count; ++i) {
        if (std::strcmp(s_entries[i].type, type) == 0) return static_cast<int>(i);
    }
    return -1;
}
} // namespace

bool AudioBackendRegistry::register_backend(const char* type, AudioFactory fn) {
    if (!type || !fn) return false;
    if (find_index(type) >= 0) return true;        // idempotent
    if (s_count >= MAX_BACKENDS) return false;
    s_entries[s_count++] = Entry{type, fn};
    return true;
}

IAudioSink* AudioBackendRegistry::create(const char* type,
                                         const modesp::Binding& b,
                                         modesp::HAL& hal) {
    if (!type) return nullptr;
    const int i = find_index(type);
    return (i >= 0) ? s_entries[i].fn(b, hal) : nullptr;
}

bool AudioBackendRegistry::is_known(const char* type) {
    return type && find_index(type) >= 0;
}

void AudioBackendRegistry::reset() { s_count = 0; }

// ── List of compiled backends (explicit registration, no static-init) ──
// Each MODESP_REGISTER_AUDIO emits extern "C" modesp_register_audio_<name>;
// declare and call them only for backends enabled in menuconfig.
extern "C" {
#ifdef CONFIG_MODESP_AUDIO_MAX98357A
    void modesp_register_audio_max98357a(void);
#endif
}

void register_all_audio_backends() {
#ifdef CONFIG_MODESP_AUDIO_MAX98357A
    modesp_register_audio_max98357a();
#endif
}

} // namespace modesp::audio
