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

// ═══════════════════════════════════════════════════════════════
// Driver factory + registration (optional via CONFIG_MODESP_DRIVER_DIGITAL_INPUT)
// ═══════════════════════════════════════════════════════════════

#include "modesp/hal/driver_registry.h"
#include "modesp/hal/hal.h"
#include "etl/string_view.h"

namespace {
DigitalInputDriver s_di_pool[modesp::MAX_SENSORS];
size_t             s_di_n = 0;

modesp::ISensorDriver* digital_input_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_di_n >= modesp::MAX_SENSORS) {
        ESP_LOGE(TAG, "DigitalInput pool exhausted");
        return nullptr;
    }
    auto* gpio_res = hal.find_gpio_input(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!gpio_res) {
        ESP_LOGE(TAG, "GPIO input '%s' not found in HAL", b.hardware_id.c_str());
        return nullptr;
    }
    auto& drv = s_di_pool[s_di_n++];
    drv.configure(b.role.c_str(), gpio_res->gpio, gpio_res->pull_up);
    return &drv;
}
} // namespace

MODESP_REGISTER_SENSOR(digital_input, &digital_input_factory)
