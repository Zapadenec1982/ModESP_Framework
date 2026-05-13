/**
 * @file stub_state_backend.h
 * @brief In-memory IStateBackend для host tests.
 *
 * Mimics `modesp::SharedState` behavior без freertos/etl mock cascades.
 * Linear scan (small key counts у tests). Empty-handed reads return false.
 */

#pragma once

#include "modesp/scenario/i_state_backend.h"
#include "modesp/types.h"

#include <string>
#include <unordered_map>

namespace modesp::scenario::testing {

class StubStateBackend : public IStateBackend {
public:
    bool get_raw(const char* key, modesp::StateValue& out) const override {
        auto it = store_.find(key);
        if (it == store_.end()) return false;
        out = it->second;
        return true;
    }

    bool set_raw(const char* key, const modesp::StateValue& v) override {
        store_[key] = v;
        return true;
    }

    void clear() { store_.clear(); }
    size_t size() const { return store_.size(); }

    bool has(const char* key) const {
        return store_.find(key) != store_.end();
    }

private:
    std::unordered_map<std::string, modesp::StateValue> store_;
};

}  // namespace modesp::scenario::testing
