/**
 * @file equipment_base.cpp
 * @brief EquipmentBase — universal HAL owner implementation
 *
 * Data flow each cycle:
 *   1. read_all_sensors()    — read sensors → EMA filter → SharedState
 *   2. on_product_update()   — product hook (before arbitration)
 *   3. apply_arbitration()   — PRODUCT override: business-specific priority logic
 *   4. apply_all_outputs()   — apply actuator requests to drivers
 *   5. publish_all_states()  — publish actual actuator states + has_* flags
 */

#include "equipment_base.h"
#include "modesp/hal/driver_manager.h"
#include "esp_log.h"
#include <cmath>

static const char* TAG = "Equipment";

// ═══════════════════════════════════════════════════════════════
// Constructor
// ═══════════════════════════════════════════════════════════════

EquipmentBase::EquipmentBase(const char* name, int priority)
    : BaseModule(name, static_cast<modesp::ModulePriority>(priority))
{}

// ═══════════════════════════════════════════════════════════════
// Driver binding — reads roles from manifest "requires"
// ═══════════════════════════════════════════════════════════════

void EquipmentBase::bind_drivers(modesp::DriverManager& dm) {
    // Iterate manifest "requires" roles and find matching drivers
    // For now, we use hardcoded role names from DriverManager
    // (future: read from manifest at runtime)

    role_count_ = 0;

    // Bind all sensors
    for (size_t i = 0; i < dm.sensor_count() && role_count_ < MAX_ROLES; i++) {
        auto* s = dm.sensor_at(i);
        if (!s) continue;
        auto& r = roles_[role_count_];
        strncpy(r.role, s->role(), sizeof(r.role) - 1);
        strncpy(r.type, "sensor", sizeof(r.type) - 1);
        r.as_sensor = s;
        // bound = role name is set
        ESP_LOGI(TAG, "  Sensor '%s' [%s] bound", r.role, s->type());
        role_count_++;
    }

    // Bind all actuators
    for (size_t i = 0; i < dm.actuator_count() && role_count_ < MAX_ROLES; i++) {
        auto* a = dm.actuator_at(i);
        if (!a) continue;
        auto& r = roles_[role_count_];
        strncpy(r.role, a->role(), sizeof(r.role) - 1);
        strncpy(r.type, "actuator", sizeof(r.type) - 1);
        r.as_actuator = a;
        // bound = role name is set
        ESP_LOGI(TAG, "  Actuator '%s' bound", r.role);
        role_count_++;
    }

    ESP_LOGI(TAG, "Bound %d roles (%d sensors, %d actuators)",
             (int)role_count_, (int)dm.sensor_count(), (int)dm.actuator_count());
}

// ═══════════════════════════════════════════════════════════════
// Role lookup
// ═══════════════════════════════════════════════════════════════

int EquipmentBase::find_role(const char* role) const {
    for (size_t i = 0; i < role_count_; i++) {
        if (strcmp(roles_[i].role, role) == 0) return static_cast<int>(i);
    }
    return -1;
}

modesp::ISensorDriver* EquipmentBase::sensor(const char* role) const {
    int idx = find_role(role);
    return (idx >= 0) ? roles_[idx].as_sensor : nullptr;
}

modesp::IActuatorDriver* EquipmentBase::actuator(const char* role) const {
    int idx = find_role(role);
    return (idx >= 0) ? roles_[idx].as_actuator : nullptr;
}

bool EquipmentBase::has_driver(const char* role) const {
    int idx = find_role(role);
    return (idx >= 0) && (roles_[idx].as_sensor || roles_[idx].as_actuator);
}

// ═══════════════════════════════════════════════════════════════
// Sensor helpers
// ═══════════════════════════════════════════════════════════════

float EquipmentBase::read_sensor_value(const char* role) {
    int idx = find_role(role);
    if (idx < 0 || !roles_[idx].as_sensor) return 0.0f;

    auto& r = roles_[idx];
    float temp = 0.0f;
    if (r.as_sensor->read(temp)) {
        if (!r.ema_init) {
            r.ema_value = temp;
            r.ema_init = true;
        } else if (ema_alpha_ > 0.0f) {
            r.ema_value += (temp - r.ema_value) * ema_alpha_;
        } else {
            r.ema_value = temp;
        }
        return roundf(r.ema_value * 100.0f) / 100.0f;
    }
    return r.ema_value;  // Return last known value
}

bool EquipmentBase::is_sensor_healthy(const char* role) const {
    int idx = find_role(role);
    if (idx < 0 || !roles_[idx].as_sensor) return false;
    return roles_[idx].as_sensor->is_healthy();
}

// ═══════════════════════════════════════════════════════════════
// Actuator helpers
// ═══════════════════════════════════════════════════════════════

void EquipmentBase::set_actuator(const char* role, bool state) {
    int idx = find_role(role);
    if (idx >= 0) roles_[idx].output_req = state;
}

bool EquipmentBase::get_actuator_state(const char* role) const {
    int idx = find_role(role);
    if (idx < 0 || !roles_[idx].as_actuator) return false;
    return roles_[idx].as_actuator->get_state();
}

// ═══════════════════════════════════════════════════════════════
// ShortCycleGuard
// ═══════════════════════════════════════════════════════════════

bool EquipmentBase::ShortCycleGuard::apply(bool requested, uint32_t dt_ms) {
    since_change_ms += dt_ms;
    if (since_change_ms > modesp::TIMER_SATISFIED)
        since_change_ms = modesp::TIMER_SATISFIED;

    if (requested != current_state) {
        if (requested) {
            // Want ON — check min OFF time
            if (since_change_ms < min_off_ms) return current_state;
        } else {
            // Want OFF — check min ON time
            if (since_change_ms < min_on_ms) return current_state;
        }
        current_state = requested;
        since_change_ms = 0;
    }
    return current_state;
}

// ═══════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════

bool EquipmentBase::on_init() {
    // Publish has_* capability flags for all roles
    for (size_t i = 0; i < role_count_; i++) {
        char key[48];
        snprintf(key, sizeof(key), "equipment.has_%s", roles_[i].role);
        state_set(key, true);
    }

    // Detect driver types (NTC, DS18B20)
    bool uses_ntc = false;
    bool uses_ds18b20 = false;
    for (size_t i = 0; i < role_count_; i++) {
        if (!roles_[i].as_sensor) continue;
        const char* t = roles_[i].as_sensor->type();
        if (strcmp(t, "ntc") == 0) uses_ntc = true;
        if (strcmp(t, "ds18b20") == 0) uses_ds18b20 = true;
    }
    state_set("equipment.has_ntc_driver", uses_ntc);
    state_set("equipment.has_ds18b20_driver", uses_ds18b20);

    // Initialize sensor state keys
    for (size_t i = 0; i < role_count_; i++) {
        if (roles_[i].as_sensor) {
            char key[48];
            snprintf(key, sizeof(key), "equipment.%s", roles_[i].role);
            state_set(key, 0.0f);
        }
        if (roles_[i].as_actuator) {
            char key[48];
            snprintf(key, sizeof(key), "equipment.%s", roles_[i].role);
            state_set(key, false);
        }
    }

    // Product-specific init
    on_product_init();

    ESP_LOGI(TAG, "Initialized (%d roles)", (int)role_count_);
    return true;
}

void EquipmentBase::on_update(uint32_t dt_ms) {
    // 1. Read EMA filter coefficient
    int coeff = read_int("equipment.filter_coeff", 4);
    ema_alpha_ = (coeff > 0) ? 1.0f / (coeff + 1) : 1.0f;

    // 2. Read all sensors → SharedState
    read_all_sensors();

    // 3. Product hook (before arbitration)
    on_product_update(dt_ms);

    // 4. Product arbitration (pure virtual)
    apply_arbitration(dt_ms);

    // 5. Apply output requests to drivers
    apply_all_outputs();

    // 6. Publish actual actuator states
    publish_all_states();
}

void EquipmentBase::on_message(const etl::imessage& msg) {
    if (msg.get_message_id() == modesp::msg_id::SYSTEM_SAFE_MODE) {
        for (size_t i = 0; i < role_count_; i++) {
            roles_[i].output_req = false;
        }
        apply_all_outputs();
        publish_all_states();
        ESP_LOGW(TAG, "SAFE MODE — all outputs OFF");
    }
}

void EquipmentBase::on_stop() {
    for (size_t i = 0; i < role_count_; i++) {
        roles_[i].output_req = false;
    }
    apply_all_outputs();
    ESP_LOGI(TAG, "Equipment stopped — all outputs OFF");
}

// ═══════════════════════════════════════════════════════════════
// Internal: read all sensors
// ═══════════════════════════════════════════════════════════════

void EquipmentBase::read_all_sensors() {
    for (size_t i = 0; i < role_count_; i++) {
        auto& r = roles_[i];
        if (!r.as_sensor) continue;

        char key[48];

        // Temperature/analog sensors
        if (strcmp(r.as_sensor->type(), "digital_input") != 0) {
            float val = read_sensor_value(r.role);
            snprintf(key, sizeof(key), "equipment.%s", r.role);
            state_set(key, val);

            // Health key: equipment.sensor{N}_ok or equipment.{role}_ok
            snprintf(key, sizeof(key), "equipment.%s_ok", r.role);
            state_set(key, r.as_sensor->is_healthy());
        } else {
            // Digital input — read as bool
            float val = 0.0f;
            r.as_sensor->read(val);
            snprintf(key, sizeof(key), "equipment.%s", r.role);
            state_set(key, val > 0.5f);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Internal: apply outputs
// ═══════════════════════════════════════════════════════════════

void EquipmentBase::apply_all_outputs() {
    for (size_t i = 0; i < role_count_; i++) {
        if (roles_[i].as_actuator) {
            roles_[i].as_actuator->set(roles_[i].output_req);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Internal: publish actuator states
// ═══════════════════════════════════════════════════════════════

void EquipmentBase::publish_all_states() {
    for (size_t i = 0; i < role_count_; i++) {
        if (!roles_[i].as_actuator) continue;

        char key[48];
        snprintf(key, sizeof(key), "equipment.%s", roles_[i].role);
        bool actual = roles_[i].as_actuator->get_state();
        state_set(key, actual);
    }
}

// ═══════════════════════════════════════════════════════════════
// Test helpers
// ═══════════════════════════════════════════════════════════════

#ifdef HOST_BUILD
void EquipmentBase::inject_sensor(const char* role, modesp::ISensorDriver* s) {
    int idx = find_role(role);
    if (idx >= 0) {
        roles_[idx].as_sensor = s;
    } else if (role_count_ < MAX_ROLES) {
        auto& r = roles_[role_count_];
        strncpy(r.role, role, sizeof(r.role) - 1);
        strncpy(r.type, "sensor", sizeof(r.type) - 1);
        r.as_sensor = s;
        // bound = role name is set
        role_count_++;
    }
}

void EquipmentBase::inject_actuator(const char* role, modesp::IActuatorDriver* a) {
    int idx = find_role(role);
    if (idx >= 0) {
        roles_[idx].as_actuator = a;
    } else if (role_count_ < MAX_ROLES) {
        auto& r = roles_[role_count_];
        strncpy(r.role, role, sizeof(r.role) - 1);
        strncpy(r.type, "actuator", sizeof(r.type) - 1);
        r.as_actuator = a;
        // bound = role name is set
        role_count_++;
    }
}
#endif
