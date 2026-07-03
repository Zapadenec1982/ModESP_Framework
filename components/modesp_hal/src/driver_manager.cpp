/**
 * @file driver_manager.cpp
 * @brief DriverManager — creates drivers from bindings via the driver registry.
 *
 * Driver-AGNOSTIC: this file knows no concrete driver type. Each binding's
 * driver_type string is looked up in DriverRegistry (populated by the generated
 * modesp_register_all_drivers()), and the matching factory builds the instance.
 * Adding/removing a driver requires no change here — see tools/cmake/modesp_driver.cmake.
 */

#include "modesp/hal/driver_manager.h"
#include "modesp/hal/driver_registry.h"
#include "driver_register_all.h"   // generated — modesp_register_all_drivers()
#include "esp_log.h"

static const char* TAG = "DriverMgr";

namespace modesp {

// ═══════════════════════════════════════════════════════════════
// Init — create all drivers from bindings via the driver registry.
// Driver factories + static pools now live inside each driver component
// (registered through modesp_register_all_drivers); no hardcoded dispatch.
// ═══════════════════════════════════════════════════════════════

bool DriverManager::init(const BindingTable& bindings, HAL& hal) {
    ESP_LOGI(TAG, "Creating drivers for %d bindings...",
             (int)bindings.bindings.size());

    // Populate the registry with every driver enabled in menuconfig (idempotent).
    modesp_register_all_drivers();

    sensors_.clear();
    actuators_.clear();
    sensor_count_ = 0;
    actuator_count_ = 0;

    // Лямбда для реєстрації сенсора
    auto add_sensor = [this](ISensorDriver* drv, const Binding& b) -> bool {
        if (!drv) return false;
        if (sensors_.full()) {
            ESP_LOGE(TAG, "  Sensor '%s' DROPPED — pool full (max %d)",
                     b.role.c_str(), (int)MAX_SENSORS);
            return false;
        }
        SensorEntry entry;
        entry.driver = drv;
        entry.role = b.role;
        entry.module = b.module_name;
        sensors_.push_back(entry);
        sensor_count_++;
        ESP_LOGI(TAG, "  Sensor '%s' [%s] -> module '%s'",
                 b.role.c_str(), b.driver_type.c_str(), b.module_name.c_str());
        return true;
    };

    // Лямбда для реєстрації актуатора
    auto add_actuator = [this](IActuatorDriver* drv, const Binding& b) -> bool {
        if (!drv) return false;
        if (actuators_.full()) {
            ESP_LOGE(TAG, "  Actuator '%s' DROPPED — pool full (max %d)",
                     b.role.c_str(), (int)MAX_ACTUATORS);
            return false;
        }
        ActuatorEntry entry;
        entry.driver = drv;
        entry.role = b.role;
        entry.module = b.module_name;
        actuators_.push_back(entry);
        actuator_count_++;
        ESP_LOGI(TAG, "  Actuator '%s' [%s] -> module '%s'",
                 b.role.c_str(), b.driver_type.c_str(), b.module_name.c_str());
        return true;
    };

    // Phase 1: Create drivers from bindings via the registry.
    for (const auto& binding : bindings.bindings) {
        const char* type = binding.driver_type.c_str();

        // Display/audio backends — module-bound: їх створює модуль-власник у
        // своєму on_bind() (create_display/create_audio), не DriverManager.
        if (DriverRegistry::is_module_backend(type)) continue;

        if (!DriverRegistry::is_known(type)) {
            ESP_LOGW(TAG, "  Driver type '%s' unknown or disabled in menuconfig "
                     "— binding '%s' skipped", type, binding.role.c_str());
            continue;
        }

        if (ISensorDriver* s = DriverRegistry::create_sensor(type, binding, hal)) {
            add_sensor(s, binding);
        } else if (IActuatorDriver* a = DriverRegistry::create_actuator(type, binding, hal)) {
            add_actuator(a, binding);
        } else {
            // Known type but factory failed (pool exhausted / HAL resource
            // missing) — the factory already logged the specific reason.
            ESP_LOGW(TAG, "  Skipping '%s' [%s] — create failed",
                     binding.role.c_str(), type);
        }
    }

    // Phase 2: Initialize all created drivers (видаляє невдалі)
    int failed = 0;
    for (size_t i = 0; i < sensors_.size(); ) {
        if (!sensors_[i].driver->init()) {
            ESP_LOGW(TAG, "Sensor '%s' init failed — disabled",
                     sensors_[i].role.c_str());
            sensors_.erase(sensors_.begin() + i);
            sensor_count_--;
            failed++;
        } else {
            i++;
        }
    }

    for (size_t i = 0; i < actuators_.size(); ) {
        if (!actuators_[i].driver->init()) {
            ESP_LOGW(TAG, "Actuator '%s' init failed — disabled",
                     actuators_[i].role.c_str());
            actuators_.erase(actuators_.begin() + i);
            actuator_count_--;
            failed++;
        } else {
            i++;
        }
    }

    if (failed > 0) {
        ESP_LOGW(TAG, "DriverManager: %d driver(s) failed, continuing with %d sensors, %d actuators",
                 failed, (int)sensor_count_, (int)actuator_count_);
    } else {
        ESP_LOGI(TAG, "DriverManager ready: %d sensors, %d actuators",
                 (int)sensor_count_, (int)actuator_count_);
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════

ISensorDriver* DriverManager::find_sensor(etl::string_view role) {
    for (auto& entry : sensors_) {
        if (entry.role.size() == role.size() &&
            etl::string_view(entry.role.c_str(), entry.role.size()) == role) {
            return entry.driver;
        }
    }
    return nullptr;
}

IActuatorDriver* DriverManager::find_actuator(etl::string_view role) {
    for (auto& entry : actuators_) {
        if (entry.role.size() == role.size() &&
            etl::string_view(entry.role.c_str(), entry.role.size()) == role) {
            return entry.driver;
        }
    }
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════
// Update all drivers
// ═══════════════════════════════════════════════════════════════

void DriverManager::update_all(uint32_t dt_ms) {
    for (auto& entry : sensors_) {
        entry.driver->update(dt_ms);
    }
    for (auto& entry : actuators_) {
        entry.driver->update(dt_ms);
    }
}

} // namespace modesp
