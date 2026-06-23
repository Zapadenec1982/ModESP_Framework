/**
 * @file mp3_decoder.h
 * @brief MP3 clip decoder via libhelix (esp-libhelix-mp3) — IAudioDecoder.
 *
 * Streaming, fixed-point MP3 decode (MPEG-1/2/2.5 Layer III). Stereo frames are
 * down-mixed to mono. The implementation is compiled only with
 * CONFIG_MODESP_AUDIO_MP3 (default y); when disabled, open() returns false and
 * .mp3 clips are reported unsupported. The header carries no libhelix types
 * (handle stored as void*), so it is safe to include unconditionally.
 */

#pragma once

#include "player/audio_decoder.h"
#include <cstdio>
#include <cstdint>

namespace modesp::audio {

class Mp3Decoder : public IAudioDecoder {
public:
    bool open(const char* path, uint32_t& sample_rate_hz) override;
    size_t read(int16_t* buf, size_t max_frames) override;
    void close() override;

private:
    static constexpr int IN_SZ   = 4096;   // input ring (> one MP3 frame ~1940 B)
    static constexpr int PCM_MAX  = 2304;  // libhelix max: 2 ch * 2 gran * 576
    static constexpr int MONO_MAX = 1152;  // max samples/channel per frame

    bool decode_next_frame();   // fills mono_/mono_n_; false ⇒ no more frames
    void refill_input();

    FILE*    f_     = nullptr;
    void*    hmp3_  = nullptr;   // HMP3Decoder

    uint8_t  in_[IN_SZ];
    int      in_len_ = 0;        // valid bytes in in_
    int      in_pos_ = 0;        // read cursor into in_
    bool     eof_    = false;    // input file drained

    int16_t  pcm_[PCM_MAX];      // raw interleaved decode output
    int16_t  mono_[MONO_MAX];    // down-mixed mono
    int      mono_n_   = 0;      // samples available in mono_
    int      mono_pos_ = 0;
    uint32_t rate_     = 0;
};

} // namespace modesp::audio
