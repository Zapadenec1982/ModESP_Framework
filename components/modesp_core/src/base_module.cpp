/**
 * @file base_module.cpp
 * @brief Реалізація BaseModule — делегування до ModuleManager/SharedState
 */

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
