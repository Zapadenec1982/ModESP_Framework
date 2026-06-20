/**
 * @file audio_sink.h
 * @brief Semantic seam of the audio subsystem (audio analogue of IDisplayPort).
 *
 * The ABSTRACT player module (PlayerModule) decides WHAT and WHEN to play
 * (clip library, decode, queue, priority) and pushes ready 16-bit mono PCM into
 * an IAudioSink. The sink (Max98357aSink, NullSink, future codecs) only knows
 * HOW to get those samples to the speaker — it owns the I²S channel + DMA and a
 * hardware mute, nothing else. The module never touches I²S; the sink never
 * touches files/decoders/state.
 *
 * Single owner of the audio output: exactly one sink, driven by exactly one
 * playback task inside the player. All sink methods are called from that single
 * task in sequence (init from main task before the task starts) — no cross-task
 * channel races.
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace modesp::audio {

/// Backend capabilities — semantic, queried once by the module.
struct AudioCaps {
    uint32_t max_sample_rate = 48000;
    bool     hw_volume       = false;  ///< false ⇒ player applies software volume
};

/// Low-level PCM transport to an I²S amplifier (16-bit signed mono).
class IAudioSink {
public:
    virtual ~IAudioSink() = default;

    /// One-time hardware init (channel/GPIO). false ⇒ audio disabled, system lives.
    virtual bool init() { return true; }

    virtual AudioCaps caps() const { return {}; }

    /// Prepare the channel for a stream at `sample_rate_hz` and unmute the amp.
    /// Called before a run of write()s. Returns false on hardware error.
    virtual bool start(uint32_t sample_rate_hz) = 0;

    /// Push `frames` mono samples; blocks (paced by the DMA) up to `timeout_ms`.
    /// Returns the number of frames actually written.
    virtual size_t write(const int16_t* pcm, size_t frames, uint32_t timeout_ms) = 0;

    /// Stop the stream and mute the amp (idle silence).
    virtual void stop() = 0;

    virtual bool is_healthy() const = 0;
    virtual const char* name() const = 0;
};

} // namespace modesp::audio
