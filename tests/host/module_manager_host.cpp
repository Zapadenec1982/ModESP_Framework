/**
 * @file module_manager_host.cpp
 * @brief HOST BUILD: module_manager.cpp adapted for host compilation.
 *
 * Includes ESP-IDF mock headers before the real ModESP headers, then
 * provides the same implementation as components/modesp_core/src/module_manager.cpp.
 *
 * module_manager.cpp uses:
 *   - esp_log.h    → replaced by esp_log_mock.h (ESP_LOG* → printf)
 *   - esp_timer.h  → replaced by esp_timer_mock.h (esp_timer_get_time → 0)
 *   - freertos/*   → replaced by freertos_mock.h
 *
 * Implementation is identical to the original module_manager.cpp.
 */

// ── HOST BUILD: intercept ESP-IDF headers before anything else ──
#include "mocks/freertos_mock.h"
#include "mocks/esp_log_mock.h"
#include "mocks/esp_timer_mock.h"

// ── Real ModESP headers ──
#include "modesp/module_manager.h"

#include "etl/algorithm.h"

static const char* TAG = "ModuleManager";

namespace modesp {

ModuleManager::ModuleManager() {}

bool ModuleManager::register_module(BaseModule& module) {
    if (modules_.full()) {
        ESP_LOGE(TAG, "Cannot register '%s': module list full (%d)",
                 module.name(), MODESP_MAX_MODULES);
        return false;
    }

    if (adapter_count_ >= adapters_.size()) {
        ESP_LOGE(TAG, "Cannot register '%s': adapter pool full", module.name());
        return false;
    }

    // Bind adapter to module
    auto& adapter = adapters_[adapter_count_];
    adapter.bind(&module);
    adapter_count_++;

    // Add module to list
    modules_.push_back(&module);

    // Subscribe adapter to bus (receives ALL messages)
    bus_.subscribe(adapter);

    ESP_LOGI(TAG, "Registered: '%s' (priority=%d)", module.name(),
             static_cast<int>(module.priority()));
    return true;
}

bool ModuleManager::init_all(SharedState& state) {
    shared_state_ = &state;

    // Sort by priority before initializing
    sort_by_priority();

    ESP_LOGI(TAG, "Initializing %d modules...", modules_.size());

    for (auto* module : modules_) {
        // Skip modules already initialized (two-phase boot support)
        if (module->state_ != BaseModule::State::CREATED) {
            ESP_LOGD(TAG, "  Skip: '%s' (already %d)", module->name(),
                     static_cast<int>(module->state_));
            continue;
        }

        // Set linkage
        module->manager_ = this;
        module->shared_state_ = shared_state_;

        ESP_LOGI(TAG, "  Init: '%s'", module->name());
        if (module->on_init()) {
            module->state_ = BaseModule::State::INITIALIZED;
        } else {
            ESP_LOGE(TAG, "  FAILED: '%s'", module->name());
            module->state_ = BaseModule::State::ERROR;
            // Continue — other modules may still work
        }
    }

    // Transition initialized modules to RUNNING
    for (auto* module : modules_) {
        if (module->state_ == BaseModule::State::INITIALIZED) {
            module->state_ = BaseModule::State::RUNNING;
        }
    }

    // Check that CRITICAL modules passed init (BUG-024 fix)
    bool critical_ok = true;
    for (auto* module : modules_) {
        if (module->priority() == ModulePriority::CRITICAL &&
            module->state_ == BaseModule::State::ERROR) {
            ESP_LOGE(TAG, "CRITICAL module '%s' failed init!", module->name());
            critical_ok = false;
        }
    }

    ESP_LOGI(TAG, "All modules initialized%s", critical_ok ? "" : " (with CRITICAL failures!)");
    return critical_ok;
}

void ModuleManager::update_all(uint32_t dt_ms) {
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    for (auto* module : modules_) {
        if (module->state_ != BaseModule::State::RUNNING) continue;

        module->on_update(dt_ms);
        module->last_update_ms_ = now_ms;
    }
}

void ModuleManager::stop_all() {
    ESP_LOGI(TAG, "Stopping %d modules (reverse priority)...", modules_.size());

    // Stop in reverse order: LOW → NORMAL → HIGH → CRITICAL
    for (int i = modules_.size() - 1; i >= 0; i--) {
        auto* module = modules_[i];
        if (module->state_ == BaseModule::State::RUNNING) {
            ESP_LOGI(TAG, "  Stop: '%s'", module->name());
            module->on_stop();
            module->state_ = BaseModule::State::STOPPED;
        }
    }

    ESP_LOGI(TAG, "All modules stopped");
}

bool ModuleManager::restart_module(BaseModule& module) {
    ESP_LOGW(TAG, "Restarting module '%s'...", module.name());

    // 1. Stop
    if (module.state_ == BaseModule::State::RUNNING) {
        module.on_stop();
        module.state_ = BaseModule::State::STOPPED;
    }

    // 2. Re-establish linkage (in case module lost them)
    module.manager_ = this;
    module.shared_state_ = shared_state_;

    // 3. Re-init
    if (module.on_init()) {
        module.state_ = BaseModule::State::RUNNING;
        module.last_update_ms_ = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGI(TAG, "Module '%s' restarted successfully", module.name());
        return true;
    } else {
        module.state_ = BaseModule::State::ERROR;
        ESP_LOGE(TAG, "Module '%s' restart FAILED", module.name());
        return false;
    }
}

void ModuleManager::publish(const etl::imessage& msg) {
    bus_.receive(msg);
}

void ModuleManager::for_each(ModuleCallback cb, void* user_data) {
    for (auto* module : modules_) {
        cb(*module, user_data);
    }
}

void ModuleManager::for_each_module(ModuleVisitor visitor, void* user_data) const {
    for (const auto* module : modules_) {
        visitor(*module, user_data);
    }
}

void ModuleManager::sort_by_priority() {
    // Bubble sort (fine for <=16 elements, called once at init)
    for (size_t i = 0; i < modules_.size(); i++) {
        for (size_t j = i + 1; j < modules_.size(); j++) {
            if (static_cast<uint8_t>(modules_[j]->priority()) <
                static_cast<uint8_t>(modules_[i]->priority())) {
                auto* tmp = modules_[i];
                modules_[i] = modules_[j];
                modules_[j] = tmp;
            }
        }
    }
}

} // namespace modesp
