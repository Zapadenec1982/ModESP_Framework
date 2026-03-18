/**
 * @file digital_input_driver.cpp
 * @brief GPIO digital input with debounce
 *
 * Читає GPIO з програмним дебаунсом (50мс).
 * Підтримує інверсію для NC контактів (door_contact, кінцевики).
 */

#include "digital_input_driver.h"
#include "esp_log.h"

static const char* TAG = "DigInput";

void DigitalInputDriver::configure(const char* role, gpio_num_t gpio,
                                   bool pull_up, bool invert) {
    role_ = role;
    gpio_ = gpio;
    pull_up_ = pull_up;
    invert_ = invert;
    configured_ = true;
}

bool DigitalInputDriver::init() {
    if (!configured_) {
        ESP_LOGE(TAG, "Driver not configured");
        return false;
    }

    // GPIO вже сконфігурований HAL (init_gpio_inputs), тільки логуємо
    // Початковий стан
    int level = gpio_get_level(gpio_);
    raw_state_ = invert_ ? (level == 0) : (level != 0);
    debounced_state_ = raw_state_;
    stable_ms_ = DEBOUNCE_MS;  // Вважаємо стабільним з самого початку
    initialized_ = true;

    ESP_LOGI(TAG, "[%s] Initialized (GPIO=%d, pull_%s, invert=%s, state=%d)",
             role_.c_str(), gpio_,
             pull_up_ ? "UP" : "DOWN",
             invert_ ? "yes" : "no",
             debounced_state_);
    return true;
}

void DigitalInputDriver::update(uint32_t dt_ms) {
    if (!initialized_) return;

    int level = gpio_get_level(gpio_);
    bool current = invert_ ? (level == 0) : (level != 0);

    if (current == raw_state_) {
        // Стан стабільний — нарощуємо таймер
        stable_ms_ += dt_ms;
        if (stable_ms_ >= DEBOUNCE_MS && current != debounced_state_) {
            debounced_state_ = current;
            ESP_LOGD(TAG, "[%s] → %s", role_.c_str(), current ? "ON" : "OFF");
        }
    } else {
        // Стан змінився — скидаємо таймер
        raw_state_ = current;
        stable_ms_ = 0;
    }
}

bool DigitalInputDriver::read(float& value) {
    if (!initialized_) return false;
    value = debounced_state_ ? 1.0f : 0.0f;
    return true;
}
