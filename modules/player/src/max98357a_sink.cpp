/**
 * @file max98357a_sink.cpp
 * @brief MAX98357A I²S Class-D amplifier as an IAudioSink (compiled only with
 *        CONFIG_MODESP_AUDIO_MAX98357A — like the display backends).
 *
 * Pure PCM transport: owns the I²S TX channel + DMA and the SD/shutdown pin
 * (hardware mute). It knows nothing about clips, decoders, tones or volume —
 * the player generates/decodes 16-bit mono PCM and hands it to write(). All
 * methods are called from the player's single playback task, in sequence.
 */

#include "sdkconfig.h"

#ifdef CONFIG_MODESP_AUDIO_MAX98357A

#include "player/audio_sink.h"
#include "player/audio_backend_registry.h"
#include "modesp/hal/hal.h"            // HAL + Binding (board.json/bindings)
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "etl/string_view.h"

namespace modesp::audio {

static const char* TAG = "max98357a_sink";

namespace {

class Max98357aSink : public IAudioSink {
public:
    Max98357aSink(gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout,
                  gpio_num_t sd, uint32_t default_rate)
        : bclk_(bclk), ws_(ws), dout_(dout), sd_(sd), rate_(default_rate ? default_rate : 16000) {}

    bool init() override {
        if (sd_ != GPIO_NUM_NC) {
            gpio_config_t io = {};
            io.pin_bit_mask = (1ULL << sd_);
            io.mode         = GPIO_MODE_OUTPUT;
            io.pull_up_en   = GPIO_PULLUP_DISABLE;
            io.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io.intr_type    = GPIO_INTR_DISABLE;
            if (gpio_config(&io) != ESP_OK) {
                ESP_LOGE(TAG, "SD GPIO %d config failed", (int)sd_);
                return false;
            }
        }
        mute(true);

        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        chan_cfg.dma_desc_num  = 4;
        chan_cfg.dma_frame_num = 240;
        esp_err_t err = i2s_new_channel(&chan_cfg, &tx_, nullptr);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
            return false;
        }

        i2s_std_config_t std_cfg = {};
        std_cfg.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(rate_);
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

        err = i2s_channel_init_std_mode(tx_, &std_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s init std failed: %s", esp_err_to_name(err));
            i2s_del_channel(tx_);
            tx_ = nullptr;
            return false;
        }

        ready_ = true;
        ESP_LOGI(TAG, "ready (BCLK=%d WS=%d DOUT=%d SD=%d, %lu Hz)",
                 (int)bclk_, (int)ws_, (int)dout_, (int)sd_, (unsigned long)rate_);
        return true;
    }

    AudioCaps caps() const override { return AudioCaps{48000, false}; }

    bool start(uint32_t sample_rate_hz) override {
        if (!ready_) return false;
        if (sample_rate_hz == 0) sample_rate_hz = rate_;

        if (enabled_) { i2s_channel_disable(tx_); enabled_ = false; }

        if (sample_rate_hz != rate_) {
            i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
            esp_err_t err = i2s_channel_reconfig_std_clock(tx_, &clk);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "reconfig clock %lu Hz failed: %s",
                         (unsigned long)sample_rate_hz, esp_err_to_name(err));
                return false;
            }
            rate_ = sample_rate_hz;
        }

        esp_err_t err = i2s_channel_enable(tx_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "channel enable failed: %s", esp_err_to_name(err));
            return false;
        }
        enabled_ = true;
        mute(false);
        return true;
    }

    size_t write(const int16_t* pcm, size_t frames, uint32_t timeout_ms) override {
        if (!enabled_ || !pcm || frames == 0) return 0;
        size_t bytes_written = 0;
        i2s_channel_write(tx_, pcm, frames * sizeof(int16_t), &bytes_written, timeout_ms);
        return bytes_written / sizeof(int16_t);
    }

    void stop() override {
        mute(true);
        if (enabled_) { i2s_channel_disable(tx_); enabled_ = false; }
    }

    bool is_healthy() const override { return ready_; }
    const char* name() const override { return "max98357a"; }

private:
    void mute(bool muted) {
        if (sd_ != GPIO_NUM_NC) gpio_set_level(sd_, muted ? 0 : 1);  // SD HIGH = on
    }

    gpio_num_t bclk_, ws_, dout_, sd_;
    uint32_t   rate_;
    i2s_chan_handle_t tx_ = nullptr;
    bool ready_   = false;
    bool enabled_ = false;
};

// ── Backend factory + registration (board.json i2s_buses → IAudioSink) ──
IAudioSink* max98357a_factory(const modesp::Binding& b, modesp::HAL& hal) {
    auto* bus = hal.find_i2s_bus(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!bus) {
        ESP_LOGE(TAG, "no i2s bus '%s' in board.json", b.hardware_id.c_str());
        return nullptr;
    }
    // Audio output is a singleton — one static sink (zero heap), built in place.
    static Max98357aSink sink(bus->bclk, bus->ws, bus->dout, bus->sd, bus->sample_rate_hz);
    return &sink;
}

} // namespace

MODESP_REGISTER_AUDIO(max98357a, &max98357a_factory)

} // namespace modesp::audio

#endif // CONFIG_MODESP_AUDIO_MAX98357A
