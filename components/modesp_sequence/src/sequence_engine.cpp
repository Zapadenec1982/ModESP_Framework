/**
 * @file sequence_engine.cpp
 * @brief SequenceEngine implementation (Step 14).
 */

#include "modesp/sequence/sequence_engine.h"
#include "modesp/sequence/sequence_instance.h"
#include "modesp/sequence/modr_loader.h"

#include <cstdio>
#include <cstring>

namespace modesp::sequence {

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
        if (s.runtime.state != SequenceRuntime::State::RUNNING
         && s.runtime.state != SequenceRuntime::State::ABORTING) continue;
        instance_tick(s.runtime, dt_ms, state_, &arbiter_);
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
    instance_abort(s->runtime);
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
