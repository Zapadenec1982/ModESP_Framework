/**
 * @file driver_registry.cpp
 * @brief DriverRegistry storage — fixed-capacity, zero heap.
 */

#include "modesp/hal/driver_registry.h"
#include <cstring>

namespace modesp {

namespace {

struct SensorEntry    { const char* type; SensorFactory   fn; };
struct ActuatorEntry  { const char* type; ActuatorFactory fn; };
struct DiscoveryEntry { const char* type; DiscoveryFn     fn; };
struct DisplayEntry   { const char* type; DisplayFactory  fn; };
struct AudioEntry     { const char* type; AudioFactory    fn; };

SensorEntry    s_sensors[DriverRegistry::MAX_DRIVER_TYPES];
size_t         s_sensor_n = 0;
ActuatorEntry  s_actuators[DriverRegistry::MAX_DRIVER_TYPES];
size_t         s_actuator_n = 0;
DiscoveryEntry s_discoveries[DriverRegistry::MAX_DRIVER_TYPES];
size_t         s_discovery_n = 0;
DisplayEntry   s_displays[DriverRegistry::MAX_DRIVER_TYPES];
size_t         s_display_n = 0;
AudioEntry     s_audios[DriverRegistry::MAX_DRIVER_TYPES];
size_t         s_audio_n = 0;
panel::IPanelPort* s_panel_port = nullptr;   // single connect-panel backend

bool sensor_has(const char* type) {
    for (size_t i = 0; i < s_sensor_n; ++i)
        if (strcmp(s_sensors[i].type, type) == 0) return true;
    return false;
}
bool actuator_has(const char* type) {
    for (size_t i = 0; i < s_actuator_n; ++i)
        if (strcmp(s_actuators[i].type, type) == 0) return true;
    return false;
}

} // namespace

bool DriverRegistry::register_sensor(const char* type, SensorFactory fn) {
    if (sensor_has(type)) return true;                  // idempotent
    if (s_sensor_n >= MAX_DRIVER_TYPES) return false;
    s_sensors[s_sensor_n++] = {type, fn};
    return true;
}

bool DriverRegistry::register_actuator(const char* type, ActuatorFactory fn) {
    if (actuator_has(type)) return true;                // idempotent
    if (s_actuator_n >= MAX_DRIVER_TYPES) return false;
    s_actuators[s_actuator_n++] = {type, fn};
    return true;
}

ISensorDriver* DriverRegistry::create_sensor(const char* type, const Binding& b, HAL& h) {
    for (size_t i = 0; i < s_sensor_n; ++i)
        if (strcmp(s_sensors[i].type, type) == 0) return s_sensors[i].fn(b, h);
    return nullptr;
}

IActuatorDriver* DriverRegistry::create_actuator(const char* type, const Binding& b, HAL& h) {
    for (size_t i = 0; i < s_actuator_n; ++i)
        if (strcmp(s_actuators[i].type, type) == 0) return s_actuators[i].fn(b, h);
    return nullptr;
}

bool DriverRegistry::is_known(const char* type) {
    return sensor_has(type) || actuator_has(type);
}

bool DriverRegistry::register_discovery(const char* type, DiscoveryFn fn) {
    if (find_discovery(type)) return true;              // idempotent
    if (s_discovery_n >= MAX_DRIVER_TYPES) return false;
    s_discoveries[s_discovery_n++] = {type, fn};
    return true;
}

DiscoveryFn DriverRegistry::find_discovery(const char* type) {
    for (size_t i = 0; i < s_discovery_n; ++i)
        if (strcmp(s_discoveries[i].type, type) == 0) return s_discoveries[i].fn;
    return nullptr;
}

size_t DriverRegistry::discovery_count() { return s_discovery_n; }

const char* DriverRegistry::discovery_type_at(size_t i) {
    return i < s_discovery_n ? s_discoveries[i].type : nullptr;
}

DiscoveryFn DriverRegistry::discovery_fn_at(size_t i) {
    return i < s_discovery_n ? s_discoveries[i].fn : nullptr;
}

bool DriverRegistry::register_display(const char* type, DisplayFactory fn) {
    if (!type || !fn) return false;
    if (has_display(type)) return true;                 // idempotent
    if (s_display_n >= MAX_DRIVER_TYPES) return false;
    s_displays[s_display_n++] = {type, fn};
    return true;
}

bool DriverRegistry::register_audio(const char* type, AudioFactory fn) {
    if (!type || !fn) return false;
    if (has_audio(type)) return true;                   // idempotent
    if (s_audio_n >= MAX_DRIVER_TYPES) return false;
    s_audios[s_audio_n++] = {type, fn};
    return true;
}

display::IDisplayPort* DriverRegistry::create_display(const char* type, const Binding& b, HAL& h) {
    if (!type) return nullptr;
    for (size_t i = 0; i < s_display_n; ++i)
        if (strcmp(s_displays[i].type, type) == 0) return s_displays[i].fn(b, h);
    return nullptr;
}

audio::IAudioSink* DriverRegistry::create_audio(const char* type, const Binding& b, HAL& h) {
    if (!type) return nullptr;
    for (size_t i = 0; i < s_audio_n; ++i)
        if (strcmp(s_audios[i].type, type) == 0) return s_audios[i].fn(b, h);
    return nullptr;
}

bool DriverRegistry::has_display(const char* type) {
    if (!type) return false;
    for (size_t i = 0; i < s_display_n; ++i)
        if (strcmp(s_displays[i].type, type) == 0) return true;
    return false;
}

bool DriverRegistry::has_audio(const char* type) {
    if (!type) return false;
    for (size_t i = 0; i < s_audio_n; ++i)
        if (strcmp(s_audios[i].type, type) == 0) return true;
    return false;
}

void DriverRegistry::set_panel_port(panel::IPanelPort* port) { s_panel_port = port; }
panel::IPanelPort* DriverRegistry::panel_port() { return s_panel_port; }

void DriverRegistry::reset() {
    s_sensor_n = s_actuator_n = s_discovery_n = s_display_n = s_audio_n = 0;
    s_panel_port = nullptr;
}

} // namespace modesp
