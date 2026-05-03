/**
 * @file resource_arbiter.h
 * @brief ISA-88 §5.3 resource arbitration для SequenceEngine.
 *
 * Two ownership scopes (per plan Q8):
 *   - Scenario-scope: claimed at scenario start, released at completion / abort /
 *     unload. Long-held resources (dedicated controller, exclusive heater).
 *   - Phase-scope: claimed at phase entry, released at phase exit. Shared
 *     resources used briefly within a phase (irrigation pump shared across
 *     greenhouse zones).
 *
 * Design properties:
 *   - **Atomic scenario acquire:** all-or-nothing. Якщо any resource у the
 *     scenario's declaration is contended, none acquired (no partial state).
 *     Caller sees RESOURCE_CONTENDED і can react (e.g., refuse to start).
 *   - **Best-effort phase acquire:** boolean return; on contention, phase enters
 *     waiting state і retries next tick (engine policy у Step 11+).
 *   - **Pure module:** does not write SharedState. Engine layer mirrors
 *     ownership to `scenario.resource.<hash>.owner` keys on acquire/release
 *     events, keeping arbiter testable у isolation.
 *   - **Zero heap:** ETL flat_map з compile-time capacity (32 concurrent
 *     resource ownerships across all instances).
 *
 * Cross-module note (per plan Q8): для MVP, conflicts BETWEEN scenarios і
 * business modules (e.g. simple_thermo) are not arbitrated — last-write-wins
 * on the underlying SharedState request key. Recipe authors disable conflicting
 * modules через set_state on phase entry; abort handlers re-enable. Engine
 * surfaces `scenario.engine_recovery_pending = true` після crash recovery
 * для user-actionable visibility. Cross-module arbiter integration з Equipment
 * Manager landed Stage 1.5.
 *
 * Reference: docs/sequence_engine/06_resource_arbitration.md, ADR-0005.
 */

#pragma once

#include "modesp/sequence/action_param.h"
#include "modesp/sequence/engine_error.h"
#include "modesp/sequence/modr_format.h"
#include "etl/flat_map.h"

#include <cstdint>

namespace modesp::sequence {

/// Maximum concurrent resource ownerships across all instances. ETL capacity
/// is compile-time bound — bumped via Kconfig at Stage 1.5 if needed.
constexpr size_t MAX_RESOURCES = 32;

/// Sentinel track index used for scenario-scope ownership.
/// Real track indices are 0..MAX_TRACKS_PER_SCENARIO-1.
constexpr TrackIdx TRACK_IDX_SCENARIO = 0xFF;

/// Owner record. Stored у arbiter's flat_map keyed by resource_hash.
struct OwnerInfo {
    SequenceHandle handle;      ///< owning scenario instance handle (1..MAX_SEQUENCES)
    TrackIdx       track_idx;   ///< owning track, або TRACK_IDX_SCENARIO для scenario-scope
    uint8_t        phase_idx;   ///< owning phase index (informational, used для diagnostic)
    uint8_t        exclusive;   ///< 1 = exclusive hold; 0 = shared (multi-owner allowed for shared)
};

/**
 * Resource arbiter — singleton-ish (typically engine holds one). Tracks which
 * (handle, track, phase) tuple owns each resource. Caller-driven acquire/
 * release; arbiter does no autonomous releasing. Engine releases tied to
 * lifecycle events (scenario start/end, phase enter/exit).
 *
 * Thread safety: methods called only from engine update task (single-threaded
 * у current design). Cross-task callers must serialize at engine layer.
 */
class ResourceArbiter {
public:
    ResourceArbiter() = default;

    /**
     * Atomically acquire all listed scenario-scope resources for `handle`.
     * Returns OK on success, RESOURCE_CONTENDED if ANY resource is already held
     * by а DIFFERENT (handle, track) pair.
     *
     * **MVP shared semantics caveat:** the underlying flat_map stores ONE owner
     * per resource hash. Shared+shared between different scenarios is treated
     * як conflict (returns false from can_grant). Same-owner re-grant IS
     * idempotent. Multi-owner shared map (true shared+shared coexistence) is
     * Stage 1.5. Recipes that declare `exclusive: 0` get the same behavior
     * як `exclusive: 1` until then.
     *
     * On contention, NO resources are acquired (atomic all-or-nothing).
     *
     * @param handle    requesting scenario instance (1..MAX_SEQUENCES)
     * @param resources array of modr_resource_decl entries (from header)
     * @param count     number of declarations
     * @param phase_idx informational, recorded у OwnerInfo for diagnostics
     */
    EngineError acquire_scenario(SequenceHandle handle,
                                 const modr_resource_decl* resources,
                                 uint8_t count,
                                 uint8_t phase_idx = 0);

    /**
     * Release all scenario-scope resources owned by `handle`. No-op для
     * resources не owned by handle. Safe to call repeatedly.
     */
    void release_scenario(SequenceHandle handle);

    /**
     * Try to acquire all listed phase-scope claims atomically для (handle, track).
     * Returns true on success — caller advances phase state. Returns false on
     * contention — caller stays у WAITING_FOR_RESOURCE phase substate і retries
     * next tick (engine policy).
     *
     * Atomicity matches scenario-scope: partial acquires released на conflict.
     *
     * @param handle    requesting scenario
     * @param track     requesting track within scenario
     * @param phase_idx phase that owns this claim batch (для release matching)
     * @param claims    array of phase resource declarations
     * @param count     number of claims
     */
    bool try_acquire_phase(SequenceHandle handle, TrackIdx track, uint8_t phase_idx,
                           const modr_phase_resource_claim* claims, uint8_t count);

    /**
     * Release all phase-scope resources owned by (handle, track). Called by
     * engine на phase exit. Scenario-scope ownerships untouched.
     */
    void release_phase(SequenceHandle handle, TrackIdx track);

    /// Returns true якщо resource_hash currently has an owner.
    bool is_owned(uint16_t resource_hash) const;

    /// Returns owner record для resource, або nullptr if not owned.
    const OwnerInfo* owner_of(uint16_t resource_hash) const;

    /// Number of currently-tracked ownerships.
    size_t count() const { return owners_.size(); }

    /// Reset all state. Test-only — production engine never calls.
    void clear_for_tests() { owners_.clear(); }

private:
    using Map = etl::flat_map<uint16_t, OwnerInfo, MAX_RESOURCES>;
    Map owners_;

    /// Returns true якщо new request з given exclusivity може be granted given
    /// existing owner (or no owner). Used by both scenario і phase paths.
    /// `requestor_handle/track` distinguish self-claim (already owned by same
    /// requester — re-grant OK) from cross-instance contention.
    bool can_grant(uint16_t resource_hash, bool exclusive,
                   SequenceHandle requestor_handle, TrackIdx requestor_track) const;
};

}  // namespace modesp::sequence
