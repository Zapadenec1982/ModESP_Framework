/**
 * @file display_backend_registry.cpp
 * @brief DisplayBackendRegistry — реалізація + список скомпільованих backend-ів.
 */

#include "display/display_backend_registry.h"

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#include <cstring>

namespace modesp::display {

namespace {
struct Entry { const char* type; DisplayFactory fn; };
Entry  s_entries[DisplayBackendRegistry::MAX_BACKENDS];
size_t s_count = 0;

int find_index(const char* type) {
    for (size_t i = 0; i < s_count; ++i) {
        if (std::strcmp(s_entries[i].type, type) == 0) return static_cast<int>(i);
    }
    return -1;
}
} // namespace

bool DisplayBackendRegistry::register_backend(const char* type, DisplayFactory fn) {
    if (!type || !fn) return false;
    if (find_index(type) >= 0) return true;        // ідемпотентно
    if (s_count >= MAX_BACKENDS) return false;
    s_entries[s_count++] = Entry{type, fn};
    return true;
}

IDisplayPort* DisplayBackendRegistry::create(const char* type,
                                             const modesp::Binding& b,
                                             modesp::HAL& hal) {
    if (!type) return nullptr;
    const int i = find_index(type);
    return (i >= 0) ? s_entries[i].fn(b, hal) : nullptr;
}

bool DisplayBackendRegistry::is_known(const char* type) {
    return type && find_index(type) >= 0;
}

void DisplayBackendRegistry::reset() { s_count = 0; }

// ── Список скомпільованих backend-ів (явна реєстрація, без static-init) ──
// Кожен MODESP_REGISTER_DISPLAY генерує extern "C" modesp_register_display_<name>;
// тут їх декларуємо і кличемо лише для увімкнених у menuconfig backend-ів.
extern "C" {
#ifdef CONFIG_MODESP_DISPLAY_AMT630A
    void modesp_register_display_amt630a(void);
#endif
#ifdef CONFIG_MODESP_DISPLAY_AT7456E
    void modesp_register_display_at7456e(void);
#endif
}

void register_all_display_backends() {
#ifdef CONFIG_MODESP_DISPLAY_AMT630A
    modesp_register_display_amt630a();
#endif
#ifdef CONFIG_MODESP_DISPLAY_AT7456E
    modesp_register_display_at7456e();
#endif
}

} // namespace modesp::display
