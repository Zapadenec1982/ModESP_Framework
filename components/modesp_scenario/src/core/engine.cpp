/**
 * @file engine.cpp
 * @brief Engine impl: slot pool + lifecycle + tick driver + observer dispatch.
 *
 * **Phase 1 lift:** Mirror keys are direct-call (free function у tick).
 * NVS persistence removed (Phase 2 wires NvsObserver).
 *
 * Target: ~320 LOC после lift (down from 485 у old modesp_sequence's
 * sequence_engine.cpp). publish_mirror_keys + persist_scan + persist_slot
 * + try_recover all moved out.
 */

#include "modesp/scenario/engine.h"
#include "modesp/scenario/action_registry.h"
#include "modesp/scenario/continuous_behavior.h"  // ContinuousRegistry
#include "modesp/scenario/i_state_backend.h"
#include "modesp/scenario/modr_loader.h"

#include "instance.h"
#include "track.h"
#include "mirror.h"

#include <cstdio>
#include <cstring>

#ifndef HOST_BUILD
#include "esp_log.h"
#endif

namespace modesp::scenario {

bool Engine::on_init() {
    arbiter_ = ResourceArbiter{};
    for (auto& s : slots_) {
        s.runtime = SequenceRuntime{};
        s.buffer_size = 0;
        s.name_warn_logged = false;
    }
    for (size_t i = 0; i < MAX_SEQUENCES; ++i) {
        last_emitted_state_[i] = SequenceRuntime::State::IDLE;
        for (auto& p : last_emitted_phase_[i]) p = 0xFF;
    }
    last_error_ = EngineError::OK;
    return true;
}

void Engine::on_stop() {
    for (uint8_t i = 0; i < MAX_SEQUENCES; ++i) {
        SequenceHandle h = static_cast<SequenceHandle>(i + 1);
        if (slots_[i].buffer_size != 0) {
            unload(h);
        }
    }
}

void Engine::on_update(uint32_t dt_ms) {
    RegistryRefs regs{actions_, continuous_};

    for (size_t idx = 0; idx < MAX_SEQUENCES; ++idx) {
        Slot& s = slots_[idx];
        if (s.buffer_size == 0) continue;

        const SequenceRuntime::State prev_state = s.runtime.state;

        // Tick only if scenario is actively progressing.
        if (s.runtime.state == SequenceRuntime::State::RUNNING
         || s.runtime.state == SequenceRuntime::State::ABORTING) {
            instance_tick(s.runtime, dt_ms, state_, regs, &arbiter_);
        }

        // Direct mirror call — every loaded slot, every tick. Covers terminal
        // states (COMPLETED/FAILED/PAUSED) so WebUI shows correct final state.
        mirror::publish(state_, s.runtime, s.name_warn_logged);

        // Observer dispatch: phase entry edges
        const uint8_t track_count = s.runtime.scenario.header() != nullptr
            ? s.runtime.scenario.header()->track_count : 0;
        for (uint8_t t = 0; t < track_count && t < 6; ++t) {
            uint8_t cur = s.runtime.tracks[t].phase_idx;
            if (cur != last_emitted_phase_[idx][t]) {
                last_emitted_phase_[idx][t] = cur;
                emit_phase_entered(s.runtime.handle, t, cur);
            }
        }

        // Observer dispatch: scenario state edges
        const SequenceRuntime::State cur_state = s.runtime.state;
        if (cur_state != prev_state) {
            using SS = SequenceRuntime::State;
            // Started: any state → RUNNING (only when previous was LOADED/PAUSED).
            // Engine emits this from start()/resume(). Tick-detected transitions
            // are FSM-internal (e.g. ABORTING → FAILED, not "started").
            if ((cur_state == SS::COMPLETED || cur_state == SS::FAILED)
             && (prev_state != SS::COMPLETED && prev_state != SS::FAILED)) {
                emit_scenario_terminal(s.runtime.handle, cur_state);
            }
            last_emitted_state_[idx] = cur_state;
        }
    }
}

// ── Observer dispatch helpers ──

void Engine::emit_scenario_started(SequenceHandle h) {
    for (auto* obs : observers_) {
        if (obs) obs->on_scenario_started(h);
    }
}

void Engine::emit_phase_entered(SequenceHandle h, TrackIdx t, uint8_t phase_idx) {
    for (auto* obs : observers_) {
        if (obs) obs->on_phase_entered(h, t, phase_idx);
    }
}

void Engine::emit_scenario_terminal(SequenceHandle h, SequenceRuntime::State s) {
    for (auto* obs : observers_) {
        if (obs) obs->on_scenario_terminal(h, s);
    }
}

// ── Slot management ──

int Engine::find_free_slot() const {
    for (uint8_t i = 0; i < MAX_SEQUENCES; ++i) {
        if (slots_[i].buffer_size == 0) return static_cast<int>(i);
    }
    return -1;
}

Engine::Slot* Engine::slot_for(SequenceHandle h) {
    if (h == 0 || h > MAX_SEQUENCES) return nullptr;
    Slot& s = slots_[h - 1];
    if (s.buffer_size == 0) return nullptr;
    return &s;
}

const Engine::Slot* Engine::slot_for(SequenceHandle h) const {
    if (h == 0 || h > MAX_SEQUENCES) return nullptr;
    const Slot& s = slots_[h - 1];
    if (s.buffer_size == 0) return nullptr;
    return &s;
}

SequenceRuntime* Engine::runtime_for(SequenceHandle h) {
    Slot* s = slot_for(h);
    return s ? &s->runtime : nullptr;
}

const SequenceRuntime* Engine::runtime_for(SequenceHandle h) const {
    const Slot* s = slot_for(h);
    return s ? &s->runtime : nullptr;
}

// ── Lifecycle ──

SequenceHandle Engine::load_buffer(const uint8_t* data, size_t size) {
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
    EngineError err = modr_validate(s.buffer, size, *actions_, fresh.scenario);
    if (err != EngineError::OK) {
        last_error_ = err;
        return 0;
    }
    fresh.state = SequenceRuntime::State::LOADED;
    s.runtime = fresh;
    s.buffer_size = size;
    s.name_warn_logged = false;
    last_emitted_state_[idx] = SequenceRuntime::State::LOADED;
    for (auto& p : last_emitted_phase_[idx]) p = 0xFF;
    last_error_ = EngineError::OK;
    return fresh.handle;
}

SequenceHandle Engine::load_path(const char* path) {
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
    EngineError err = modr_validate(s.buffer, read, *actions_, fresh.scenario);
    if (err != EngineError::OK) {
        last_error_ = err;
        return 0;
    }
    fresh.state = SequenceRuntime::State::LOADED;
    s.runtime = fresh;
    s.buffer_size = read;
    s.name_warn_logged = false;
    last_emitted_state_[idx] = SequenceRuntime::State::LOADED;
    for (auto& p : last_emitted_phase_[idx]) p = 0xFF;
    last_error_ = EngineError::OK;
    return fresh.handle;
}

EngineError Engine::unload(SequenceHandle h) {
    Slot* s = slot_for(h);
    if (!s) return EngineError::INVALID_HANDLE;

    arbiter_.release_scenario(h);
    if (s->runtime.scenario.header() != nullptr) {
        for (uint8_t t = 0; t < s->runtime.scenario.header()->track_count; ++t) {
            arbiter_.release_phase(h, t);
        }
    }
    s->runtime = SequenceRuntime{};
    s->buffer_size = 0;
    s->name_warn_logged = false;
    return EngineError::OK;
}

EngineError Engine::start(SequenceHandle h) {
    Slot* s = slot_for(h);
    if (!s) return EngineError::INVALID_HANDLE;
    if (s->runtime.state != SequenceRuntime::State::LOADED) {
        return EngineError::NOT_LOADED;
    }
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
    emit_scenario_started(h);
    return EngineError::OK;
}

EngineError Engine::pause(SequenceHandle h) {
    Slot* s = slot_for(h);
    if (!s) return EngineError::INVALID_HANDLE;
    if (s->runtime.state != SequenceRuntime::State::RUNNING) {
        return EngineError::NOT_LOADED;
    }
    s->runtime.state = SequenceRuntime::State::PAUSED;
    return EngineError::OK;
}

EngineError Engine::resume(SequenceHandle h) {
    Slot* s = slot_for(h);
    if (!s) return EngineError::INVALID_HANDLE;
    if (s->runtime.state != SequenceRuntime::State::PAUSED) {
        return EngineError::NOT_LOADED;
    }
    s->runtime.state = SequenceRuntime::State::RUNNING;
    return EngineError::OK;
}

EngineError Engine::abort(SequenceHandle h, uint8_t /*reason*/) {
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

SequenceRuntime::State Engine::state(SequenceHandle h) const {
    const Slot* s = slot_for(h);
    return s ? s->runtime.state : SequenceRuntime::State::IDLE;
}

uint32_t Engine::scenario_elapsed_ms(SequenceHandle h) const {
    const Slot* s = slot_for(h);
    return s ? s->runtime.scenario_elapsed_ms : 0u;
}

uint8_t Engine::track_count(SequenceHandle h) const {
    const Slot* s = slot_for(h);
    return s ? s->runtime.scenario.header()->track_count : 0u;
}

TrackRuntime::State Engine::track_state(SequenceHandle h, TrackIdx t) const {
    const Slot* s = slot_for(h);
    if (!s || t >= s->runtime.scenario.header()->track_count) {
        return TrackRuntime::State::IDLE;
    }
    return s->runtime.tracks[t].state;
}

uint8_t Engine::track_phase_idx(SequenceHandle h, TrackIdx t) const {
    const Slot* s = slot_for(h);
    if (!s || t >= s->runtime.scenario.header()->track_count) return 0;
    return s->runtime.tracks[t].phase_idx;
}

uint32_t Engine::track_phase_elapsed_ms(SequenceHandle h, TrackIdx t) const {
    const Slot* s = slot_for(h);
    if (!s || t >= s->runtime.scenario.header()->track_count) return 0;
    return s->runtime.tracks[t].phase_elapsed_ms;
}

uint8_t Engine::active_count() const {
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

}  // namespace modesp::scenario
