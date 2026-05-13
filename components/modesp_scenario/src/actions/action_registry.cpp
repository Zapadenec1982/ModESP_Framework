/**
 * @file action_registry.cpp
 * @brief ActionRegistry implementation (Phase 1 lift, singleton removed).
 *
 * See action_registry.h for design rationale. No heap, ETL flat_map in-place.
 */

#include "modesp/scenario/action_registry.h"

namespace modesp::scenario {

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

}  // namespace modesp::scenario
