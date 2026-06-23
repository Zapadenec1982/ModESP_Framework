/**
 * @file mp3_decoder.cpp
 * @brief MP3 streaming decode via libhelix — implementation.
 *
 * Compiled only with CONFIG_MODESP_AUDIO_MP3 (default y). Maintains a small
 * input ring, finds frame sync words, decodes one MPEG Layer III frame at a
 * time, down-mixes stereo to mono, and doles the result out in read()-sized
 * chunks. When disabled, every method is a no-op and .mp3 clips are rejected.
 */

#include "player/mp3_decoder.h"
#include "esp_log.h"

static const char* TAG = "Mp3Dec";

#ifdef CONFIG_MODESP_AUDIO_MP3

#include "mp3dec.h"
#include <cstring>

namespace modesp::audio {

// Largest MP3 frame (MAINBUF_SIZE) — keep a whole frame buffered before decode.
static constexpr int MP3_FRAME_MAX = 1940;

bool Mp3Decoder::open(const char* path, uint32_t& sample_rate_hz) {
    f_ = fopen(path, "rb");
    if (!f_) {
        ESP_LOGW(TAG, "open failed: %s", path);
        return false;
    }

    // Skip a leading ID3v2 tag if present. Real-world MP3s prepend it, and with
    // embedded cover art (APIC) it is routinely 20–300 KB of non-audio bytes that
    // contain many false MPEG sync candidates — fseeking past it lets the first
    // real frame be found immediately. Size is synchsafe (7 bits/byte).
    uint8_t id3[10];
    if (fread(id3, 1, 10, f_) == 10 && std::memcmp(id3, "ID3", 3) == 0) {
        uint32_t tag = ((uint32_t)(id3[6] & 0x7F) << 21) | ((uint32_t)(id3[7] & 0x7F) << 14) |
                       ((uint32_t)(id3[8] & 0x7F) << 7)  |  (uint32_t)(id3[9] & 0x7F);
        long skip = 10 + (long)tag + ((id3[5] & 0x10) ? 10 : 0);  // +10 if footer present
        fseek(f_, skip, SEEK_SET);
    } else {
        fseek(f_, 0, SEEK_SET);   // not ID3 — rewind to start
    }

    hmp3_ = MP3InitDecoder();
    if (!hmp3_) {
        ESP_LOGE(TAG, "MP3InitDecoder failed (out of memory)");
        fclose(f_); f_ = nullptr;
        return false;
    }

    in_len_ = 0; in_pos_ = 0; eof_ = false;
    mono_n_ = 0; mono_pos_ = 0; rate_ = 0;

    refill_input();
    if (!decode_next_frame()) {          // first frame establishes the sample rate
        ESP_LOGW(TAG, "%s: no decodable MP3 frame", path);
        close();
        return false;
    }
    sample_rate_hz = rate_;
    ESP_LOGI(TAG, "%s: %lu Hz", path, (unsigned long)rate_);
    return true;
}

void Mp3Decoder::refill_input() {
    int rem = in_len_ - in_pos_;
    if (rem > 0 && in_pos_ > 0) std::memmove(in_, in_ + in_pos_, rem);
    in_len_ = rem;
    in_pos_ = 0;
    if (eof_) return;
    int want = IN_SZ - in_len_;
    if (want <= 0) return;                       // buffer already full — nothing to pull
    size_t got = fread(in_ + in_len_, 1, static_cast<size_t>(want), f_);
    in_len_ += static_cast<int>(got);
    if (got == 0) eof_ = true;                   // EOF or read error — either way, drained
}

bool Mp3Decoder::decode_next_frame() {
    // Loop until a frame is produced or the stream is truly drained. Termination
    // is guaranteed by forward progress: every iteration either advances in_pos_,
    // grows the input via refill_input(), or sets eof_ (fread returns 0). There is
    // NO fixed iteration cap — so a long ID3/garbage prefix or a mid-stream
    // corrupt run resyncs to the next valid frame instead of giving up early.
    for (;;) {
        if (in_len_ - in_pos_ < MP3_FRAME_MAX && !eof_) refill_input();

        int left = in_len_ - in_pos_;
        if (left <= 0) return false;     // truly drained → end of clip

        unsigned char* p = in_ + in_pos_;
        int off = MP3FindSyncWord(p, left);
        if (off < 0) {
            // No sync candidate in the buffer — keep a couple of trailing bytes
            // (a sync word may straddle the refill boundary) and pull more.
            if (eof_) return false;                          // nothing left to scan
            in_pos_ = (in_len_ > 2) ? in_len_ - 2 : in_len_;
            refill_input();
            continue;
        }
        in_pos_ += off;

        // Make sure a full frame is buffered before handing it to helix.
        if (in_len_ - in_pos_ < MP3_FRAME_MAX && !eof_) refill_input();

        p = in_ + in_pos_;
        int bytes_left = in_len_ - in_pos_;
        int before = bytes_left;
        int err = MP3Decode(static_cast<HMP3Decoder>(hmp3_), &p, &bytes_left, pcm_, 0);
        int consumed = before - bytes_left;

        if (err == ERR_MP3_NONE) {
            in_pos_ += consumed;
            MP3FrameInfo info;
            MP3GetLastFrameInfo(static_cast<HMP3Decoder>(hmp3_), &info);
            int nch = info.nChans > 0 ? info.nChans : 1;
            int spc = info.outputSamps / nch;          // samples per channel
            if (spc > MONO_MAX) spc = MONO_MAX;
            if (nch >= 2) {
                for (int i = 0; i < spc; ++i)
                    mono_[i] = static_cast<int16_t>(
                        (static_cast<int32_t>(pcm_[2 * i]) + pcm_[2 * i + 1]) / 2);
            } else {
                for (int i = 0; i < spc; ++i) mono_[i] = pcm_[i];
            }
            if (info.samprate > 0) rate_ = static_cast<uint32_t>(info.samprate);
            if (spc > 0) { mono_n_ = spc; mono_pos_ = 0; return true; }
            continue;   // valid but empty frame — keep going
        }

        if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
            if (eof_) return false;                           // can't satisfy — true end
            if (in_len_ - in_pos_ >= MP3_FRAME_MAX) {
                // A full frame is buffered yet helix still underflows → the sync was
                // a false positive / corrupt header. Skip past it and resync.
                in_pos_ += 1;
                continue;
            }
            refill_input();                                   // genuinely need more bytes
            continue;
        }

        // Corrupt frame — skip past it (≥1 byte) and resync.
        in_pos_ += (consumed > 0) ? consumed : 1;
    }
}

size_t Mp3Decoder::read(int16_t* buf, size_t max_frames) {
    if (!f_ || !hmp3_) return 0;
    if (mono_pos_ >= mono_n_) {
        if (!decode_next_frame()) return 0;   // EOF
    }
    int avail = mono_n_ - mono_pos_;
    int n = (static_cast<int>(max_frames) < avail) ? static_cast<int>(max_frames) : avail;
    std::memcpy(buf, mono_ + mono_pos_, n * sizeof(int16_t));
    mono_pos_ += n;
    return static_cast<size_t>(n);
}

void Mp3Decoder::close() {
    if (hmp3_) { MP3FreeDecoder(static_cast<HMP3Decoder>(hmp3_)); hmp3_ = nullptr; }
    if (f_)    { fclose(f_); f_ = nullptr; }
    in_len_ = 0; in_pos_ = 0; eof_ = false;
    mono_n_ = 0; mono_pos_ = 0;
}

} // namespace modesp::audio

#else  // CONFIG_MODESP_AUDIO_MP3 disabled — MP3 unsupported

namespace modesp::audio {

bool Mp3Decoder::open(const char* path, uint32_t&) {
    ESP_LOGW(TAG, "MP3 disabled — enable CONFIG_MODESP_AUDIO_MP3 to play %s", path);
    return false;
}
size_t Mp3Decoder::read(int16_t*, size_t) { return 0; }
void Mp3Decoder::close() {}

} // namespace modesp::audio

#endif // CONFIG_MODESP_AUDIO_MP3
