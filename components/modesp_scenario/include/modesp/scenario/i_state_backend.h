/**
 * @file i_state_backend.h
 * @brief State backend interface — engine's view of underlying state store.
 *
 * Engine reads і writes through this interface only. Production wires
 * `modesp::SharedState` via а thin adapter (lives у main/). Host tests use
 * `StubStateBackend` (у tests/host/common/). Engine has zero compile-time
 * dependency on `modesp::SharedState` — clean separation.
 *
 * **Design (locked у plan A8):**
 *  - **2 raw virtuals** only — `get_raw` і `set_raw` over the existing
 *    `modesp::StateValue` variant (int32/float/bool/string).
 *  - **Non-virtual typed accessors** (`get<T>`, `set<T>`) inline у header —
 *    zero v-table cost для common usage patterns.
 *  - Caller responsible для key lifetime.
 *
 * **Why not 7 virtuals (one per type)?** Interface gravity. Adding а new
 * `StateValue` variant case requires 4 places change instead of 1.
 *
 * Reference: docs/scenario_engine/03_api_reference.md (post-rebuild).
 */

#pragma once

#include "modesp/types.h"   // StateValue variant

#include <etl/optional.h>
#include <etl/variant.h>

namespace modesp::scenario {

class IStateBackend {
public:
    virtual ~IStateBackend() = default;

    // ── Two raw virtuals — backend implements these ──

    /// Read raw variant by key. Returns false якщо key missing.
    /// @param out filled only on success.
    virtual bool get_raw(const char* key, modesp::StateValue& out) const = 0;

    /// Write raw variant by key. Returns false якщо store rejects
    /// (overflow, immutable key, etc.).
    virtual bool set_raw(const char* key, const modesp::StateValue& value) = 0;

    // ── Non-virtual typed accessors (zero v-table cost) ──

    /// Read typed value. Returns false якщо key missing OR type mismatch.
    template <typename T>
    bool get(const char* key, T& out) const {
        modesp::StateValue v;
        if (!get_raw(key, v)) return false;
        if (auto pv = etl::get_if<T>(&v)) {
            out = *pv;
            return true;
        }
        return false;
    }

    /// Write typed value. Convenience wrapper над set_raw.
    template <typename T>
    bool set(const char* key, T value) {
        return set_raw(key, modesp::StateValue{value});
    }

    /// Overload для string literals (const char* implicit-converts до StringValue).
    bool set(const char* key, const char* value) {
        modesp::StringValue sv{value};
        return set_raw(key, modesp::StateValue{sv});
    }

    /// Overload для char arrays decaying до char* (е.g. local буфер `char buf[16]`).
    bool set(const char* key, char* value) {
        return set(key, static_cast<const char*>(value));
    }
};

}  // namespace modesp::scenario
