/**
 * @file action_registry.cpp
 * @brief Implementation of singleton ActionRegistry.
 *
 * See action_registry.h for documentation. No heap allocation — ETL flat_map
 * у-place. Initialize-on-first-use singleton; safe to call instance() з будь-якого
 * module's on_init() before engine.start().
 */

#include "modesp/sequence/action_registry.h"

namespace modesp::sequence {

ActionRegistry& ActionRegistry::instance() {
    // Meyers' singleton — thread-safe initialization у C++11+.
    static ActionRegistry inst;
    return inst;
}

bool ActionRegistry::register_action(const ActionDescriptor& d) {
    // Verify name/hash consistency. Catches manual hash entries that drift
    // out of sync with name string (a real risk if author edits descriptor
    // by hand instead of using djb2_hash16_const).
    if (d.hash != djb2_hash16(d.name)) {
        return false;
    }
    if (actions_.full()) {
        return false;
    }
    if (actions_.find(d.hash) != actions_.end()) {
        return false;  // collision: hash already registered
    }
    actions_.insert({d.hash, d});
    return true;
}

bool ActionRegistry::register_condition(const ActionDescriptor& d) {
    if (d.hash != djb2_hash16(d.name)) {
        return false;
    }
    if (conditions_.full()) {
        return false;
    }
    if (conditions_.find(d.hash) != conditions_.end()) {
        return false;
    }
    conditions_.insert({d.hash, d});
    return true;
}

const ActionDescriptor* ActionRegistry::find_action(uint16_t hash) const {
    auto it = actions_.find(hash);
    return (it != actions_.end()) ? &it->second : nullptr;
}

const ActionDescriptor* ActionRegistry::find_condition(uint16_t hash) const {
    auto it = conditions_.find(hash);
    return (it != conditions_.end()) ? &it->second : nullptr;
}

size_t ActionRegistry::action_count() const {
    return actions_.size();
}

size_t ActionRegistry::condition_count() const {
    return conditions_.size();
}

void ActionRegistry::clear_for_tests() {
    actions_.clear();
    conditions_.clear();
}

}  // namespace modesp::sequence
