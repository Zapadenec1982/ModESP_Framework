/**
 * @file module_manager.cpp
 * @brief Реалізація реєстрації, lifecycle та message bus
 */

#include "modesp/module_manager.h"
#include "esp_log.h"
#include "esp_timer.h"

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

    // Зв'язати адаптер з модулем
    auto& adapter = adapters_[adapter_count_];
    adapter.bind(&module);
    adapter_count_++;

    // Додати модуль до списку
    modules_.push_back(&module);

    // Підключити адаптер до шини (отримує ВСІ повідомлення)
    bus_.subscribe(adapter);

    ESP_LOGI(TAG, "Registered: '%s' (priority=%d)", module.name(),
             static_cast<int>(module.priority()));
    return true;
}

bool ModuleManager::init_all(SharedState& state) {
    shared_state_ = &state;

    // Сортуємо по пріоритету перед ініціалізацією
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

    // Перевести ініціалізовані модулі в RUNNING
    for (auto* module : modules_) {
        if (module->state_ == BaseModule::State::INITIALIZED) {
            module->state_ = BaseModule::State::RUNNING;
        }
    }

    // Перевіряємо чи CRITICAL модулі пройшли init (BUG-024 fix)
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

    // Зупиняємо у зворотному порядку: LOW → NORMAL → HIGH → CRITICAL
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

    // 2. Перевстановити зв'язки (на випадок якщо модуль їх втратив)
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
    // Bubble sort (на 16 елементах це нормально, і робиться один раз)
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
