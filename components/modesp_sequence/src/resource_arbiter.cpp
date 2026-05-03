/**
 * @file resource_arbiter.cpp
 * @brief Implementation of ISA-88 §5.3 resource arbitration.
 *
 * Design notes (per plan Q8):
 *   - Atomic acquires use two-phase commit pattern: первая phase iterates
 *     declarations і checks availability via `can_grant()` без mutating state;
 *     якщо all OK, second phase inserts ownership records. На contention
 *     return early без partial state.
 *   - Single owner per resource у current model (flat_map<u16, OwnerInfo>).
 *     Shared resources: future enhancement (multi-owner shared map keyed by
 *     hash з list-of-owners) — Stage 1.5 if proven needed.
 */

#include "modesp/sequence/resource_arbiter.h"

namespace modesp::sequence {

bool ResourceArbiter::can_grant(uint16_t resource_hash, bool exclusive,
                                SequenceHandle requestor_handle,
                                TrackIdx requestor_track) const {
    auto it = owners_.find(resource_hash);
    if (it == owners_.end()) return true;  // unowned — always grantable

    const OwnerInfo& cur = it->second;

    // Якщо same (handle, track) already owns це — re-grant OK (idempotent).
    if (cur.handle == requestor_handle && cur.track_idx == requestor_track) {
        return true;
    }

    // Different owner: shared+shared OK, anything else blocked.
    if (!exclusive && !cur.exclusive) {
        // Two shared requesters can coexist — но MVP map stores single owner
        // per hash. Treat як conflict для now (conservative). Stage 1.5
        // multi-owner map якщо profiling shows it as bottleneck.
        return false;
    }
    return false;
}

EngineError ResourceArbiter::acquire_scenario(SequenceHandle handle,
                                              const modr_resource_decl* resources,
                                              uint8_t count, uint8_t phase_idx) {
    if (count == 0) return EngineError::OK;
    if (resources == nullptr) return EngineError::INVALID_FILE;

    // Phase 1: dry-run check all entries
    for (uint8_t i = 0; i < count; ++i) {
        const auto& d = resources[i];
        if (!can_grant(d.resource_hash, d.exclusive != 0,
                       handle, TRACK_IDX_SCENARIO)) {
            return EngineError::RESOURCE_CONTENDED;
        }
    }

    // Phase 2: commit all
    for (uint8_t i = 0; i < count; ++i) {
        const auto& d = resources[i];
        // Якщо already owned by це same handle (idempotent re-grant), skip insert
        auto existing = owners_.find(d.resource_hash);
        if (existing != owners_.end() && existing->second.handle == handle) {
            continue;
        }
        if (owners_.full()) {
            // Roll back already-inserted on this call. We tracked count
            // insertions у first `i` iterations — release них by hash.
            for (uint8_t j = 0; j < i; ++j) {
                auto it = owners_.find(resources[j].resource_hash);
                if (it != owners_.end() && it->second.handle == handle
                 && it->second.track_idx == TRACK_IDX_SCENARIO) {
                    owners_.erase(it);
                }
            }
            return EngineError::RESOURCE_CONTENDED;  // arbiter capacity exhausted
        }
        OwnerInfo info{handle, TRACK_IDX_SCENARIO, phase_idx,
                       static_cast<uint8_t>(d.exclusive ? 1 : 0)};
        owners_.insert({d.resource_hash, info});
    }
    return EngineError::OK;
}

void ResourceArbiter::release_scenario(SequenceHandle handle) {
    // Iterate, removing entries owned by handle з track_idx == SCENARIO.
    // ETL flat_map iterators invalidate on erase, so collect-then-remove pattern.
    uint16_t to_remove[MAX_RESOURCES];
    size_t n = 0;
    for (const auto& kv : owners_) {
        if (kv.second.handle == handle && kv.second.track_idx == TRACK_IDX_SCENARIO) {
            if (n < MAX_RESOURCES) to_remove[n++] = kv.first;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        owners_.erase(to_remove[i]);
    }
}

bool ResourceArbiter::try_acquire_phase(SequenceHandle handle, TrackIdx track,
                                       uint8_t phase_idx,
                                       const modr_phase_resource_claim* claims,
                                       uint8_t count) {
    if (count == 0) return true;
    if (claims == nullptr) return false;

    // Phase 1: dry-run check
    for (uint8_t i = 0; i < count; ++i) {
        if (!can_grant(claims[i].resource_hash, claims[i].exclusive != 0,
                       handle, track)) {
            return false;
        }
    }

    // Phase 2: commit, з rollback on capacity exhaustion
    for (uint8_t i = 0; i < count; ++i) {
        auto existing = owners_.find(claims[i].resource_hash);
        if (existing != owners_.end()
         && existing->second.handle == handle
         && existing->second.track_idx == track) {
            continue;  // already owned by цим (handle, track) — idempotent
        }
        if (owners_.full()) {
            for (uint8_t j = 0; j < i; ++j) {
                auto it = owners_.find(claims[j].resource_hash);
                if (it != owners_.end() && it->second.handle == handle
                 && it->second.track_idx == track) {
                    owners_.erase(it);
                }
            }
            return false;
        }
        OwnerInfo info{handle, track, phase_idx,
                       static_cast<uint8_t>(claims[i].exclusive ? 1 : 0)};
        owners_.insert({claims[i].resource_hash, info});
    }
    return true;
}

void ResourceArbiter::release_phase(SequenceHandle handle, TrackIdx track) {
    uint16_t to_remove[MAX_RESOURCES];
    size_t n = 0;
    for (const auto& kv : owners_) {
        if (kv.second.handle == handle && kv.second.track_idx == track) {
            if (n < MAX_RESOURCES) to_remove[n++] = kv.first;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        owners_.erase(to_remove[i]);
    }
}

bool ResourceArbiter::is_owned(uint16_t resource_hash) const {
    return owners_.find(resource_hash) != owners_.end();
}

const OwnerInfo* ResourceArbiter::owner_of(uint16_t resource_hash) const {
    auto it = owners_.find(resource_hash);
    return (it != owners_.end()) ? &it->second : nullptr;
}

}  // namespace modesp::sequence
