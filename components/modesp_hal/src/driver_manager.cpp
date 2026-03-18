/**
 * @file driver_manager.cpp
 * @brief DriverManager implementation — driver creation from bindings
 *
 * Includes concrete driver headers and manages static driver pools.
 * Each binding maps a hardware resource to a driver type and role.
 *
 * Driver types:
 *   "ds18b20"       → DS18B20Driver       (sensor, OneWire)
 *   "digital_input" → DigitalInputDriver   (sensor, GPIO)
 *   "ntc"           → NtcDriver            (sensor, ADC)
 *   "relay"         → RelayDriver          (actuator, GPIO)
 *   "pcf8574_relay" → PCF8574RelayDriver   (actuator, I2C expander)
 *   "pcf8574_input" → PCF8574InputDriver   (sensor, I2C expander)
 */

#include "modesp/hal/driver_manager.h"
#include "ds18b20_driver.h"
#include "relay_driver.h"
#include "digital_input_driver.h"
#include "ntc_driver.h"
#include "pcf8574_relay_driver.h"
#include "pcf8574_input_driver.h"
#include "esp_log.h"

static const char* TAG = "DriverMgr";

namespace modesp {

// ═══════════════════════════════════════════════════════════════
// Static driver pools — zero heap allocation
// ═══════════════════════════════════════════════════════════════

static DS18B20Driver ds18b20_pool[MAX_SENSORS];
static size_t ds18b20_count = 0;

static DigitalInputDriver di_pool[MAX_SENSORS];
static size_t di_count = 0;

static NtcDriver ntc_pool[MAX_SENSORS];
static size_t ntc_count = 0;

static RelayDriver relay_pool[MAX_ACTUATORS];
static size_t relay_count = 0;

static PCF8574RelayDriver pcf_relay_pool[MAX_ACTUATORS];
static size_t pcf_relay_count = 0;

static PCF8574InputDriver pcf_input_pool[MAX_SENSORS];
static size_t pcf_input_count = 0;

// ═══════════════════════════════════════════════════════════════
// Init — create all drivers from bindings
// ═══════════════════════════════════════════════════════════════

bool DriverManager::init(const BindingTable& bindings, HAL& hal) {
    ESP_LOGI(TAG, "Creating drivers for %d bindings...",
             (int)bindings.bindings.size());

    // Reset pools
    ds18b20_count = 0;
    di_count = 0;
    ntc_count = 0;
    relay_count = 0;
    pcf_relay_count = 0;
    pcf_input_count = 0;
    sensors_.clear();
    actuators_.clear();
    sensor_count_ = 0;
    actuator_count_ = 0;

    // Лямбда для реєстрації сенсора
    auto add_sensor = [this](ISensorDriver* drv, const Binding& b) -> bool {
        if (!drv) return false;
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

    // Phase 1: Create drivers from bindings (пропускає невдалі)
    for (const auto& binding : bindings.bindings) {
        bool ok = false;
        if (binding.driver_type == "ds18b20") {
            ok = add_sensor(create_sensor(binding, hal), binding);
        } else if (binding.driver_type == "digital_input") {
            ok = add_sensor(create_di_sensor(binding, hal), binding);
        } else if (binding.driver_type == "ntc") {
            ok = add_sensor(create_ntc_sensor(binding, hal), binding);
        } else if (binding.driver_type == "relay") {
            ok = add_actuator(create_actuator(binding, hal), binding);
        } else if (binding.driver_type == "pcf8574_relay") {
            ok = add_actuator(create_pcf_actuator(binding, hal), binding);
        } else if (binding.driver_type == "pcf8574_input") {
            ok = add_sensor(create_pcf_sensor(binding, hal), binding);
        } else {
            ESP_LOGW(TAG, "  Unknown driver type '%s' for binding '%s'",
                     binding.driver_type.c_str(),
                     binding.hardware_id.c_str());
            continue;
        }
        if (!ok) {
            ESP_LOGW(TAG, "  Skipping '%s' [%s] — create failed",
                     binding.role.c_str(), binding.driver_type.c_str());
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
// Driver creation
// ═══════════════════════════════════════════════════════════════

ISensorDriver* DriverManager::create_sensor(const Binding& binding, HAL& hal) {
    if (binding.driver_type == "ds18b20") {
        if (ds18b20_count >= MAX_SENSORS) {
            ESP_LOGE(TAG, "DS18B20 pool exhausted");
            return nullptr;
        }

        auto* ow_res = hal.find_onewire_bus(
            etl::string_view(binding.hardware_id.c_str(), binding.hardware_id.size()));
        if (!ow_res) {
            ESP_LOGE(TAG, "OneWire bus '%s' not found in HAL",
                     binding.hardware_id.c_str());
            return nullptr;
        }

        auto& drv = ds18b20_pool[ds18b20_count++];
        drv.configure(binding.role.c_str(), ow_res->gpio, 1000,
                      binding.address.empty() ? nullptr : binding.address.c_str());
        return &drv;
    }

    return nullptr;
}

ISensorDriver* DriverManager::create_di_sensor(const Binding& binding, HAL& hal) {
    if (di_count >= MAX_SENSORS) {
        ESP_LOGE(TAG, "DigitalInput pool exhausted");
        return nullptr;
    }

    auto* gpio_res = hal.find_gpio_input(
        etl::string_view(binding.hardware_id.c_str(), binding.hardware_id.size()));
    if (!gpio_res) {
        ESP_LOGE(TAG, "GPIO input '%s' not found in HAL", binding.hardware_id.c_str());
        return nullptr;
    }

    auto& drv = di_pool[di_count++];
    drv.configure(binding.role.c_str(), gpio_res->gpio, gpio_res->pull_up);
    return &drv;
}

ISensorDriver* DriverManager::create_ntc_sensor(const Binding& binding, HAL& hal) {
    if (ntc_count >= MAX_SENSORS) {
        ESP_LOGE(TAG, "NTC pool exhausted");
        return nullptr;
    }

    auto* adc_res = hal.find_adc_channel(
        etl::string_view(binding.hardware_id.c_str(), binding.hardware_id.size()));
    if (!adc_res) {
        ESP_LOGE(TAG, "ADC channel '%s' not found in HAL", binding.hardware_id.c_str());
        return nullptr;
    }

    auto& drv = ntc_pool[ntc_count++];
    drv.configure(binding.role.c_str(), adc_res->gpio, adc_res->atten);
    return &drv;
}

IActuatorDriver* DriverManager::create_actuator(const Binding& binding, HAL& hal) {
    if (binding.driver_type == "relay") {
        if (relay_count >= MAX_ACTUATORS) {
            ESP_LOGE(TAG, "Relay pool exhausted");
            return nullptr;
        }

        auto* gpio_res = hal.find_gpio_output(
            etl::string_view(binding.hardware_id.c_str(), binding.hardware_id.size()));
        if (!gpio_res) {
            ESP_LOGE(TAG, "GPIO output '%s' not found in HAL",
                     binding.hardware_id.c_str());
            return nullptr;
        }

        auto& drv = relay_pool[relay_count++];
        // min_switch_ms = 0 для всіх реле. Захист компресора від коротких циклів
        // реалізовано на рівні EquipmentModule (COMP_MIN_OFF_MS / COMP_MIN_ON_MS)
        // з асиметричними таймерами (180с OFF, 120с ON).
        drv.configure(binding.role.c_str(), gpio_res->gpio, gpio_res->active_high, 0);
        return &drv;
    }

    return nullptr;
}

IActuatorDriver* DriverManager::create_pcf_actuator(const Binding& binding, HAL& hal) {
    if (pcf_relay_count >= MAX_ACTUATORS) {
        ESP_LOGE(TAG, "PCF relay pool exhausted");
        return nullptr;
    }

    // Знайти expander output config по hardware_id ("relay_1")
    auto* out_cfg = hal.find_expander_output(
        etl::string_view(binding.hardware_id.c_str(), binding.hardware_id.size()));
    if (!out_cfg) {
        ESP_LOGE(TAG, "Expander output '%s' not found", binding.hardware_id.c_str());
        return nullptr;
    }

    // Знайти expander resource по expander_id ("relay_exp")
    auto* expander = hal.find_i2c_expander(
        etl::string_view(out_cfg->expander_id.c_str(), out_cfg->expander_id.size()));
    if (!expander) {
        ESP_LOGE(TAG, "Expander '%s' not found", out_cfg->expander_id.c_str());
        return nullptr;
    }

    auto& drv = pcf_relay_pool[pcf_relay_count++];
    drv.configure(binding.role.c_str(), expander, out_cfg->pin, out_cfg->active_high);
    return &drv;
}

ISensorDriver* DriverManager::create_pcf_sensor(const Binding& binding, HAL& hal) {
    if (pcf_input_count >= MAX_SENSORS) {
        ESP_LOGE(TAG, "PCF input pool exhausted");
        return nullptr;
    }

    // Знайти expander input config по hardware_id ("din_1")
    auto* in_cfg = hal.find_expander_input(
        etl::string_view(binding.hardware_id.c_str(), binding.hardware_id.size()));
    if (!in_cfg) {
        ESP_LOGE(TAG, "Expander input '%s' not found", binding.hardware_id.c_str());
        return nullptr;
    }

    // Знайти expander resource
    auto* expander = hal.find_i2c_expander(
        etl::string_view(in_cfg->expander_id.c_str(), in_cfg->expander_id.size()));
    if (!expander) {
        ESP_LOGE(TAG, "Expander '%s' not found", in_cfg->expander_id.c_str());
        return nullptr;
    }

    auto& drv = pcf_input_pool[pcf_input_count++];
    drv.configure(binding.role.c_str(), expander, in_cfg->pin, in_cfg->invert);
    return &drv;
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
