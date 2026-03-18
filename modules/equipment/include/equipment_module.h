/**
 * @file equipment_module.h
 * @brief Equipment Manager — single owner of all HAL drivers
 *
 * Architecture:
 *   EM is the ONLY module that accesses ISensorDriver / IActuatorDriver.
 *   Business modules (Thermostat, Defrost, Protection) communicate
 *   through SharedState only:
 *     - EM publishes sensor readings: equipment.air_temp, equipment.sensor1_ok
 *     - EM publishes actuator states: equipment.compressor, equipment.defrost_relay
 *     - Business modules publish requests: thermostat.req.compressor, defrost.req.defrost_relay
 *     - EM reads requests, applies arbitration, drives outputs
 *
 * Arbitration priority: Protection LOCKOUT > Defrost active > Thermostat
 *
 * Interlocks (hardcoded, cannot be bypassed):
 *   - Electric defrost relay and compressor NEVER simultaneously
 *   - Protection lockout = everything OFF
 *
 * SharedState keys published:
 *   equipment.air_temp       — float (°C), main chamber sensor
 *   equipment.evap_temp      — float (°C), evaporator sensor (optional)
 *   equipment.sensor1_ok     — bool, air sensor healthy
 *   equipment.sensor2_ok     — bool, evap sensor healthy
 *   equipment.compressor     — bool, actual relay state
 *   equipment.defrost_relay  — bool, actual relay state
 *   equipment.evap_fan       — bool, actual relay state
 *   equipment.cond_fan       — bool, actual relay state
 *
 * SharedState keys read (requests from other modules):
 *   thermostat.req.compressor    — bool
 *   thermostat.req.evap_fan      — bool
 *   thermostat.req.cond_fan      — bool
 *   defrost.active               — bool
 *   defrost.req.compressor       — bool
 *   defrost.req.defrost_relay    — bool
 *   defrost.req.evap_fan         — bool
 *   defrost.req.cond_fan         — bool
 *   protection.lockout           — bool
 */

#pragma once

#include "modesp/base_module.h"
#include "modesp/hal/driver_interfaces.h"

namespace modesp {
class DriverManager;
}

class EquipmentModule : public modesp::BaseModule {
public:
    EquipmentModule();

    /// Єдиний модуль з bind_drivers — володіє всіма drivers
    void bind_drivers(modesp::DriverManager& dm);

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_message(const etl::imessage& msg) override;
    void on_stop() override;

private:
    // === Сенсори ===
    modesp::ISensorDriver* sensor_air_  = nullptr;  // Обов'язковий
    modesp::ISensorDriver* sensor_evap_ = nullptr;  // Опціональний
    modesp::ISensorDriver* sensor_cond_ = nullptr;  // Опціональний (DS18B20 або NTC)

    // === Актуатори ===
    modesp::IActuatorDriver* compressor_    = nullptr;  // Обов'язковий
    modesp::IActuatorDriver* defrost_relay_ = nullptr;  // Опціональний (тен або клапан ГГ)
    modesp::IActuatorDriver* evap_fan_      = nullptr;  // Опціональний
    modesp::IActuatorDriver* cond_fan_      = nullptr;  // Опціональний

    // === Дискретні входи ===
    modesp::ISensorDriver* door_sensor_  = nullptr;  // Опціональний
    modesp::ISensorDriver* night_sensor_ = nullptr;  // Опціональний

    // === Внутрішня логіка ===
    void read_sensors();
    void read_requests();
    void apply_arbitration();
    void apply_outputs();
    void publish_state();

    // Кешовані значення сенсорів
    float air_temp_  = 0.0f;
    float evap_temp_ = 0.0f;
    float cond_temp_ = 0.0f;

    // EMA фільтр температури (цифровий фільтр, аналог FiL у Dixell)
    float ema_air_  = 0.0f;
    float ema_evap_ = 0.0f;
    float ema_cond_ = 0.0f;
    bool  ema_air_init_  = false;
    bool  ema_evap_init_ = false;
    bool  ema_cond_init_ = false;

    // Requests від бізнес-модулів (читаються кожен цикл з SharedState)
    struct Requests {
        // Thermostat
        bool therm_compressor = false;
        bool therm_evap_fan   = false;
        bool therm_cond_fan   = false;

        // Defrost
        bool defrost_active    = false;
        bool def_compressor    = false;
        bool def_defrost_relay = false;
        bool def_evap_fan      = false;
        bool def_cond_fan      = false;

        // Protection
        bool protection_lockout    = false;
        bool compressor_blocked    = false;
    } req_;

    // Фінальний вихід (після арбітражу)
    struct Outputs {
        bool compressor    = false;
        bool defrost_relay = false;
        bool evap_fan      = false;
        bool cond_fan      = false;
    } out_;

    // Defrost transition tracking (логуємо тільки при зміні стану)
    bool prev_defrost_active_ = false;

    // AUDIT-003: Compressor anti-short-cycle на рівні виходу (output-level).
    // Захищає компресор незалежно від джерела запиту (thermostat/defrost).
    // Доповнює, а не замінює таймери thermostat (ті працюють для state machine логіки).
    bool  comp_actual_        = false;   // Фактичний стан компресора
    uint32_t comp_since_ms_   = modesp::TIMER_SATISFIED;  // Час з останнього перемикання
    static constexpr uint32_t COMP_MIN_OFF_MS = 180000;  // 3 хв min OFF
    static constexpr uint32_t COMP_MIN_ON_MS  = 120000;  // 2 хв min ON

#ifdef HOST_BUILD
public:
    // Test-only: прямий доступ до driver pointers для ін'єкції mock drivers
    void inject_sensor_air(modesp::ISensorDriver* s)     { sensor_air_ = s; }
    void inject_sensor_evap(modesp::ISensorDriver* s)    { sensor_evap_ = s; }
    void inject_sensor_cond(modesp::ISensorDriver* s)    { sensor_cond_ = s; }
    void inject_compressor(modesp::IActuatorDriver* a)   { compressor_ = a; }
    void inject_defrost_relay(modesp::IActuatorDriver* a){ defrost_relay_ = a; }
    void inject_evap_fan(modesp::IActuatorDriver* a)     { evap_fan_ = a; }
    void inject_cond_fan(modesp::IActuatorDriver* a)     { cond_fan_ = a; }
    void inject_door_sensor(modesp::ISensorDriver* s)    { door_sensor_ = s; }
    void inject_night_sensor(modesp::ISensorDriver* s)   { night_sensor_ = s; }
#endif
};
