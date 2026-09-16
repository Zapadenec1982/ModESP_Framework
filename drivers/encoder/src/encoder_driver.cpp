/**
 * @file encoder_driver.cpp
 * @brief Квадратурний декодер поворотного енкодера
 *
 * Декодує два піни (CLK/DT) у знаковий лічильник детентів за таблицею
 * переходів сірого коду. Опитування на кожному такті (100 Гц) — достатньо
 * для енкодера, що крутять рукою. Кнопка енкодера — окремий digital_input.
 */

#include "encoder_driver.h"
#include "esp_log.h"

static const char* TAG = "Encoder";

// Таблиця квадратурних переходів: індекс = (prev << 2) | curr,
// значення = напрямок чвертькроку (+1 CW / -1 CCW; 0 — без руху або брязкіт).
static const int8_t QUAD_LUT[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0,
};

void EncoderDriver::configure(const char* role, gpio_num_t clk, gpio_num_t dt,
                              uint8_t steps_per_detent, bool invert) {
    role_ = role;
    clk_ = clk;
    dt_ = dt;
    steps_per_detent_ = steps_per_detent ? steps_per_detent : 1;
    invert_ = invert;
    configured_ = true;
}

uint8_t EncoderDriver::read_state() const {
    return (uint8_t)((gpio_get_level(clk_) << 1) | gpio_get_level(dt_));
}

bool EncoderDriver::init() {
    if (!configured_) {
        ESP_LOGE(TAG, "Driver not configured");
        return false;
    }

    // Піни вже сконфігуровані HAL (init_encoders) — лише знімаємо стартовий стан
    prev_state_ = read_state();
    sub_count_ = 0;
    position_ = 0;
    initialized_ = true;

    ESP_LOGI(TAG, "[%s] Initialized (CLK=%d, DT=%d, steps/detent=%d, invert=%s)",
             role_.c_str(), clk_, dt_, steps_per_detent_, invert_ ? "yes" : "no");
    return true;
}

void EncoderDriver::update(uint32_t /*dt_ms*/) {
    if (!initialized_) return;

    uint8_t curr = read_state();
    if (curr == prev_state_) return;          // стан не змінився

    int8_t dir = QUAD_LUT[(prev_state_ << 2) | curr];
    prev_state_ = curr;
    if (dir == 0) return;                      // брязкіт / пропущений крок

    sub_count_ += invert_ ? -dir : dir;

    // Накопичили повний детент в один або інший бік?
    if (sub_count_ >= (int8_t)steps_per_detent_) {
        sub_count_ -= steps_per_detent_;
        position_++;
        ESP_LOGD(TAG, "[%s] CW  (pos=%ld)", role_.c_str(), (long)position_);
    } else if (sub_count_ <= -(int8_t)steps_per_detent_) {
        sub_count_ += steps_per_detent_;
        position_--;
        ESP_LOGD(TAG, "[%s] CCW (pos=%ld)", role_.c_str(), (long)position_);
    }
}

bool EncoderDriver::read(float& value) {
    if (!initialized_) return false;
    value = (float)position_;
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Driver factory + registration (optional via CONFIG_MODESP_DRIVER_ENCODER)
// ═══════════════════════════════════════════════════════════════

#include "modesp/hal/driver_registry.h"
#include "modesp/hal/hal.h"
#include "etl/string_view.h"

namespace {
EncoderDriver s_enc_pool[modesp::MAX_ENCODERS];
size_t        s_enc_n = 0;

modesp::ISensorDriver* encoder_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_enc_n >= modesp::MAX_ENCODERS) {
        ESP_LOGE(TAG, "Encoder pool exhausted");
        return nullptr;
    }
    auto* enc = hal.find_encoder(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!enc) {
        ESP_LOGE(TAG, "Encoder '%s' not found in HAL", b.hardware_id.c_str());
        return nullptr;
    }
    auto& drv = s_enc_pool[s_enc_n++];
    uint8_t spd = (uint8_t)b.setting_or("steps_per_detent", 4.0f);
    bool    inv = b.setting_or("invert", 0.0f) > 0.5f;
    drv.configure(b.role.c_str(), enc->clk, enc->dt, spd, inv);
    return &drv;
}
} // namespace

MODESP_REGISTER_SENSOR(encoder, &encoder_factory)
