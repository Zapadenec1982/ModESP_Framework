/**
 * @file max98357a_driver.h
 * @brief MAX98357A I²S Class-D amp — alarm sounder (IActuatorDriver)
 *
 * The MAX98357A is a mono I²S DAC + Class-D amplifier. There is no register
 * interface — it simply turns an I²S audio stream into sound, with a single
 * SD (shutdown/gain) pin for hardware mute. We drive it as a generic on/off
 * actuator so EquipmentBase can manage it exactly like a relay:
 *
 *   set(true)   → play the configured alarm tone/pattern
 *   set(false)  → silence (SD mute + I²S channel disabled)
 *   set_value(v)→ volume 0..1   (supports_analog() == true)
 *
 * Threading model (single-owner I²S channel — zero heap on the hot path):
 *   - The I²S channel + DMA buffers + sine LUT are owned by THIS driver
 *     (like the NTC driver owns its ADC handle — HAL only holds pins).
 *   - A dedicated FreeRTOS task owns the channel lifecycle (enable/disable/
 *     write). Blocking i2s_channel_write() is paced by the DMA, so the task
 *     spends its time sleeping on the DMA, not spinning.
 *   - set()/update() (main loop, 100 Hz) only flip lock-free std::atomic flags
 *     and notify the task — they NEVER touch the I²S channel directly, so there
 *     are no cross-task ESP-IDF channel races.
 *
 * Lifecycle:
 *   1. DriverManager → configure() with role + I²S pins + sample rate
 *   2. DriverManager → apply_settings() with tone_hz / volume / pattern
 *   3. DriverManager → init() (builds LUT, creates channel, spawns task)
 *   4. Main loop → update(dt_ms) sequences the beep pattern
 *   5. Business module → set(true/false) to sound/silence the alarm
 */

#pragma once

#include "modesp/hal/driver_interfaces.h"
#include "etl/string.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cstdint>

namespace modesp { struct Binding; }

class Max98357aDriver : public modesp::IActuatorDriver {
public:
    /// Alarm cadence (sequenced in update() at the main-loop rate).
    enum class Pattern : uint8_t {
        Continuous = 0,   // solid tone
        SlowBeep   = 1,   // 200 ms on / 800 ms off
        FastBeep   = 2,   // 200 ms on / 200 ms off
        DoubleBeep = 3,   // beep-beep … pause
    };

    Max98357aDriver() = default;

    /// Configure before init (called by the factory). Pins/rate come from HAL's
    /// I2SBusResource (board.json). sd == GPIO_NUM_NC ⇒ SD line is tied high.
    void configure(const char* role, gpio_num_t bclk, gpio_num_t ws,
                   gpio_num_t dout, gpio_num_t sd, uint32_t sample_rate_hz);

    /// Apply per-binding settings: tone_hz, volume (%), pattern.
    void apply_settings(const modesp::Binding& b);

    // ── IActuatorDriver interface ──
    bool init() override;
    void update(uint32_t dt_ms) override;
    bool set(bool state) override;
    bool get_state() const override { return playing_.load(std::memory_order_relaxed); }
    bool set_value(float value_0_1) override;     // volume
    float get_value() const override;
    bool supports_analog() const override { return true; }
    const char* role() const override { return role_.c_str(); }
    const char* type() const override { return "max98357a"; }
    bool is_healthy() const override { return initialized_; }
    void emergency_stop() override { set(false); }
    uint32_t switch_count() const override { return cycles_; }

private:
    static constexpr size_t LUT_SIZE     = 256;   // sine table (power of 2 → mask)
    static constexpr size_t AUDIO_FRAMES = 240;   // mono samples per DMA buffer

    static void audio_task_trampoline(void* arg);
    void audio_task();
    size_t fill_buffer(int16_t* buf, size_t frames, bool tone);
    void set_mute(bool muted);

    etl::string<16> role_;
    gpio_num_t bclk_ = GPIO_NUM_NC;
    gpio_num_t ws_   = GPIO_NUM_NC;
    gpio_num_t dout_ = GPIO_NUM_NC;
    gpio_num_t sd_   = GPIO_NUM_NC;
    uint32_t   sample_rate_ = 16000;

    // Settings (write-once before the audio task starts → read freely by task).
    uint16_t tone_hz_ = 2000;
    Pattern  pattern_ = Pattern::SlowBeep;

    // Cross-task runtime state (lock-free).
    std::atomic<bool>     playing_{false};     // overall alarm active (set by set())
    std::atomic<bool>     tone_active_{true};  // within-alarm gate (beep pattern)
    std::atomic<uint16_t> volume_q8_{179};     // 0..256 fixed-point (70 % ≈ 179)

    // Pattern sequencing (main task only).
    uint32_t pattern_ms_ = 0;

    // I²S + task (audio task only after init).
    i2s_chan_handle_t tx_handle_   = nullptr;
    TaskHandle_t      task_handle_ = nullptr;
    uint32_t          phase_       = 0;        // Q16 phase accumulator
    int16_t           lut_[LUT_SIZE]{};

    bool configured_  = false;
    bool initialized_ = false;
    uint32_t cycles_  = 0;
};
