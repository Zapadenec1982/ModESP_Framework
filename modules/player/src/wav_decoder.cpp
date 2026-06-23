/**
 * @file wav_decoder.cpp
 * @brief Minimal WAV reader — 16-bit PCM mono only.
 */

#include "player/wav_decoder.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "WavDec";

namespace modesp::audio {

bool WavDecoder::open(const char* path, uint32_t& sample_rate_hz) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "open failed: %s", path);
        return false;
    }

    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 ||
        std::memcmp(hdr, "RIFF", 4) != 0 || std::memcmp(hdr + 8, "WAVE", 4) != 0) {
        ESP_LOGW(TAG, "%s: not a RIFF/WAVE file", path);
        fclose(f);
        return false;
    }

    uint16_t fmt = 0, channels = 0, bits = 0;
    uint32_t rate = 0, data_size = 0;
    bool have_fmt = false, have_data = false;

    uint8_t ck[8];
    while (fread(ck, 1, 8, f) == 8) {
        uint32_t sz = ck[4] | (ck[5] << 8) | (ck[6] << 16) | ((uint32_t)ck[7] << 24);
        if (std::memcmp(ck, "fmt ", 4) == 0) {
            uint8_t fb[16];
            uint32_t rd = (sz < 16) ? sz : 16;
            if (fread(fb, 1, rd, f) != rd) break;
            fmt      = fb[0]  | (fb[1] << 8);
            channels = fb[2]  | (fb[3] << 8);
            rate     = fb[4]  | (fb[5] << 8) | (fb[6] << 16) | ((uint32_t)fb[7] << 24);
            bits     = fb[14] | (fb[15] << 8);
            have_fmt = true;
            if (sz > rd) fseek(f, sz - rd, SEEK_CUR);
        } else if (std::memcmp(ck, "data", 4) == 0) {
            data_size = sz;
            have_data = true;
            break;   // PCM stream starts here
        } else {
            fseek(f, sz, SEEK_CUR);   // skip unknown chunk
        }
    }

    if (!have_fmt || !have_data || fmt != 1 || bits != 16 || channels != 1) {
        ESP_LOGW(TAG, "%s: unsupported (need PCM 16-bit mono; got fmt=%u ch=%u bits=%u)",
                 path, fmt, channels, bits);
        fclose(f);
        return false;
    }

    f_              = f;
    remaining_      = data_size;
    sample_rate_hz  = rate ? rate : 16000;
    ESP_LOGI(TAG, "%s: %lu Hz, %lu bytes", path,
             (unsigned long)sample_rate_hz, (unsigned long)data_size);
    return true;
}

size_t WavDecoder::read(int16_t* buf, size_t max_frames) {
    if (!f_ || remaining_ == 0) return 0;

    size_t want = max_frames * sizeof(int16_t);
    if (want > remaining_) want = remaining_;

    size_t got = fread(buf, 1, want, f_);
    remaining_ -= got;
    return got / sizeof(int16_t);
}

void WavDecoder::close() {
    if (f_) { fclose(f_); f_ = nullptr; }
    remaining_ = 0;
}

} // namespace modesp::audio
