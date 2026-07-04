/**
 * @file driver_registry.h
 * @brief Type-string → factory registry for drivers.
 *
 * Replaces the old hardcoded if-else dispatch in DriverManager. Each driver
 * registers a factory under its manifest "driver" type string; DriverManager
 * looks up by binding.driver_type — no hardcoded #includes, pools or branches.
 *
 * A driver disabled in menuconfig (CONFIG_MODESP_DRIVER_<NAME>=n) is simply not
 * compiled, so its registration function is absent and the generated
 * modesp_register_all_drivers() skips it (guarded by #if). Bindings referencing
 * a disabled/unknown driver are skipped with a warning.
 *
 * Registration is EXPLICIT (not static-init): the generated
 * modesp_register_all_drivers() calls each enabled driver's
 * modesp_register_driver_<name>() — deterministic, no WHOLE_ARCHIVE tricks.
 */

#pragma once

#include "modesp/hal/hal_types.h"          // Binding, MAX_SENSORS/ACTUATORS
#include "modesp/hal/driver_interfaces.h"  // ISensorDriver / IActuatorDriver

namespace modesp {

class HAL;

// Display/audio backends are drivers too (drivers/<name>/ + manifest category
// display/audio). Їхні інтерфейси — доменні шви (modesp/hal/display_port.h,
// modesp/hal/audio_sink.h); тут достатньо forward-декларацій — фабрики
// повертають вказівники.
namespace display { class IDisplayPort; }
namespace audio   { class IAudioSink; }
namespace panel   { class IPanelPort; }

using SensorFactory   = ISensorDriver*          (*)(const Binding&, HAL&);
using ActuatorFactory = IActuatorDriver*        (*)(const Binding&, HAL&);
using DisplayFactory  = display::IDisplayPort*  (*)(const Binding&, HAL&);
using AudioFactory    = audio::IAudioSink*      (*)(const Binding&, HAL&);

/// One device found by a driver's discovery scan (bus enumeration before any
/// binding exists). `value` is an optional preview sample (e.g. temperature);
/// `rssi` is an optional signal level for radios (BLE) — 0 = not applicable.
struct DiscoveredDevice {
    char   address[24] = {};   // ROM address / BLE MAC / bus device id
    float  value       = 0.0f;
    bool   has_value   = false;
    int8_t rssi        = 0;    // dBm, 0 = unavailable (wired buses leave it 0)
    char   type[16]    = {};   // identified driver type (unified BLE scan); "" = wired/unknown
    char   summary[24] = {};   // short current-readings string for the manual scan ("24.6C 41%")
};

/// Scan the board resource `hw_id` (id from board.json, e.g. "ow_1") and fill
/// out[0..max). Returns the device count, or <0 if the resource is unknown to
/// this driver (wrong id / wrong bus kind). The driver resolves the resource
/// through HAL itself — the HTTP layer stays bus-agnostic.
using DiscoveryFn = int (*)(HAL&, const char* hw_id, DiscoveredDevice* out, size_t max);

class DriverRegistry {
public:
    static constexpr size_t MAX_DRIVER_TYPES = 16;

    /// Register a factory under a type string. Idempotent: re-registering the
    /// same type is a no-op (so calling register-all twice is safe).
    static bool register_sensor(const char* type, SensorFactory fn);
    static bool register_actuator(const char* type, ActuatorFactory fn);

    /// Create a driver for the given type. Returns nullptr if the type is not
    /// registered (disabled/unknown) or the factory itself failed.
    static ISensorDriver*   create_sensor(const char* type, const Binding&, HAL&);
    static IActuatorDriver* create_actuator(const char* type, const Binding&, HAL&);

    /// True if any (sensor or actuator) factory is registered for this type.
    /// НЕ покриває display/audio — DriverManager створює лише sensors/actuators;
    /// display/audio backend-и модулі беруть самі через create_display/create_audio
    /// у своєму on_bind().
    static bool is_known(const char* type);

    // ── Display/audio backends (module-bound: створюються НЕ DriverManager-ом,
    //    а модулем-власником у on_bind за bindings.json) ──
    static bool register_display(const char* type, DisplayFactory fn);
    static bool register_audio(const char* type, AudioFactory fn);
    static display::IDisplayPort* create_display(const char* type, const Binding&, HAL&);
    static audio::IAudioSink*     create_audio(const char* type, const Binding&, HAL&);
    static bool has_display(const char* type);
    static bool has_audio(const char* type);

    // ── Panel port (single connect-panel backend, e.g. ble_led_panel). Unlike
    //    display/audio (factory-per-type resolved via create_*), a panel is a lone
    //    instance the driver publishes at factory time; the panel module fetches it
    //    in on_bind. Kept separate from is_module_backend: the panel driver is still
    //    a normal actuator created by DriverManager — this only exposes its text/
    //    control surface to the owning module. ──
    static void               set_panel_port(panel::IPanelPort* port);
    static panel::IPanelPort* panel_port();
    /// True для типів, що є module-bound backend-ами (display/audio) — щоб
    /// DriverManager міг тихо пропустити їхні bindings без хибного WARN.
    static bool is_module_backend(const char* type) {
        return has_display(type) || has_audio(type);
    }

    /// Тест-хук: очистити ВСІ реєстри (host-тести). Не використовувати на девайсі.
    static void reset();

    // ── Discovery (optional per driver) ──
    // A discovery-capable driver (manifest: discovery.supported=true) also
    // registers a scan function; the generic scan endpoint dispatches through
    // here — modesp_net never includes a concrete driver again.
    static bool        register_discovery(const char* type, DiscoveryFn fn);
    static DiscoveryFn find_discovery(const char* type);
    static size_t      discovery_count();
    static const char* discovery_type_at(size_t i);   // nullptr if out of range
    static DiscoveryFn discovery_fn_at(size_t i);     // nullptr if out of range
};

} // namespace modesp

// ─────────────────────────────────────────────────────────────────────
// Registration macros — used once at file scope in a driver's .cpp.
//
// Defines `extern "C" void modesp_register_driver_<name>()`, which the
// generated modesp_register_all_drivers() declares and calls. <name> MUST equal
// the manifest "driver" field (== folder name), so the generator can derive the
// symbol mechanically without knowing the C++ class name.
//
//   static ISensorDriver* my_factory(const Binding&, HAL&) { ... }
//   MODESP_REGISTER_SENSOR(ds18b20, &my_factory)
// ─────────────────────────────────────────────────────────────────────
#define MODESP_REGISTER_SENSOR(name, factory_fn)                          \
    extern "C" void modesp_register_driver_##name(void) {                 \
        ::modesp::DriverRegistry::register_sensor(#name, (factory_fn));    \
    }

#define MODESP_REGISTER_ACTUATOR(name, factory_fn)                        \
    extern "C" void modesp_register_driver_##name(void) {                 \
        ::modesp::DriverRegistry::register_actuator(#name, (factory_fn));  \
    }

// Sensor that can also enumerate devices on its bus before any binding exists
// (manifest: discovery.supported=true). Registers both in one hook.
#define MODESP_REGISTER_SENSOR_WITH_DISCOVERY(name, factory_fn, discovery_fn) \
    extern "C" void modesp_register_driver_##name(void) {                     \
        ::modesp::DriverRegistry::register_sensor(#name, (factory_fn));        \
        ::modesp::DriverRegistry::register_discovery(#name, (discovery_fn));   \
    }

// Display backend driver (manifest category "display"). The module that owns
// the seam (DisplayModule) resolves it in on_bind via create_display().
#define MODESP_REGISTER_DISPLAY(name, factory_fn)                          \
    extern "C" void modesp_register_driver_##name(void) {                  \
        ::modesp::DriverRegistry::register_display(#name, (factory_fn));    \
    }

// Audio sink driver (manifest category "audio") — resolved by PlayerModule.
#define MODESP_REGISTER_AUDIO(name, factory_fn)                            \
    extern "C" void modesp_register_driver_##name(void) {                  \
        ::modesp::DriverRegistry::register_audio(#name, (factory_fn));      \
    }
