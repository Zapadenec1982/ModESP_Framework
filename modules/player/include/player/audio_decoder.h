/**
 * @file audio_decoder.h
 * @brief Pluggable clip-decoder seam — file bytes → 16-bit mono PCM.
 *
 * The player picks an IAudioDecoder by file extension (.wav → WavDecoder,
 * .mp3 → Mp3Decoder) and streams its output to the IAudioSink. Decoders are
 * volume-agnostic (the player applies software volume) and always emit MONO
 * 16-bit samples — stereo sources are down-mixed by the decoder, because the
 * MAX98357A is a mono amplifier.
 *
 * All methods are called from the player's single playback task, in sequence.
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace modesp::audio {

class IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;

    /// Open a clip file; sets sample_rate_hz from the stream. false on error.
    virtual bool open(const char* path, uint32_t& sample_rate_hz) = 0;

    /// Produce up to `max_frames` 16-bit MONO samples into `buf`.
    /// Returns the number of frames produced; 0 means end-of-clip.
    virtual size_t read(int16_t* buf, size_t max_frames) = 0;

    virtual void close() = 0;
};

} // namespace modesp::audio
