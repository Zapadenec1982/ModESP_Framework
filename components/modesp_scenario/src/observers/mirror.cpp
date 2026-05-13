/**
 * @file mirror.cpp
 * @brief Direct-call mirror publisher impl.
 *
 * Lifted from old `SequenceEngine::publish_mirror_keys` member. State stringifiers
 * are file-static (anonymous namespace) — мapping FSM enums до strings used by
 * WebUI's `visible_when` matching.
 */

#include "mirror.h"
#include "modesp/scenario/i_state_backend.h"

#ifndef HOST_BUILD
#include "esp_log.h"
#else
#include <cstdio>
#endif

#include <cstdio>
#include <cstring>

namespace modesp::scenario::mirror {

namespace {

const char* scenario_state_str(SequenceRuntime::State s) {
    using SS = SequenceRuntime::State;
    switch (s) {
        case SS::IDLE:      return "idle";
        case SS::LOADED:    return "loaded";
        case SS::RUNNING:   return "running";
        case SS::PAUSED:    return "paused";
        case SS::ABORTING:  return "aborting";
        case SS::COMPLETED: return "completed";
        case SS::FAILED:    return "failed";
    }
    return "unknown";
}

const char* track_state_str(TrackRuntime::State s) {
    using TS = TrackRuntime::State;
    switch (s) {
        case TS::IDLE:                 return "idle";
        case TS::RUNNING:              return "running";
        case TS::WAITING_FOR_RESOURCE: return "waiting";
        case TS::ABORTING:             return "aborting";
        case TS::COMPLETED:            return "completed";
        case TS::FAILED:               return "failed";
    }
    return "unknown";
}

}  // anonymous namespace

void publish(IStateBackend* backend, const SequenceRuntime& sr,
             bool& name_warn_logged) {
    if (backend == nullptr) return;
    auto* hdr = sr.scenario.header();

    // Resolve recipe name from string pool
    char recipe_name[16] = {0};
    if (!sr.scenario.read_string(hdr->name_str_idx, recipe_name, sizeof(recipe_name))) {
        if (!name_warn_logged) {
#ifdef HOST_BUILD
            std::fprintf(stderr, "[scenario] recipe name too long (>15 chars)"
                                  " — mirror keys disabled for slot %u\n",
                         static_cast<unsigned>(sr.handle));
#else
            ESP_LOGW("scenario", "recipe name too long (>15 chars) — "
                                  "mirror keys disabled for slot %u",
                     static_cast<unsigned>(sr.handle));
#endif
            name_warn_logged = true;
        }
        return;
    }

    char keybuf[32];

    // Scenario-level mirror keys
    int n = std::snprintf(keybuf, sizeof(keybuf), "%s.scenario_state", recipe_name);
    if (n > 0 && static_cast<size_t>(n) < sizeof(keybuf)) {
        backend->set(keybuf, scenario_state_str(sr.state));
    }

    n = std::snprintf(keybuf, sizeof(keybuf), "%s.scenario_elapsed_s", recipe_name);
    if (n > 0 && static_cast<size_t>(n) < sizeof(keybuf)) {
        backend->set(keybuf, static_cast<int32_t>(sr.scenario_elapsed_ms / 1000));
    }

    // Per-track mirror keys
    auto* tracks = sr.scenario.tracks();
    for (uint8_t t = 0; t < hdr->track_count && t < 6; ++t) {
        char track_name[12] = {0};
        if (!sr.scenario.read_string(tracks[t].name_str_idx,
                                     track_name, sizeof(track_name))) {
            continue;
        }
        const TrackRuntime& tr = sr.tracks[t];

        // <recipe>.<track>_state
        n = std::snprintf(keybuf, sizeof(keybuf), "%s.%s_state", recipe_name, track_name);
        if (n > 0 && static_cast<size_t>(n) < sizeof(keybuf)) {
            backend->set(keybuf, track_state_str(tr.state));
        }

        // <recipe>.<track>_phase_idx
        n = std::snprintf(keybuf, sizeof(keybuf), "%s.%s_phase_idx", recipe_name, track_name);
        if (n > 0 && static_cast<size_t>(n) < sizeof(keybuf)) {
            backend->set(keybuf, static_cast<int32_t>(tr.phase_idx));
        }

        // <recipe>.<track>_elapsed_s
        n = std::snprintf(keybuf, sizeof(keybuf), "%s.%s_elapsed_s", recipe_name, track_name);
        if (n > 0 && static_cast<size_t>(n) < sizeof(keybuf)) {
            backend->set(keybuf, static_cast<int32_t>(tr.phase_elapsed_ms / 1000));
        }

        // <recipe>.<track>_phase_name
        if (tr.phase_idx < tracks[t].phase_count) {
            auto* phases = sr.scenario.phases(t);
            char phase_name[24] = {0};
            if (sr.scenario.read_string(phases[tr.phase_idx].name_str_idx,
                                        phase_name, sizeof(phase_name))) {
                n = std::snprintf(keybuf, sizeof(keybuf), "%s.%s_phase_name",
                                  recipe_name, track_name);
                if (n > 0 && static_cast<size_t>(n) < sizeof(keybuf)) {
                    backend->set(keybuf, phase_name);
                }
            }
        }
    }
}

}  // namespace modesp::scenario::mirror
