/**
 * @file equipment_module.cpp
 * @brief Equipment Manager — єдиний власник HAL drivers
 *
 * Потік даних кожен цикл:
 *   1. read_sensors()      — читає сенсори → SharedState
 *   2. read_requests()     — читає req.* від бізнес-модулів
 *   3. apply_arbitration() — арбітраж + інтерлоки → визначає outputs
 *   4. apply_outputs()     — застосовує outputs ДО реле (той самий цикл)
 *   5. publish_state()     — публікує фактичний стан актуаторів
 *
 * ВАЖЛИВО: apply_outputs() після apply_arbitration() в тому самому циклі.
 * Попередній порядок (apply_outputs першим) створював осциляцію через
 * однотактову затримку між рішенням арбітражу та його застосуванням.
 */

#include "equipment_module.h"
#include "modesp/hal/driver_manager.h"
#include "esp_log.h"
#include <cstring>
#include <cmath>

static const char* TAG = "Equipment";

// ═══════════════════════════════════════════════════════════════
// Constructor
// ═══════════════════════════════════════════════════════════════

EquipmentModule::EquipmentModule()
    : BaseModule("equipment", modesp::ModulePriority::CRITICAL)
{}

// ═══════════════════════════════════════════════════════════════
// Driver binding — єдиний модуль з доступом до HAL
// ═══════════════════════════════════════════════════════════════

void EquipmentModule::bind_drivers(modesp::DriverManager& dm) {
    // Обов'язкові
    sensor_air_  = dm.find_sensor("air_temp");
    compressor_  = dm.find_actuator("compressor");

    // Опціональні сенсори
    sensor_evap_ = dm.find_sensor("evap_temp");
    sensor_cond_ = dm.find_sensor("condenser_temp");

    // Актуатори
    defrost_relay_ = dm.find_actuator("defrost_relay");
    evap_fan_      = dm.find_actuator("evap_fan");
    cond_fan_      = dm.find_actuator("cond_fan");

    // Дискретні входи
    door_sensor_  = dm.find_sensor("door_contact");
    night_sensor_ = dm.find_sensor("night_input");

    if (!sensor_air_) {
        ESP_LOGE(TAG, "Sensor 'air_temp' not found — REQUIRED");
    }
    if (!compressor_) {
        ESP_LOGE(TAG, "Actuator 'compressor' not found — REQUIRED");
    }
    if (sensor_evap_)   ESP_LOGI(TAG, "Evaporator sensor bound");
    if (sensor_cond_)   ESP_LOGI(TAG, "Condenser sensor bound");
    if (defrost_relay_) ESP_LOGI(TAG, "Defrost relay bound");
    if (evap_fan_)      ESP_LOGI(TAG, "Evaporator fan bound");
    if (cond_fan_)      ESP_LOGI(TAG, "Condenser fan bound");
    if (door_sensor_)   ESP_LOGI(TAG, "Door contact bound");
    if (night_sensor_)  ESP_LOGI(TAG, "Night input bound");
}

// ═══════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════

bool EquipmentModule::on_init() {
    if (!sensor_air_ || !compressor_) {
        ESP_LOGE(TAG, "Required drivers not bound — call bind_drivers() first");
        return false;
    }

    // Початковий стан в SharedState
    // Оптимістична ініціалізація: sensor_ok = true щоб Protection
    // не спрацьовувала ERR1/ERR2 до першого реального read().
    // DS18B20 потребує ~750ms на першу конверсію — перші read() фейляться.
    // Якщо датчик не сконфігурований — теж true (не помилка).
    state_set("equipment.air_temp", 0.0f);
    state_set("equipment.evap_temp", 0.0f);
    state_set("equipment.cond_temp", 0.0f);
    state_set("equipment.sensor1_ok", true);
    state_set("equipment.sensor2_ok", true);
    state_set("equipment.compressor", false);
    state_set("equipment.defrost_relay", false);
    state_set("equipment.evap_fan", false);
    state_set("equipment.cond_fan", false);
    state_set("equipment.door_open", false);
    state_set("equipment.night_input", false);

    // Публікуємо наявність опціонального обладнання —
    // UI (visible_when, disabled options) та бізнес-модулі перевіряють
    state_set("equipment.has_defrost_relay", defrost_relay_ != nullptr);
    state_set("equipment.has_cond_fan", cond_fan_ != nullptr);
    state_set("equipment.has_door_contact", door_sensor_ != nullptr);
    state_set("equipment.has_evap_temp", sensor_evap_ != nullptr);
    state_set("equipment.has_cond_temp", sensor_cond_ != nullptr);
    state_set("equipment.has_night_input", night_sensor_ != nullptr);

    // Тип драйверів — для visibility карток налаштувань в UI
    // ISensorDriver::type() повертає "ds18b20", "ntc", "digital_input" тощо
    bool uses_ntc = false;
    bool uses_ds18b20 = false;
    auto check_type = [&](modesp::ISensorDriver* s) {
        if (!s) return;
        if (strcmp(s->type(), "ntc") == 0) uses_ntc = true;
        if (strcmp(s->type(), "ds18b20") == 0) uses_ds18b20 = true;
    };
    check_type(sensor_air_);
    check_type(sensor_evap_);
    check_type(sensor_cond_);
    state_set("equipment.has_ntc_driver", uses_ntc);
    state_set("equipment.has_ds18b20_driver", uses_ds18b20);

    ESP_LOGI(TAG, "Initialized (air_sensor=%s, compressor=%s, ntc=%s, ds18b20=%s)",
             sensor_air_ ? "OK" : "MISSING",
             compressor_ ? "OK" : "MISSING",
             uses_ntc ? "yes" : "no",
             uses_ds18b20 ? "yes" : "no");
    return true;
}

void EquipmentModule::on_update(uint32_t dt_ms) {
    // AUDIT-003: оновлюємо таймер компресора
    comp_since_ms_ += dt_ms;
    if (comp_since_ms_ > modesp::TIMER_SATISFIED) comp_since_ms_ = modesp::TIMER_SATISFIED;

    // 1. Читаємо сенсори → SharedState
    read_sensors();

    // 2. Читаємо requests від бізнес-модулів
    read_requests();

    // 3. Арбітраж + інтерлоки → визначаємо outputs
    apply_arbitration();

    // 4. Застосовуємо outputs до реле (в тому самому циклі!)
    //    Раніше apply_outputs() був першим і застосовував out_ з ПОПЕРЕДНЬОГО циклу.
    //    Це створювало осциляцію: anti-short-cycle блокував ON → out_=false,
    //    але relay вже ввімкнулось з попереднього out_=true → toggle кожен цикл.
    apply_outputs();

    // 5. Публікуємо фактичний стан актуаторів
    publish_state();
}

void EquipmentModule::on_message(const etl::imessage& msg) {
    // Safe mode — все вимкнути (NEW-001 fix: було 1, а SYSTEM_SAFE_MODE = 7)
    if (msg.get_message_id() == modesp::msg_id::SYSTEM_SAFE_MODE) {
        out_ = {};
        apply_outputs();
        publish_state();
        ESP_LOGW(TAG, "SAFE MODE — all outputs OFF");
    }
}

void EquipmentModule::on_stop() {
    // Аварійна зупинка — все вимкнути
    out_ = {};
    apply_outputs();
    ESP_LOGI(TAG, "Equipment stopped — all outputs OFF");
}

// ═══════════════════════════════════════════════════════════════
// Читання сенсорів
// ═══════════════════════════════════════════════════════════════

void EquipmentModule::read_sensors() {
    // Коефіцієнт цифрового фільтра (0 = вимкнено, 1-10 = EMA)
    int coeff = read_int("equipment.filter_coeff", 4);
    float alpha = (coeff > 0) ? 1.0f / (coeff + 1) : 1.0f;

    // Датчик камери (обов'язковий)
    if (sensor_air_) {
        float temp = 0.0f;
        if (sensor_air_->read(temp)) {
            if (!ema_air_init_) { ema_air_ = temp; ema_air_init_ = true; }
            else { ema_air_ += (temp - ema_air_) * alpha; }
            // Округлення до 0.01°C — зменшує кількість version bumps
            air_temp_ = roundf(ema_air_ * 100.0f) / 100.0f;
            state_set("equipment.air_temp", air_temp_);
        }
        // is_healthy() враховує consecutive_errors (драйвер відстежує).
        // read() повертає true з кешованим значенням навіть коли датчик офлайн,
        // тому для статусу використовуємо is_healthy().
        state_set("equipment.sensor1_ok", sensor_air_->is_healthy());
    }

    // Датчик випарника (опціональний)
    if (sensor_evap_) {
        float temp = 0.0f;
        if (sensor_evap_->read(temp)) {
            if (!ema_evap_init_) { ema_evap_ = temp; ema_evap_init_ = true; }
            else { ema_evap_ += (temp - ema_evap_) * alpha; }
            evap_temp_ = roundf(ema_evap_ * 100.0f) / 100.0f;
            state_set("equipment.evap_temp", evap_temp_);
        }
        state_set("equipment.sensor2_ok", sensor_evap_->is_healthy());
    }

    // Датчик конденсатора (опціональний — DS18B20 або NTC)
    if (sensor_cond_) {
        float temp = 0.0f;
        if (sensor_cond_->read(temp)) {
            if (!ema_cond_init_) { ema_cond_ = temp; ema_cond_init_ = true; }
            else { ema_cond_ += (temp - ema_cond_) * alpha; }
            cond_temp_ = roundf(ema_cond_ * 100.0f) / 100.0f;
            state_set("equipment.cond_temp", cond_temp_);
        }
    }

    // Контакт дверей (опціональний — digital_input)
    if (door_sensor_) {
        float val = 0.0f;
        door_sensor_->read(val);
        state_set("equipment.door_open", val > 0.5f);
    }

    // Дискретний вхід нічного режиму (опціональний)
    if (night_sensor_) {
        float val = 0.0f;
        night_sensor_->read(val);
        state_set("equipment.night_input", val > 0.5f);
    }
}

// ═══════════════════════════════════════════════════════════════
// Читання requests з SharedState
// ═══════════════════════════════════════════════════════════════

void EquipmentModule::read_requests() {
    // Thermostat requests
    req_.therm_compressor = read_bool("thermostat.req.compressor");
    req_.therm_evap_fan   = read_bool("thermostat.req.evap_fan");
    req_.therm_cond_fan   = read_bool("thermostat.req.cond_fan");

    // Defrost requests
    req_.defrost_active    = read_bool("defrost.active");
    req_.def_compressor    = read_bool("defrost.req.compressor");
    req_.def_defrost_relay = read_bool("defrost.req.defrost_relay");
    req_.def_evap_fan      = read_bool("defrost.req.evap_fan");
    req_.def_cond_fan      = read_bool("defrost.req.cond_fan");

    // Protection
    req_.protection_lockout  = read_bool("protection.lockout");
    req_.compressor_blocked  = read_bool("protection.compressor_blocked");
}

// ═══════════════════════════════════════════════════════════════
// Арбітраж + інтерлоки
// ═══════════════════════════════════════════════════════════════

void EquipmentModule::apply_arbitration() {
    // Protection lockout = все вимкнено (найвищий пріоритет)
    if (req_.protection_lockout) {
        out_ = {};
        return;
    }

    // Defrost active = defrost requests мають пріоритет
    if (req_.defrost_active) {
        out_.compressor    = req_.def_compressor;
        out_.defrost_relay = req_.def_defrost_relay;
        out_.evap_fan      = req_.def_evap_fan;
        out_.cond_fan      = req_.def_cond_fan;
        // Логуємо тільки при вході в defrost (не кожен цикл)
        if (!prev_defrost_active_) {
            ESP_LOGI(TAG, "DEFROST arb START: comp=%d relay=%d efan=%d cfan=%d",
                     out_.compressor, out_.defrost_relay, out_.evap_fan, out_.cond_fan);
        }
    } else {
        // Логуємо при виході з defrost
        if (prev_defrost_active_) {
            ESP_LOGI(TAG, "DEFROST arb END — normal mode restored");
        }
        // Нормальний режим: thermostat requests
        out_.compressor    = req_.therm_compressor;
        out_.defrost_relay = false;   // Тільки defrost може ввімкнути
        out_.evap_fan      = req_.therm_evap_fan;
        out_.cond_fan      = req_.therm_cond_fan;
    }

    // === AUDIT-003: Compressor anti-short-cycle (output-level) ===
    // Захищає компресор незалежно від джерела запиту (thermostat/defrost).
    // Працює на фактичному стані реле, а не на запитах бізнес-модулів.
    // ВАЖЛИВО: виконується ДО інтерлоків, щоб інтерлоки мали фінальне слово.
    if (out_.compressor != comp_actual_) {
        if (out_.compressor) {
            // Запит на ввімкнення — перевіряємо min OFF time
            if (comp_since_ms_ < COMP_MIN_OFF_MS) {
                out_.compressor = false;
                ESP_LOGD(TAG, "Compressor ON blocked — min OFF (%lu/%lu ms)",
                         comp_since_ms_, COMP_MIN_OFF_MS);
            }
        } else {
            // Запит на вимкнення — перевіряємо min ON time
            if (comp_since_ms_ < COMP_MIN_ON_MS) {
                out_.compressor = true;  // Тримаємо ON
                ESP_LOGD(TAG, "Compressor OFF blocked — min ON (%lu/%lu ms)",
                         comp_since_ms_, COMP_MIN_ON_MS);
            }
        }
    }

    // === Protection compressor block (forced off при continuous run) ===
    // Тільки компресор OFF, вентилятори працюють за нормальним арбітражем.
    if (req_.compressor_blocked) {
        out_.compressor = false;
    }

    // === ІНТЕРЛОКИ (hardcoded, неможливо обійти) ===
    // Виконуються ОСТАННІМИ — мають найвищий пріоритет після protection lockout.

    // Електрична відтайка (тен) і компресор НІКОЛИ одночасно.
    // Гарячий газ: компресор потрібен — інтерлок НЕ застосовується.
    // Перевіряємо defrost.type щоб визначити чи реле = тен (type=1).
    if (out_.defrost_relay && out_.compressor) {
        int defrost_type = read_int("defrost.type", 0);
        if (defrost_type == 1) {
            // Електричний тен — компресор OFF
            out_.compressor = false;
            ESP_LOGW(TAG, "INTERLOCK: heater+compressor → compressor OFF");
        }
        // defrost_type == 2 (ГГ): обидва ON — це нормально
    }

    // Оновлюємо tracking для delta-логування
    prev_defrost_active_ = req_.defrost_active;
}

// ═══════════════════════════════════════════════════════════════
// Застосування outputs до relay
// ═══════════════════════════════════════════════════════════════

void EquipmentModule::apply_outputs() {
    if (compressor_)    compressor_->set(out_.compressor);
    if (defrost_relay_) defrost_relay_->set(out_.defrost_relay);
    if (evap_fan_)      evap_fan_->set(out_.evap_fan);
    if (cond_fan_)      cond_fan_->set(out_.cond_fan);
}

// ═══════════════════════════════════════════════════════════════
// Публікація фактичного стану актуаторів
// ═══════════════════════════════════════════════════════════════

void EquipmentModule::publish_state() {
    // AUDIT-002: публікуємо ФАКТИЧНИЙ стан реле (get_state()), а не бажаний (out_)
    bool comp_now = compressor_ ? compressor_->get_state() : false;

    // AUDIT-003: відстежуємо зміну фактичного стану компресора для таймера
    if (comp_now != comp_actual_) {
        comp_since_ms_ = 0;
        comp_actual_ = comp_now;
        ESP_LOGI(TAG, "Compressor → %s", comp_now ? "ON" : "OFF");
    }

    state_set("equipment.compressor",    comp_now);
    state_set("equipment.defrost_relay", defrost_relay_ ? defrost_relay_->get_state() : false);
    state_set("equipment.evap_fan",      evap_fan_      ? evap_fan_->get_state()      : false);
    state_set("equipment.cond_fan",      cond_fan_      ? cond_fan_->get_state()      : false);
}
