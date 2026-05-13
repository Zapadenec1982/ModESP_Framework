/**
 * @file shared_state_backend.h
 * @brief IStateBackend adapter wrapping modesp::SharedState.
 *
 * Engine код depends on `modesp::scenario::IStateBackend` (DI seam, plan A8).
 * This adapter — application-layer glue — connects the engine до the project's
 * actual SharedState без forcing engine itself to know about SharedState.
 *
 * Lifetime: static у main.cpp, alongside SharedState instance.
 */

#pragma once

#include "modesp/scenario/i_state_backend.h"
#include "modesp/shared_state.h"

class SharedStateBackend : public modesp::scenario::IStateBackend {
public:
    explicit SharedStateBackend(modesp::SharedState& state) : state_(&state) {}

    bool get_raw(const char* key, modesp::StateValue& out) const override {
        auto opt = state_->get(modesp::StateKey(key));
        if (!opt.has_value()) return false;
        out = *opt;
        return true;
    }

    bool set_raw(const char* key, const modesp::StateValue& value) override {
        return state_->set(modesp::StateKey(key), value);
    }

private:
    modesp::SharedState* state_;
};
