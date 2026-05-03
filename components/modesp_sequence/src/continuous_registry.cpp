/**
 * @file continuous_registry.cpp
 * @brief Implementation of singleton ContinuousRegistry.
 *
 * See continuous_behavior.h for documentation.
 */

#include "modesp/sequence/continuous_behavior.h"
#include "modesp/sequence/action_registry.h"  // для djb2_hash16 helper

namespace modesp::sequence {

ContinuousRegistry& ContinuousRegistry::instance() {
    static ContinuousRegistry inst;
    return inst;
}

bool ContinuousRegistry::register_factory(uint16_t hash, const char* name,
                                           ContinuousFactory factory) {
    if (factory == nullptr) {
        return false;
    }
    if (hash != djb2_hash16(name)) {
        return false;
    }
    if (factories_.full()) {
        return false;
    }
    if (factories_.find(hash) != factories_.end()) {
        return false;  // collision
    }
    factories_.insert({hash, factory});
    return true;
}

ContinuousFactory ContinuousRegistry::find(uint16_t hash) const {
    auto it = factories_.find(hash);
    return (it != factories_.end()) ? it->second : nullptr;
}

ContinuousBehavior* ContinuousRegistry::create(uint16_t hash) const {
    auto factory = find(hash);
    return factory ? factory() : nullptr;
}

size_t ContinuousRegistry::count() const {
    return factories_.size();
}

void ContinuousRegistry::clear_for_tests() {
    factories_.clear();
}

}  // namespace modesp::sequence
