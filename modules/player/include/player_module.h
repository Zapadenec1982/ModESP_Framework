/**
 * @file player_module.h
 * @brief Audio player module — clip library + alarm tones over an IAudioSink.
 *
 * Owns "what / when": reads command keys from SharedState (player.play /
 * player.stop / player.volume), plays either a built-in alarm tone or a WAV
 * clip from /data/audio/, and mirrors playback state back to SharedState for
 * the web UI / MQTT. The single playback FreeRTOS task is the sole owner of the
 * IAudioSink; on_update (100 Hz) only posts requests and mirrors atomics.
 *
 * Triggers are SharedState-mediated (equipment / scenarios write player.play),
 * the same decoupling presence uses to read equipment.presence. Alarm requests
 * preempt normal clips; while an alarm plays, normal requests are ignored.
 *
 * The active sink backend is resolved from bindings.json via
 * AudioBackendRegistry (mirror of DisplayModule::bind_display). No binding →
 * NullSink (no sound, system lives).
 */

#pragma once

#include "modesp/base_module.h"
#include "player/audio_sink.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "etl/string.h"
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace modesp { struct BindingTable; class HAL; }

class PlayerModule : public modesp::BaseModule {
public:
    PlayerModule();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

    /// Resolve the active audio sink from bindings.json (role/driver) via
    /// AudioBackendRegistry; pins from board.json via HAL. Called in main.cpp
    /// after hal.init (like display.bind_display). No binding → NullSink.
    void bind_audio(const modesp::BindingTable& bindings, modesp::HAL& hal);

private:
    static constexpr size_t LUT_SIZE     = 256;   // sine table (power of 2)
    static constexpr size_t AUDIO_FRAMES = 240;   // mono samples per buffer
    static constexpr size_t CLIP_NAME    = 32;
    static constexpr uint32_t TONE_RATE  = 16000; // built-in tones are 16 kHz

    enum class Kind : uint8_t { Stop = 0, Tone = 1, Wav = 2 };
    enum Priority   : uint8_t { PRIO_NORMAL = 0, PRIO_ALARM = 1 };

    struct Request {
        Kind     kind       = Kind::Stop;
        uint8_t  priority   = PRIO_NORMAL;
        uint16_t tone_hz    = 2000;
        bool     tone_beep  = false;        // beep pattern vs solid tone
        uint32_t tone_total = 0;            // tone length in frames (0 = until stop)
        char     clip[CLIP_NAME] = {0};     // WAV filename (no path)
    };

    static void task_trampoline(void* arg);
    void playback_task();
    bool post(const Request& r);

    Request make_request(const char* name) const;

    // ── playback task helpers ──
    bool start_active(const Request& r);
    bool stream_one(const Request& r);      // true ⇒ source exhausted
    void end_active();
    size_t fill_tone(int16_t* buf, size_t frames, uint16_t hz, bool gate);
    size_t fill_wav(int16_t* buf, size_t frames);
    bool open_wav(const char* name, uint32_t& rate_out);

    bool read_string(const char* key, char* out, size_t out_sz) const;

    modesp::audio::IAudioSink* sink_ = nullptr;

    QueueHandle_t q_    = nullptr;
    TaskHandle_t  task_ = nullptr;

    std::atomic<bool>     playing_{false};
    std::atomic<uint16_t> volume_q8_{179};   // 0..256 fixed-point (70 %)

    int16_t  lut_[LUT_SIZE]{};

    // playback-task-only streaming state
    uint32_t cur_rate_      = TONE_RATE;
    uint32_t phase_         = 0;             // Q16 sine phase
    uint32_t tone_pos_      = 0;             // frames played in current tone
    FILE*    wav_           = nullptr;
    uint32_t wav_remaining_ = 0;             // bytes left in the data chunk

    bool initialized_ = false;
};
