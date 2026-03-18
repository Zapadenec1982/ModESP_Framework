/**
 * @file ntc_driver.cpp
 * @brief NTC thermistor via ADC — B-parameter temperature calculation
 *
 * Схема підключення:
 *   VCC(3.3V) — [R_series] — ADC_PIN — [NTC] — GND
 *
 * R_ntc = R_series * adc_raw / (ADC_MAX - adc_raw)
 * T = 1 / (1/T0 + (1/beta) * ln(R_ntc / R_nominal)) - 273.15
 */

#include "ntc_driver.h"
#include "esp_log.h"
#include <cmath>

static const char* TAG = "NTC";

// Спільний ADC1 handle для всіх NTC instances.
// adc_oneshot_new_unit(ADC_UNIT_1) можна викликати тільки ОДИН раз —
// другий виклик повертає ESP_ERR_INVALID_STATE і драйвер не ініціалізується.
static adc_oneshot_unit_handle_t s_adc1_handle = nullptr;

void NtcDriver::configure(const char* role, gpio_num_t gpio, uint8_t atten) {
    role_ = role;
    gpio_ = gpio;
    atten_ = atten;
    configured_ = true;
}

bool NtcDriver::gpio_to_adc1_channel(gpio_num_t gpio, adc_channel_t& out) {
    // ESP32 ADC1 GPIO mapping
    switch (gpio) {
        case GPIO_NUM_36: out = ADC_CHANNEL_0; return true;
        case GPIO_NUM_37: out = ADC_CHANNEL_1; return true;
        case GPIO_NUM_38: out = ADC_CHANNEL_2; return true;
        case GPIO_NUM_39: out = ADC_CHANNEL_3; return true;
        case GPIO_NUM_32: out = ADC_CHANNEL_4; return true;
        case GPIO_NUM_33: out = ADC_CHANNEL_5; return true;
        case GPIO_NUM_34: out = ADC_CHANNEL_6; return true;
        case GPIO_NUM_35: out = ADC_CHANNEL_7; return true;
        default:          return false;
    }
}

bool NtcDriver::init() {
    if (!configured_) {
        ESP_LOGE(TAG, "Driver not configured");
        return false;
    }

    if (!gpio_to_adc1_channel(gpio_, adc_channel_)) {
        ESP_LOGE(TAG, "[%s] GPIO %d is not ADC1 channel", role_.c_str(), gpio_);
        return false;
    }

    // Ініціалізуємо ADC1 oneshot — один shared handle для всіх NTC instances.
    // Якщо вже ініціалізований (другий датчик) — просто використовуємо існуючий.
    esp_err_t err = ESP_OK;
    if (!s_adc1_handle) {
        adc_oneshot_unit_init_cfg_t unit_cfg = {};
        unit_cfg.unit_id = ADC_UNIT_1;
        unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

        err = adc_oneshot_new_unit(&unit_cfg, &s_adc1_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[%s] ADC unit init failed: %s", role_.c_str(), esp_err_to_name(err));
            return false;
        }
    }
    adc_handle_ = s_adc1_handle;

    // Конфігуруємо канал
    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.bitwidth = ADC_BITWIDTH_12;
    chan_cfg.atten = (adc_atten_t)atten_;

    err = adc_oneshot_config_channel(adc_handle_, adc_channel_, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] ADC channel config failed: %s", role_.c_str(), esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "[%s] Initialized (GPIO=%d, channel=%d, atten=%d, beta=%d)",
             role_.c_str(), gpio_, adc_channel_, atten_, beta_);
    return true;
}

void NtcDriver::update(uint32_t dt_ms) {
    if (!adc_handle_) return;

    ms_since_read_ += dt_ms;
    if (ms_since_read_ < read_interval_ms_) return;
    ms_since_read_ = 0;

    int raw = 0;
    esp_err_t err = adc_oneshot_read(adc_handle_, adc_channel_, &raw);
    if (err != ESP_OK) {
        consecutive_errors_++;
        ESP_LOGW(TAG, "[%s] ADC read failed: %s", role_.c_str(), esp_err_to_name(err));
        return;
    }

    // Перевірка на обрив/короткозамкнення NTC
    if (raw < ADC_MIN_VALID || raw > ADC_MAX_VALID) {
        consecutive_errors_++;
        if (consecutive_errors_ == MAX_CONSECUTIVE_ERRORS) {
            ESP_LOGE(TAG, "[%s] Sensor fault (ADC=%d)", role_.c_str(), raw);
        }
        return;
    }

    float temp = adc_to_temperature(raw);

    if (temp >= MIN_VALID_TEMP && temp <= MAX_VALID_TEMP) {
        current_temp_ = temp;
        has_valid_ = true;
        consecutive_errors_ = 0;
        ESP_LOGD(TAG, "[%s] %.1f°C (ADC=%d)", role_.c_str(), temp, raw);
    } else {
        consecutive_errors_++;
        ESP_LOGW(TAG, "[%s] Out of range: %.1f°C (ADC=%d)", role_.c_str(), temp, raw);
    }
}

bool NtcDriver::read(float& value) {
    if (!has_valid_) return false;
    value = current_temp_;
    return true;
}

bool NtcDriver::is_healthy() const {
    return consecutive_errors_ < MAX_CONSECUTIVE_ERRORS;
}

float NtcDriver::adc_to_temperature(int raw) {
    // Voltage divider: R_ntc = R_series * raw / (ADC_MAX - raw)
    float r_ntc = (float)r_series_ * (float)raw / (float)(ADC_MAX_RAW - raw);

    // B-parameter equation: T = 1 / (1/T0 + (1/beta) * ln(R/R0))
    float inv_t = (1.0f / T0_KELVIN) + (1.0f / (float)beta_) * logf(r_ntc / (float)r_nominal_);
    float temp_c = (1.0f / inv_t) - 273.15f + offset_;

    return temp_c;
}
