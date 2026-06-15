/**
 * @file display_backend_registry.h
 * @brief Type-string → factory registry for display backends (ADR-002).
 *
 * Дисплейний аналог modesp::DriverRegistry. Кожен backend (amt630a, at7456e…)
 * реєструє фабрику під своїм рядком-типом (== поле "driver" у bindings.json);
 * DisplayModule резолвить активний backend за binding.driver_type — без
 * hardcoded #if-каскаду. Backend, вимкнений у menuconfig, не компілюється, тож
 * його register-функція відсутня і register_all_display_backends() її пропускає.
 *
 * Шар: цей реєстр живе у display-модулі (а не в modesp_hal), бо IDisplayPort —
 * доменний інтерфейс дисплея. Фабрика отримує (Binding&, HAL&) — як драйверні
 * фабрики — і резолвить шину через HAL::find_i2c_bus (board.json = джерело
 * істини для пінів). Binding/HAL — лише forward-decl: типедеф фабрики бере
 * посилання, тож повні визначення (з ESP-IDF) тут не потрібні.
 */

#pragma once

#include "display/display_port.h"
#include <cstddef>

namespace modesp {
struct Binding;   // hal_types.h — повне визначення лише у фабриках backend-ів
class  HAL;
}

namespace modesp::display {

/// Фабрика backend-у: (binding, hal) → IDisplayPort* (nullptr при помилці).
using DisplayFactory = IDisplayPort* (*)(const modesp::Binding&, modesp::HAL&);

class DisplayBackendRegistry {
public:
    static constexpr size_t MAX_BACKENDS = 8;

    /// Зареєструвати фабрику під рядком-типом. Ідемпотентно (повторна
    /// реєстрація того ж типу — no-op), тож register-all можна звати двічі.
    static bool register_backend(const char* type, DisplayFactory fn);

    /// Створити backend для типу. nullptr якщо тип не зареєстровано
    /// (вимкнений/невідомий) або фабрика повернула помилку.
    static IDisplayPort* create(const char* type, const modesp::Binding& b, modesp::HAL& hal);

    /// Чи зареєстровано фабрику для цього типу.
    static bool is_known(const char* type);

    /// Тест-хук: очистити реєстр (host-тести).
    static void reset();
};

/// Заповнити реєстр усіма скомпільованими backend-ами (явно, без static-init —
/// дзеркало modesp_register_all_drivers). LOG — неявний fallback без binding.
void register_all_display_backends();

} // namespace modesp::display

// ─────────────────────────────────────────────────────────────────────
// Реєстрація — один раз на file-scope у .cpp backend-у:
//   static IDisplayPort* my_factory(const Binding&, HAL&) { ... }
//   MODESP_REGISTER_DISPLAY(amt630a, &my_factory)
// <name> МУСИТЬ збігатися з полем "driver" у bindings.json.
// ─────────────────────────────────────────────────────────────────────
#define MODESP_REGISTER_DISPLAY(name, factory_fn)                              \
    extern "C" void modesp_register_display_##name(void) {                     \
        ::modesp::display::DisplayBackendRegistry::register_backend(            \
            #name, (factory_fn));                                              \
    }
