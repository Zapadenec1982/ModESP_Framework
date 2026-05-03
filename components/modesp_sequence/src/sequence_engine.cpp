/**
 * @file sequence_engine.cpp
 * @brief SequenceEngine implementation (Step 14 + mirror keys).
 */

#include "modesp/sequence/sequence_engine.h"
#include "modesp/sequence/sequence_instance.h"
#include "modesp/sequence/modr_loader.h"
#include "modesp/shared_state.h"

#ifndef HOST_BUILD
#include "esp_log.h"
#endif

#include <cstdio>
#include <cstring>

namespace modesp::sequence {

// ── State stringifiers ────────────────────────────────────────────────
//
// Engine writes scenario state і per-track state to SharedState as strings
// для WebUI consumption. WebUI's visible_when can match exact strings (e.g.
// {"abs_test.scenario_state": ["running", "paused"]}). Recipe authors can
// also use state_key_eq for cross-track sync via mirror keys.

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

bool SequenceEngine::on_init() {
    arbiter_.clear_for_tests();
    for (auto& s : slots_) {
        s.runtime = SequenceRuntime{};
        s.buffer_size = 0;
    }
    last_error_ = EngineError::OK;
    return true;
}

void SequenceEngine::on_stop() {
    for (uint8_t i = 0; i < MAX_SEQUENCES; ++i) {
        SequenceHandle h = static_cast<SequenceHandle>(i + 1);
        if (slots_[i].buffer_size != 0) {
            unload(h);
        }
    }
}

void SequenceEngine::on_update(uint32_t dt_ms) {
    for (auto& s : slots_) {
        if (s.buffer_size == 0) continue;
        // Tick only if scenario is actively progressing.
        if (s.runtime.state == SequenceRuntime::State::RUNNING
         || s.runtime.state == SequenceRuntime::State::ABORTING) {
            instance_tick(s.runtime, dt_ms, state_, &arbiter_);
        }
        // Publish mirror keys для ALL loaded slots — covers terminal states
        // (COMPLETED/FAILED/PAUSED) so WebUI shows correct final state.
        publish_mirror_keys(s);
    }
}

void SequenceEngine::publish_mirror_keys(Slot& s) {
    if (state_ == nullptr) return;
    auto* hdr = s.runtime.scenario.header();

    // Resolve recipe name from string pool
    char recipe_name[16] = {0};
    if (!s.runtime.scenario.read_string(hdr->name_str_idx, recipe_name, sizeof(recipe_name))) {
        // Name fits у pool but exceeds our 15-char buffer (recipe author used
        // longer name than mirror-key budget allows). Warn once per slot —
        // мирні key publishing then skipped silently (mirrors won't appear у
        // SharedState/WebUI). Recipe authors hit це when they exceed the
        // documented ≤12-char budget.
        if (!s.name_warn_logged) {
#ifdef HOST_BUILD
            std::fprintf(stderr, "[scenario] recipe name too long (>15 chars)"
                                  " — mirror keys disabled for slot %u\n",
                         static_cast<unsigned>(s.runtime.handle));
#else
            ESP_LOGW("scenario", "recipe name too long (>15 chars) — "
                                  "mirror keys disabled for slot %u",
                     static_cast<unsigned>(s.runtime.handle));
#endif
            s.name_warn_logged = true;
        }
        return;
    }

    char keybuf[32];

    // Scenario-level mirror keys
    int n = std::snprintf(keybuf, sizeof(keybuf), "%s.scenario_state", recipe_name);
    if (n > 0 && static_cast<size_t>(n) < sizeof(keybuf)) {
        state_->set(keybuf, scenario_state_str(s.runtime.state));
    }

    n = std::snprintf(keybuf, sizeof(keybuf), "%s.scenario_elapsed_s", recipe_name);
    if (n > 0 && static_cast<size_t>(n) < sizeof(keybuf)) {
        state_->set(keybuf, static_cast<int32_t>(s.runtime.scenario_elapsed_ms / 1000));
    }

    // Per-track mirror keys
    auto* tracks = s.runtime.scenario.tracks();
    for (uint8_t t = 0; t < hdr->track_count && t < 6; ++t) {
        char track_name[12] = {0};
        if (!s.runtime.scenario.read_string(tracks[t].name_str_idx,
                                             track_name, sizeof(track_name))) {
            continue;
        }
        const TrackRuntime& tr = s.runtime.tracks[t];

        // <recipe>.<track>_state
        n = std::snprintf(keybuf, sizeof(keybuf), "%s.%s_state", recipe_name, track_name);
        if (n > 0 && static_cast<size_t>(n) < sizeof(keybuf)) {
            state_->set(keybuf, track_state_str(tr.state));
        }

        // <recipe>.<track>_phase_idx
        n = std::snprintf(keybuf, sizeof(keybuf), "%s.%s_phase_idx", recipe_name, track_name);
        if (n > 0 && static_cast<size_t>(n) < sizeof(keybuf)) {
            state_->set(keybuf, static_cast<int32_t>(tr.phase_idx));
        }

        // <recipe>.<track>_elapsed_s
        n = std::snprintf(keybuf, sizeof(keybuf), "%s.%s_elapsed_s", recipe_name, track_name);
        if (n > 0 && static_cast<size_t>(n) < sizeof(keybuf)) {
            state_->set(keybuf, static_cast<int32_t>(tr.phase_elapsed_ms / 1000));
        }

        // <recipe>.<track>_phase_name
        if (tr.phase_idx < tracks[t].phase_count) {
            auto* phases = s.runtime.scenario.phases(t);
            char phase_name[24] = {0};
            if (s.runtime.scenario.read_string(phases[tr.phase_idx].name_str_idx,
                                                phase_name, sizeof(phase_name))) {
                n = std::snprintf(keybuf, sizeof(keybuf), "%s.%s_phase_name",
                                  recipe_name, track_name);
                if (n > 0 && static_cast<size_t>(n) < sizeof(keybuf)) {
                    state_->set(keybuf, phase_name);
                }
            }
        }
    }
}

int SequenceEngine::find_free_slot() const {
    for (uint8_t i = 0; i < MAX_SEQUENCES; ++i) {
        if (slots_[i].buffer_size == 0) return static_cast<int>(i);
    }
    return -1;
}

SequenceEngine::Slot* SequenceEngine::slot_for(SequenceHandle h) {
    if (h == 0 || h > MAX_SEQUENCES) return nullptr;
    Slot& s = slots_[h - 1];
    if (s.buffer_size == 0) return nullptr;
    return &s;
}

const SequenceEngine::Slot* SequenceEngine::slot_for(SequenceHandle h) const {
    if (h == 0 || h > MAX_SEQUENCES) return nullptr;
    const Slot& s = slots_[h - 1];
    if (s.buffer_size == 0) return nullptr;
    return &s;
}

SequenceHandle SequenceEngine::load_buffer(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0 || size > MODR_MAX_SIZE) {
        last_error_ = EngineError::INVALID_FILE;
        return 0;
    }
    int idx = find_free_slot();
    if (idx < 0) {
        last_error_ = EngineError::NO_SLOT;
        return 0;
    }
    Slot& s = slots_[idx];
    std::memcpy(s.buffer, data, size);

    SequenceRuntime fresh{};
    fresh.handle = static_cast<SequenceHandle>(idx + 1);
    EngineError err = modr_validate(s.buffer, size, fresh.scenario);
    if (err != EngineError::OK) {
        last_error_ = err;
        return 0;
    }
    fresh.state = SequenceRuntime::State::LOADED;
    s.runtime = fresh;
    s.buffer_size = size;
    last_error_ = EngineError::OK;
    return fresh.handle;
}

SequenceHandle SequenceEngine::load_path(const char* path) {
    if (path == nullptr) {
        last_error_ = EngineError::INVALID_FILE;
        return 0;
    }
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        last_error_ = EngineError::INVALID_FILE;
        return 0;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0 || static_cast<size_t>(size) > MODR_MAX_SIZE) {
        std::fclose(f);
        last_error_ = EngineError::INVALID_FILE;
        return 0;
    }
    int idx = find_free_slot();
    if (idx < 0) {
        std::fclose(f);
        last_error_ = EngineError::NO_SLOT;
        return 0;
    }
    Slot& s = slots_[idx];
    size_t read = std::fread(s.buffer, 1, static_cast<size_t>(size), f);
    std::fclose(f);
    if (read != static_cast<size_t>(size)) {
        last_error_ = EngineError::INVALID_FILE;
        return 0;
    }
    SequenceRuntime fresh{};
    fresh.handle = static_cast<SequenceHandle>(idx + 1);
    EngineError err = modr_validate(s.buffer, read, fresh.scenario);
    if (err != EngineError::OK) {
        last_error_ = err;
        return 0;
    }
    fresh.state = SequenceRuntime::State::LOADED;
    s.runtime = fresh;
    s.buffer_size = read;
    last_error_ = EngineError::OK;
    return fresh.handle;
}

EngineError SequenceEngine::unload(SequenceHandle h) {
    Slot* s = slot_for(h);
    if (!s) return EngineError::INVALID_HANDLE;

    // If running, release any scenario-scope і phase-scope resources.
    arbiter_.release_scenario(h);
    for (uint8_t t = 0; t < s->runtime.scenario.header()->track_count; ++t) {
        arbiter_.release_phase(h, t);
    }
    s->runtime = SequenceRuntime{};
    s->buffer_size = 0;
    return EngineError::OK;
}

EngineError SequenceEngine::start(SequenceHandle h) {
    Slot* s = slot_for(h);
    if (!s) return EngineError::INVALID_HANDLE;
    if (s->runtime.state != SequenceRuntime::State::LOADED) {
        return EngineError::NOT_LOADED;
    }
    // Acquire scenario-scope resources atomically
    auto* hdr = s->runtime.scenario.header();
    if (hdr->resource_count > 0) {
        EngineError e = arbiter_.acquire_scenario(h, s->runtime.scenario.resources(),
                                                  hdr->resource_count, 0);
        if (e != EngineError::OK) {
            last_error_ = e;
            return e;
        }
    }
    instance_start(s->runtime);
    return EngineError::OK;
}

EngineError SequenceEngine::pause(SequenceHandle h) {
    Slot* s = slot_for(h);
    if (!s) return EngineError::INVALID_HANDLE;
    if (s->runtime.state != SequenceRuntime::State::RUNNING) {
        return EngineError::NOT_LOADED;
    }
    s->runtime.state = SequenceRuntime::State::PAUSED;
    return EngineError::OK;
}

EngineError SequenceEngine::resume(SequenceHandle h) {
    Slot* s = slot_for(h);
    if (!s) return EngineError::INVALID_HANDLE;
    if (s->runtime.state != SequenceRuntime::State::PAUSED) {
        return EngineError::NOT_LOADED;
    }
    s->runtime.state = SequenceRuntime::State::RUNNING;
    return EngineError::OK;
}

EngineError SequenceEngine::abort(SequenceHandle h, uint8_t /*reason_code*/) {
    Slot* s = slot_for(h);
    if (!s) return EngineError::INVALID_HANDLE;
    if (s->runtime.state != SequenceRuntime::State::RUNNING
     && s->runtime.state != SequenceRuntime::State::PAUSED) {
        return EngineError::NOT_LOADED;
    }
    instance_abort(s->runtime, &arbiter_);
    return EngineError::OK;
}

// ── Diagnostic accessors ──

SequenceRuntime::State SequenceEngine::state(SequenceHandle h) const {
    const Slot* s = slot_for(h);
    return s ? s->runtime.state : SequenceRuntime::State::IDLE;
}

uint32_t SequenceEngine::scenario_elapsed_ms(SequenceHandle h) const {
    const Slot* s = slot_for(h);
    return s ? s->runtime.scenario_elapsed_ms : 0u;
}

uint8_t SequenceEngine::track_count(SequenceHandle h) const {
    const Slot* s = slot_for(h);
    return s ? s->runtime.scenario.header()->track_count : 0u;
}

TrackRuntime::State SequenceEngine::track_state(SequenceHandle h, TrackIdx t) const {
    const Slot* s = slot_for(h);
    if (!s || t >= s->runtime.scenario.header()->track_count) {
        return TrackRuntime::State::IDLE;
    }
    return s->runtime.tracks[t].state;
}

uint8_t SequenceEngine::track_phase_idx(SequenceHandle h, TrackIdx t) const {
    const Slot* s = slot_for(h);
    if (!s || t >= s->runtime.scenario.header()->track_count) return 0;
    return s->runtime.tracks[t].phase_idx;
}

uint32_t SequenceEngine::track_phase_elapsed_ms(SequenceHandle h, TrackIdx t) const {
    const Slot* s = slot_for(h);
    if (!s || t >= s->runtime.scenario.header()->track_count) return 0;
    return s->runtime.tracks[t].phase_elapsed_ms;
}

uint8_t SequenceEngine::active_count() const {
    uint8_t n = 0;
    for (const auto& s : slots_) {
        if (s.buffer_size == 0) continue;
        if (s.runtime.state == SequenceRuntime::State::RUNNING
         || s.runtime.state == SequenceRuntime::State::PAUSED) {
            ++n;
        }
    }
    return n;
}

}  // namespace modesp::sequence
