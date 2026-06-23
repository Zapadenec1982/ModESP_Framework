/**
 * @file wav_decoder.h
 * @brief WAV clip decoder (16-bit PCM mono) — IAudioDecoder.
 */

#pragma once

#include "player/audio_decoder.h"
#include <cstdio>

namespace modesp::audio {

class WavDecoder : public IAudioDecoder {
public:
    bool open(const char* path, uint32_t& sample_rate_hz) override;
    size_t read(int16_t* buf, size_t max_frames) override;
    void close() override;

private:
    FILE*    f_         = nullptr;
    uint32_t remaining_ = 0;   // bytes left in the data chunk
};

} // namespace modesp::audio
