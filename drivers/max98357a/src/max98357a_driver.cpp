/**
 * @file max98357a_driver.cpp
 * @brief MAX98357A I²S alarm sounder — implementation
 *
 * Signal chain: sine LUT → phase accumulator → volume scale → I²S TX DMA →
 * MAX98357A → speaker. The SD pin gives a true hardware mute (and powers the
 * amp down) while idle; within an active alarm the beep pattern gates tone vs
 * silence by what the audio task writes.
 */

#include "max98357a_driver.h"
#include "modesp/hal/hal_types.h"   // complete modesp::Binding for apply_settings()
#include "esp_log.h"
#include <cmath>
#include <cstring>

static const char* TAG = "MAX98357A";

// ═══════════════════════════════════════════════════════════════
// Configure / settings (called by factory before init)
// ═══════════════════════════════════════════════════════════════

void Max98357aDriver::configure(const char* role, gpio_num_t bclk, gpio_num_t ws,
                                gpio_num_t dout, gpio_num_t sd, uint32_t sample_rate_hz) {
    role_         = role;
    bclk_         = bclk;
    ws_           = ws;
    dout_         = dout;
    sd_           = sd;
    sample_rate_  = sample_rate_hz ? sample_rate_hz : 16000;
    configured_   = true;
}

void Max98357aDriver::apply_settings(const modesp::Binding& b) {
    tone_hz_ = static_cast<uint16_t>(b.setting_or("tone_hz", tone_hz_));

    int vol_pct = static_cast<int>(b.setting_or("volume", 70.0f));
    if (vol_pct < 0)   vol_pct = 0;
    if (vol_pct > 100) vol_pct = 100;
    volume_q8_.store(static_cast<uint16_t>((vol_pct * 256) / 100), std::memory_order_relaxed);

    int p = static_cast<int>(b.setting_or("pattern", static_cast<float>(static_cast<int>(pattern_))));
    if (p < 0) p = 0;
    if (p > 3) p = 3;
    pattern_ = static_cast<Pattern>(p);
}

// ═══════════════════════════════════════════════════════════════
// SD / hardware mute  (MAX98357A: SD HIGH = enabled, LOW = shutdown)
// ═══════════════════════════════════════════════════════════════

void Max98357aDriver::set_mute(bool muted) {
    if (sd_ == GPIO_NUM_NC) return;          // SD tied high externally — always on
    gpio_set_level(sd_, muted ? 0 : 1);
}

// ═══════════════════════════════════════════════════════════════
// IActuatorDriver lifecycle
// ═══════════════════════════════════════════════════════════════

bool Max98357aDriver::init() {
    if (!configured_) {
        ESP_LOGE(TAG, "Driver not configured — call configure() first");
        return false;
    }

    // SD pin as output, start muted (amp powered down).
    if (sd_ != GPIO_NUM_NC) {
        gpio_config_t io = {};
        io.pin_bit_mask = (1ULL << sd_);
        io.mode         = GPIO_MODE_OUTPUT;
        io.pull_up_en   = GPIO_PULLUP_DISABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type    = GPIO_INTR_DISABLE;
        esp_err_t gerr = gpio_config(&io);
        if (gerr != ESP_OK) {
            ESP_LOGE(TAG, "[%s] SD GPIO %d config failed: %s",
                     role_.c_str(), (int)sd_, esp_err_to_name(gerr));
            return false;
        }
    }
    set_mute(true);

    // Build a full-scale sine LUT (amplitude leaves headroom below INT16_MAX).
    for (size_t i = 0; i < LUT_SIZE; i++) {
        float angle = 2.0f * (float)M_PI * (float)i / (float)LUT_SIZE;
        lut_[i] = (int16_t)(sinf(angle) * 30000.0f);
    }

    // Create the I²S TX channel (this driver owns it — like NTC owns its ADC).
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 4;
    chan_cfg.dma_frame_num = AUDIO_FRAMES;
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle_, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] i2s_new_channel failed: %s", role_.c_str(), esp_err_to_name(err));
        return false;
    }

    // 16-bit mono, Philips standard. clk/slot via the standard default-config
    // macros; GPIOs assigned field-by-field (no din/mclk for a TX-only amp).
    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_);
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                           I2S_SLOT_MODE_MONO);
    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = bclk_;
    std_cfg.gpio_cfg.ws   = ws_;
    std_cfg.gpio_cfg.dout = dout_;
    std_cfg.gpio_cfg.din  = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv   = false;

    err = i2s_channel_init_std_mode(tx_handle_, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] i2s_channel_init_std_mode failed: %s",
                 role_.c_str(), esp_err_to_name(err));
        i2s_del_channel(tx_handle_);
        tx_handle_ = nullptr;
        return false;
    }

    // Audio feeder task. Stack covers the local DMA staging buffer + I²S calls.
    BaseType_t ok = xTaskCreate(&Max98357aDriver::audio_task_trampoline,
                                "max98357a", 4096, this, 5, &task_handle_);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "[%s] audio task create failed", role_.c_str());
        i2s_del_channel(tx_handle_);
        tx_handle_ = nullptr;
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "[%s] ready (BCLK=%d WS=%d DOUT=%d SD=%d, %lu Hz, tone=%u Hz, pattern=%d)",
             role_.c_str(), (int)bclk_, (int)ws_, (int)dout_, (int)sd_,
             (unsigned long)sample_rate_, (unsigned)tone_hz_, (int)pattern_);
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Pattern sequencing (main loop — only flips the lock-free gate)
// ═══════════════════════════════════════════════════════════════

void Max98357aDriver::update(uint32_t dt_ms) {
    if (!playing_.load(std::memory_order_relaxed)) {
        pattern_ms_ = 0;
        tone_active_.store(true, std::memory_order_relaxed);  // armed for next play
        return;
    }

    pattern_ms_ += dt_ms;
    bool on = true;
    switch (pattern_) {
        case Pattern::Continuous:
            on = true;
            break;
        case Pattern::SlowBeep: {
            uint32_t t = pattern_ms_ % 1000;   // 200 on / 800 off
            on = (t < 200);
            break;
        }
        case Pattern::FastBeep: {
            uint32_t t = pattern_ms_ % 400;    // 200 on / 200 off
            on = (t < 200);
            break;
        }
        case Pattern::DoubleBeep: {
            uint32_t t = pattern_ms_ % 1200;   // beep-beep … pause
            on = (t < 150) || (t >= 300 && t < 450);
            break;
        }
    }
    tone_active_.store(on, std::memory_order_relaxed);
}

// ═══════════════════════════════════════════════════════════════
// On/off + volume — never touch the I²S channel directly (task owns it)
// ═══════════════════════════════════════════════════════════════

bool Max98357aDriver::set(bool state) {
    if (!initialized_) return false;
    if (state == playing_.load(std::memory_order_relaxed)) return true;  // idempotent

    playing_.store(state, std::memory_order_relaxed);
    if (state) {
        pattern_ms_ = 0;
        tone_active_.store(true, std::memory_order_relaxed);
        cycles_++;
        if (task_handle_) xTaskNotifyGive(task_handle_);  // wake idle task
        ESP_LOGI(TAG, "[%s] ALARM ON (cycle #%lu)", role_.c_str(), (unsigned long)cycles_);
    } else {
        // Task observes playing_==false next iteration → disables + mutes itself.
        ESP_LOGI(TAG, "[%s] alarm OFF", role_.c_str());
    }
    return true;
}

bool Max98357aDriver::set_value(float value_0_1) {
    if (value_0_1 < 0.0f) value_0_1 = 0.0f;
    if (value_0_1 > 1.0f) value_0_1 = 1.0f;
    volume_q8_.store(static_cast<uint16_t>(value_0_1 * 256.0f + 0.5f), std::memory_order_relaxed);
    return true;
}

float Max98357aDriver::get_value() const {
    if (!playing_.load(std::memory_order_relaxed)) return 0.0f;
    return volume_q8_.load(std::memory_order_relaxed) / 256.0f;
}

// ═══════════════════════════════════════════════════════════════
// Audio task — sole owner of the I²S channel lifecycle
// ═══════════════════════════════════════════════════════════════

void Max98357aDriver::audio_task_trampoline(void* arg) {
    static_cast<Max98357aDriver*>(arg)->audio_task();
}

size_t Max98357aDriver::fill_buffer(int16_t* buf, size_t frames, bool tone) {
    if (!tone) {
        std::memset(buf, 0, frames * sizeof(int16_t));
        return frames;
    }
    const uint16_t vol = volume_q8_.load(std::memory_order_relaxed);
    // Q16 phase increment: tone_hz cycles/sec across LUT_SIZE entries / sample_rate.
    const uint32_t inc = (uint32_t)(((uint64_t)tone_hz_ * LUT_SIZE << 16) / sample_rate_);
    for (size_t i = 0; i < frames; i++) {
        int16_t s = lut_[(phase_ >> 16) & (LUT_SIZE - 1)];
        buf[i] = (int16_t)(((int32_t)s * vol) >> 8);
        phase_ += inc;
    }
    return frames;
}

void Max98357aDriver::audio_task() {
    int16_t buf[AUDIO_FRAMES];
    bool channel_on = false;

    for (;;) {
        if (!playing_.load(std::memory_order_relaxed)) {
            if (channel_on) {
                set_mute(true);
                i2s_channel_disable(tx_handle_);
                channel_on = false;
                phase_ = 0;
            }
            // Sleep until set(true) notifies; wake periodically as a safety net.
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }

        if (!channel_on) {
            if (i2s_channel_enable(tx_handle_) != ESP_OK) {
                ESP_LOGE(TAG, "[%s] i2s_channel_enable failed", role_.c_str());
                playing_.store(false, std::memory_order_relaxed);
                continue;
            }
            set_mute(false);
            channel_on = true;
        }

        bool tone = tone_active_.load(std::memory_order_relaxed);
        size_t n = fill_buffer(buf, AUDIO_FRAMES, tone);
        size_t written = 0;
        // Blocking write paced by the DMA → re-checks playing_ each buffer (~15 ms).
        i2s_channel_write(tx_handle_, buf, n * sizeof(int16_t), &written, 200);
    }
}

// ═══════════════════════════════════════════════════════════════
// Driver factory + registration (optional via CONFIG_MODESP_DRIVER_MAX98357A)
// ═══════════════════════════════════════════════════════════════

#include "modesp/hal/driver_registry.h"
#include "modesp/hal/hal.h"
#include "etl/string_view.h"

namespace {
Max98357aDriver s_max98357a_pool[modesp::MAX_I2S_BUSES];
size_t          s_max98357a_n = 0;

modesp::IActuatorDriver* max98357a_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_max98357a_n >= modesp::MAX_I2S_BUSES) {
        ESP_LOGE(TAG, "MAX98357A pool exhausted");
        return nullptr;
    }
    auto* bus = hal.find_i2s_bus(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!bus) {
        ESP_LOGE(TAG, "I2S bus '%s' not found in HAL", b.hardware_id.c_str());
        return nullptr;
    }
    auto& drv = s_max98357a_pool[s_max98357a_n++];
    drv.configure(b.role.c_str(), bus->bclk, bus->ws, bus->dout, bus->sd, bus->sample_rate_hz);
    drv.apply_settings(b);
    return &drv;
}
} // namespace

MODESP_REGISTER_ACTUATOR(max98357a, &max98357a_factory)
