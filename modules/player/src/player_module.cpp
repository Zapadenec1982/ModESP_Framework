/**
 * @file player_module.cpp
 * @brief Audio player module — implementation.
 *
 * Signal sources → 16-bit mono PCM → IAudioSink → I²S → MAX98357A → speaker.
 *  - Tone  : sine LUT + phase accumulator (alarm beeps, no asset needed).
 *  - WAV   : streamed from /data/audio/<name>.wav (16-bit PCM mono).
 * Software volume is applied while filling each buffer (the MAX98357A has no
 * volume register). The playback task owns the sink; on_update only posts
 * requests and mirrors atomics into SharedState.
 */

#include "player_module.h"
#include "player/audio_backend_registry.h"
#include "modesp/hal/hal.h"          // HAL + BindingTable
#include "modesp/types.h"            // StringValue
#include "esp_log.h"
#include "etl/variant.h"
#include <cmath>
#include <cstring>

static const char* TAG = "Player";

namespace {

using namespace modesp::audio;

/// Fallback sink — no hardware. start() returns false so the playback task does
/// not busy-loop; requests are still acknowledged in SharedState.
class NullSink : public IAudioSink {
public:
    bool init() override { return true; }
    bool start(uint32_t rate) override {
        ESP_LOGI(TAG, "[NullSink] (no audio backend) would play @ %lu Hz", (unsigned long)rate);
        return false;
    }
    size_t write(const int16_t*, size_t, uint32_t) override { return 0; }
    void stop() override {}
    bool is_healthy() const override { return false; }
    const char* name() const override { return "null"; }
};

NullSink s_null_sink;

} // namespace

PlayerModule::PlayerModule()
    : BaseModule("player", modesp::ModulePriority::LOW)
    , sink_(&s_null_sink)   // fallback; bind_audio() swaps in the real backend
{}

// ═══════════════════════════════════════════════════════════════
// Backend binding (main task, before on_init — mirrors DisplayModule)
// ═══════════════════════════════════════════════════════════════

void PlayerModule::bind_audio(const modesp::BindingTable& bindings, modesp::HAL& hal) {
    using namespace modesp::audio;
    register_all_audio_backends();   // idempotent

    for (const auto& b : bindings.bindings) {
        if (!(b.module_name == "player")) continue;
        const char* type = b.driver_type.c_str();
        if (!AudioBackendRegistry::is_known(type)) {
            ESP_LOGW(TAG, "backend '%s' unknown/disabled in menuconfig — NullSink", type);
            return;
        }
        if (IAudioSink* s = AudioBackendRegistry::create(type, b, hal)) {
            sink_ = s;
            ESP_LOGI(TAG, "backend '%s' [hw=%s, role=%s]",
                     type, b.hardware_id.c_str(), b.role.c_str());
        } else {
            ESP_LOGW(TAG, "backend '%s' create failed — NullSink", type);
        }
        return;   // single audio output
    }
    ESP_LOGI(TAG, "no audio binding — NullSink (silent)");
}

// ═══════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════

bool PlayerModule::on_init() {
    state_set("player.playing", false);
    state_set("player.clip", "");

    for (size_t i = 0; i < LUT_SIZE; i++) {
        lut_[i] = (int16_t)(sinf(2.0f * (float)M_PI * (float)i / (float)LUT_SIZE) * 30000.0f);
    }

    if (!sink_->init()) {
        ESP_LOGW(TAG, "audio sink init failed — player muted");
        // keep going; requests stay no-op
    }

    q_ = xQueueCreate(4, sizeof(Request));
    if (!q_) {
        ESP_LOGE(TAG, "request queue create failed");
        return true;   // do not bring the system down over audio
    }
    if (xTaskCreate(&PlayerModule::task_trampoline, "player", 4096, this, 5, &task_) != pdPASS) {
        ESP_LOGE(TAG, "playback task create failed");
        return true;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Initialized (sink=%s)", sink_->name());
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Command surface (main task) — only posts requests + mirrors state
// ═══════════════════════════════════════════════════════════════

void PlayerModule::on_update(uint32_t /*dt_ms*/) {
    if (!initialized_) return;

    // Volume (software) — 0..100 % → 0..256.
    int vol = read_int("player.volume", 70);
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    volume_q8_.store((uint16_t)((vol * 256) / 100), std::memory_order_relaxed);

    if (!read_bool("player.enabled", true)) {
        if (playing_.load(std::memory_order_relaxed)) {
            Request r; r.kind = Kind::Stop; post(r);
        }
        state_set("player.playing", playing_.load(std::memory_order_relaxed));
        return;
    }

    // Stop command (momentary, self-reset).
    if (read_bool("player.stop", false)) {
        state_set("player.stop", false);
        Request r; r.kind = Kind::Stop; post(r);
    }

    // Play command (string, momentary, self-reset): "alarm" / "beep" / <wav name>.
    char name[CLIP_NAME];
    if (read_string("player.play", name, sizeof(name)) && name[0]) {
        post(make_request(name));
        state_set("player.clip", name);
        state_set("player.play", "");   // consume command
    }

    // Bench test buttons (momentary, self-reset) — web UI shortcuts.
    if (read_bool("player.btn_alarm", false)) {
        state_set("player.btn_alarm", false);
        post(make_request("alarm"));
        state_set("player.clip", "alarm");
    }
    if (read_bool("player.btn_beep", false)) {
        state_set("player.btn_beep", false);
        post(make_request("beep"));
        state_set("player.clip", "beep");
    }

    state_set("player.playing", playing_.load(std::memory_order_relaxed));
}

PlayerModule::Request PlayerModule::make_request(const char* name) const {
    Request r;
    if (std::strcmp(name, "alarm") == 0) {
        r.kind = Kind::Tone; r.priority = PRIO_ALARM;
        r.tone_hz = 2000; r.tone_beep = true;
        r.tone_total = TONE_RATE * 120;        // safety cap: 120 s, else until stop
    } else if (std::strcmp(name, "beep") == 0) {
        r.kind = Kind::Tone; r.priority = PRIO_NORMAL;
        r.tone_hz = 2000; r.tone_beep = false;
        r.tone_total = TONE_RATE / 5;          // 200 ms
    } else {
        r.kind = Kind::Wav; r.priority = PRIO_NORMAL;
        std::strncpy(r.clip, name, CLIP_NAME - 1);
    }
    return r;
}

bool PlayerModule::post(const Request& r) {
    if (!q_) return false;
    return xQueueSend(q_, &r, 0) == pdTRUE;
}

bool PlayerModule::read_string(const char* key, char* out, size_t out_sz) const {
    auto v = state_get(key);
    if (!v.has_value()) return false;
    const auto* sp = etl::get_if<modesp::StringValue>(&v.value());
    if (!sp) return false;
    std::strncpy(out, sp->c_str(), out_sz - 1);
    out[out_sz - 1] = '\0';
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Playback task — sole owner of the sink
// ═══════════════════════════════════════════════════════════════

void PlayerModule::task_trampoline(void* arg) {
    static_cast<PlayerModule*>(arg)->playback_task();
}

void PlayerModule::playback_task() {
    Request active;
    bool have_active = false;

    for (;;) {
        // 1. Fetch the next command (block when idle, poll while streaming).
        Request r;
        if (xQueueReceive(q_, &r, have_active ? 0 : portMAX_DELAY) == pdTRUE) {
            if (r.kind == Kind::Stop) {
                if (have_active) { end_active(); have_active = false; }
            } else if (have_active && active.priority == PRIO_ALARM && r.priority < PRIO_ALARM) {
                // Alarm in progress — ignore lower-priority request.
            } else {
                if (have_active) end_active();
                if (start_active(r)) { active = r; have_active = true; }
                else                 { have_active = false; }
            }
        }

        // 2. Stream one buffer of the active source.
        if (have_active) {
            if (stream_one(active)) { end_active(); have_active = false; }
        }
    }
}

bool PlayerModule::start_active(const Request& r) {
    uint32_t rate = TONE_RATE;
    if (r.kind == Kind::Wav) {
        if (!open_wav(r.clip, rate)) return false;
    }
    if (!sink_->start(rate)) {
        if (wav_) { fclose(wav_); wav_ = nullptr; }
        return false;
    }
    cur_rate_ = rate;
    phase_    = 0;
    tone_pos_ = 0;
    playing_.store(true, std::memory_order_relaxed);
    return true;
}

bool PlayerModule::stream_one(const Request& r) {
    int16_t buf[AUDIO_FRAMES];
    size_t n;

    if (r.kind == Kind::Tone) {
        bool gate = true;
        if (r.tone_beep) {
            const uint32_t period = (cur_rate_ * 2) / 5;   // 400 ms
            gate = (tone_pos_ % period) < (cur_rate_ / 5);  // 200 ms on
        }
        n = fill_tone(buf, AUDIO_FRAMES, r.tone_hz, gate);
        sink_->write(buf, n, 200);
        tone_pos_ += n;
        return (r.tone_total != 0 && tone_pos_ >= r.tone_total);
    }

    // WAV
    n = fill_wav(buf, AUDIO_FRAMES);
    if (n == 0) return true;          // EOF
    sink_->write(buf, n, 200);
    return false;
}

void PlayerModule::end_active() {
    sink_->stop();
    if (wav_) { fclose(wav_); wav_ = nullptr; wav_remaining_ = 0; }
    playing_.store(false, std::memory_order_relaxed);
}

size_t PlayerModule::fill_tone(int16_t* buf, size_t frames, uint16_t hz, bool gate) {
    if (!gate) {
        std::memset(buf, 0, frames * sizeof(int16_t));
        return frames;
    }
    const uint16_t vol = volume_q8_.load(std::memory_order_relaxed);
    const uint32_t inc = (uint32_t)(((uint64_t)hz * LUT_SIZE << 16) / cur_rate_);
    for (size_t i = 0; i < frames; i++) {
        int16_t s = lut_[(phase_ >> 16) & (LUT_SIZE - 1)];
        buf[i] = (int16_t)(((int32_t)s * vol) >> 8);
        phase_ += inc;
    }
    return frames;
}

size_t PlayerModule::fill_wav(int16_t* buf, size_t frames) {
    if (!wav_ || wav_remaining_ == 0) return 0;

    size_t want_bytes = frames * sizeof(int16_t);
    if (want_bytes > wav_remaining_) want_bytes = wav_remaining_;

    size_t got = fread(buf, 1, want_bytes, wav_);
    wav_remaining_ -= got;
    size_t n = got / sizeof(int16_t);

    const uint16_t vol = volume_q8_.load(std::memory_order_relaxed);
    if (vol != 256) {
        for (size_t i = 0; i < n; i++) {
            buf[i] = (int16_t)(((int32_t)buf[i] * vol) >> 8);
        }
    }
    return n;
}

// ── Minimal WAV reader: 16-bit PCM mono only ──
bool PlayerModule::open_wav(const char* name, uint32_t& rate_out) {
    char path[64];
    bool has_dot = (std::strchr(name, '.') != nullptr);
    std::snprintf(path, sizeof(path), "/data/audio/%s%s", name, has_dot ? "" : ".wav");

    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "clip not found: %s", path);
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

    wav_           = f;
    wav_remaining_ = data_size;
    rate_out       = rate ? rate : TONE_RATE;
    ESP_LOGI(TAG, "play %s (%lu Hz, %lu bytes)", path,
             (unsigned long)rate_out, (unsigned long)data_size);
    return true;
}
