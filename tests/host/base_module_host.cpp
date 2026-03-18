/**
 * @file base_module_host.cpp
 * @brief HOST BUILD: base_module.cpp adapted for host compilation.
 *
 * Includes ESP-IDF mock headers before the real ModESP headers, then
 * provides the same implementation as components/modesp_core/src/base_module.cpp.
 *
 * base_module.cpp itself has no ESP-IDF calls (no esp_log, no esp_timer),
 * so the mock headers are pulled in here purely to satisfy transitive
 * includes from modesp/base_module.h → modesp/shared_state.h →
 * freertos/FreeRTOS.h and freertos/semphr.h.
 */

// ── HOST BUILD: intercept ESP-IDF headers before anything else ──
#include "mocks/freertos_mock.h"
#include "mocks/esp_log_mock.h"
#include "mocks/esp_timer_mock.h"

// ── Real ModESP headers ──
#include "modesp/base_module.h"
#include "modesp/module_manager.h"
#include "modesp/shared_state.h"

namespace modesp {

void BaseModule::publish(const etl::imessage& msg) {
    if (manager_) {
        manager_->publish(msg);
    }
}

bool BaseModule::state_set(const StateKey& key, const StateValue& value, bool track_change) {
    if (shared_state_) {
        return shared_state_->set(key, value, track_change);
    }
    return false;
}

bool BaseModule::state_set(const char* key, int32_t value, bool track_change) {
    return shared_state_ ? shared_state_->set(key, value, track_change) : false;
}

bool BaseModule::state_set(const char* key, float value, bool track_change) {
    return shared_state_ ? shared_state_->set(key, value, track_change) : false;
}

bool BaseModule::state_set(const char* key, bool value, bool track_change) {
    return shared_state_ ? shared_state_->set(key, value, track_change) : false;
}

bool BaseModule::state_set(const char* key, const char* value, bool track_change) {
    return shared_state_ ? shared_state_->set(key, value, track_change) : false;
}

etl::optional<StateValue> BaseModule::state_get(const StateKey& key) const {
    if (shared_state_) {
        return shared_state_->get(key);
    }
    return etl::nullopt;
}

etl::optional<StateValue> BaseModule::state_get(const char* key) const {
    return shared_state_ ? shared_state_->get(key) : etl::nullopt;
}

} // namespace modesp
