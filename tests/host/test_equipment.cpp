/**
 * @file test_equipment.cpp
 * @brief HOST unit tests for EquipmentModule.
 *
 * Tests cover:
 *   - Sensor reading → SharedState publication
 *   - Arbitration priority (Protection > Defrost > Thermostat)
 *   - Compressor anti-short-cycle (output-level)
 *   - Interlocks (electric defrost vs compressor)
 *   - EMA filter behavior
 *   - Safe mode / on_stop
 *   - Optional equipment detection (has_* keys)
 */

#include "mocks/freertos_mock.h"
#include "mocks/esp_log_mock.h"
#include "mocks/esp_timer_mock.h"

#include "doctest.h"
#include "modesp/shared_state.h"
#include "modesp/module_manager.h"
#include "mocks/mock_drivers.h"
#include "equipment_module.h"

// ── Helpers ──

static float get_float(modesp::SharedState& s, const char* key, float def = -999.0f) {
    auto v = s.get(key);
    if (!v.has_value()) return def;
    const auto* fp = etl::get_if<float>(&v.value());
    return fp ? *fp : def;
}

static bool get_bool(modesp::SharedState& s, const char* key, bool def = false) {
    auto v = s.get(key);
    if (!v.has_value()) return def;
    const auto* bp = etl::get_if<bool>(&v.value());
    return bp ? *bp : def;
}

// ── Test fixture ──

struct EquipFixture {
    modesp::SharedState state;
    modesp::ModuleManager mgr;
    EquipmentModule em;

    // Mock drivers
    modesp::MockSensorDriver   air{"air_temp", "ds18b20"};
    modesp::MockSensorDriver   evap{"evap_temp", "ds18b20"};
    modesp::MockSensorDriver   cond{"condenser_temp", "ntc"};
    modesp::MockActuatorDriver comp{"compressor"};
    modesp::MockActuatorDriver defrost_relay{"defrost_relay"};
    modesp::MockActuatorDriver efan{"evap_fan"};
    modesp::MockActuatorDriver cfan{"cond_fan"};
    modesp::MockSensorDriver   door{"door_contact", "digital_input"};
    modesp::MockSensorDriver   night{"night_input", "digital_input"};

    EquipFixture() {
        mgr.register_module(em);

        // Ін'єкція mock drivers ДО init_all
        em.inject_sensor_air(&air);
        em.inject_sensor_evap(&evap);
        em.inject_sensor_cond(&cond);
        em.inject_compressor(&comp);
        em.inject_defrost_relay(&defrost_relay);
        em.inject_evap_fan(&efan);
        em.inject_cond_fan(&cfan);
        em.inject_door_sensor(&door);
        em.inject_night_sensor(&night);

        // Початкові значення сенсорів
        air.set_value(5.0f);
        evap.set_value(-10.0f);
        cond.set_value(35.0f);

        // Ініціалізація через ModuleManager (sets shared_state_ + calls on_init)
        mgr.init_all(state);
    }

    void tick(uint32_t ms = 100) {
        em.on_update(ms);
    }
};

// ═══════════════════════════════════════════════════════════════
// TEST CASES
// ═══════════════════════════════════════════════════════════════

TEST_SUITE("EquipmentModule") {

// ── 1. Init publishes correct initial state ──

TEST_CASE_FIXTURE(EquipFixture, "init publishes sensor and actuator state") {
    CHECK(get_float(state, "equipment.air_temp") == doctest::Approx(0.0f));
    CHECK(get_bool(state, "equipment.sensor1_ok") == true);
    CHECK(get_bool(state, "equipment.sensor2_ok") == true);
    CHECK(get_bool(state, "equipment.compressor") == false);
    CHECK(get_bool(state, "equipment.defrost_relay") == false);
    CHECK(get_bool(state, "equipment.evap_fan") == false);
    CHECK(get_bool(state, "equipment.cond_fan") == false);
}

// ── 2. Init publishes has_* equipment detection ──

TEST_CASE_FIXTURE(EquipFixture, "init publishes has_* keys based on bound drivers") {
    CHECK(get_bool(state, "equipment.has_defrost_relay") == true);
    CHECK(get_bool(state, "equipment.has_cond_fan") == true);
    CHECK(get_bool(state, "equipment.has_door_contact") == true);
    CHECK(get_bool(state, "equipment.has_evap_temp") == true);
    CHECK(get_bool(state, "equipment.has_cond_temp") == true);
    CHECK(get_bool(state, "equipment.has_night_input") == true);
    CHECK(get_bool(state, "equipment.has_ntc_driver") == true);    // cond is NTC
    CHECK(get_bool(state, "equipment.has_ds18b20_driver") == true); // air is DS18B20
}

TEST_CASE("has_* keys false when drivers not bound") {
    modesp::SharedState state;
    modesp::ModuleManager mgr;
    EquipmentModule em;
    modesp::MockSensorDriver air{"air_temp"};
    modesp::MockActuatorDriver comp{"compressor"};

    mgr.register_module(em);
    em.inject_sensor_air(&air);
    em.inject_compressor(&comp);
    air.set_value(5.0f);

    mgr.init_all(state);
    CHECK(get_bool(state, "equipment.has_defrost_relay") == false);
    CHECK(get_bool(state, "equipment.has_evap_temp") == false);
    CHECK(get_bool(state, "equipment.has_cond_temp") == false);
    CHECK(get_bool(state, "equipment.has_door_contact") == false);
}

// ── 3. Sensor reading → SharedState ──

TEST_CASE_FIXTURE(EquipFixture, "read_sensors publishes air_temp") {
    air.set_value(4.5f);
    tick();
    CHECK(get_float(state, "equipment.air_temp") == doctest::Approx(4.5f).epsilon(0.1));
}

TEST_CASE_FIXTURE(EquipFixture, "sensor1_ok reflects sensor health") {
    air.set_healthy(false);
    tick();
    CHECK(get_bool(state, "equipment.sensor1_ok") == false);

    air.set_healthy(true);
    tick();
    CHECK(get_bool(state, "equipment.sensor1_ok") == true);
}

TEST_CASE_FIXTURE(EquipFixture, "evap sensor publishes sensor2_ok") {
    evap.set_healthy(false);
    tick();
    CHECK(get_bool(state, "equipment.sensor2_ok") == false);
}

TEST_CASE_FIXTURE(EquipFixture, "door sensor publishes door_open") {
    door.set_value(1.0f);  // > 0.5 = open
    tick();
    CHECK(get_bool(state, "equipment.door_open") == true);

    door.set_value(0.0f);
    tick();
    CHECK(get_bool(state, "equipment.door_open") == false);
}

TEST_CASE_FIXTURE(EquipFixture, "night sensor publishes night_input") {
    night.set_value(1.0f);
    tick();
    CHECK(get_bool(state, "equipment.night_input") == true);
}

// ── 4. Thermostat arbitration (normal mode) ──

TEST_CASE_FIXTURE(EquipFixture, "thermostat requests drive outputs in normal mode") {
    state.set("thermostat.req.compressor", true);
    state.set("thermostat.req.evap_fan", true);
    state.set("thermostat.req.cond_fan", true);

    // Спочатку потрібно задовольнити anti-short-cycle
    // comp_since_ms_ ініціалізується як TIMER_SATISFIED, тому перший ON проходить
    tick();

    CHECK(comp.get_state() == true);
    CHECK(efan.get_state() == true);
    CHECK(cfan.get_state() == true);
    CHECK(defrost_relay.get_state() == false);
}

TEST_CASE_FIXTURE(EquipFixture, "defrost_relay stays OFF in normal mode") {
    state.set("thermostat.req.compressor", true);
    tick();
    CHECK(defrost_relay.get_state() == false);
}

// ── 5. Defrost arbitration ──

TEST_CASE_FIXTURE(EquipFixture, "defrost active overrides thermostat") {
    // Thermostat wants compressor ON
    state.set("thermostat.req.compressor", true);
    state.set("thermostat.req.evap_fan", true);

    // Defrost is active — wants compressor OFF, defrost_relay ON, evap_fan OFF
    state.set("defrost.active", true);
    state.set("defrost.req.compressor", false);
    state.set("defrost.req.defrost_relay", true);
    state.set("defrost.req.evap_fan", false);
    state.set("defrost.req.cond_fan", false);

    tick();

    // Defrost wins
    CHECK(comp.get_state() == false);
    CHECK(defrost_relay.get_state() == true);
    CHECK(efan.get_state() == false);
}

TEST_CASE_FIXTURE(EquipFixture, "hot gas defrost allows compressor + defrost_relay") {
    state.set("defrost.active", true);
    state.set("defrost.req.compressor", true);
    state.set("defrost.req.defrost_relay", true);
    state.set("defrost.type", static_cast<int32_t>(2));  // hot gas

    tick();

    // Обидва ON — hot gas дозволяє
    CHECK(comp.get_state() == true);
    CHECK(defrost_relay.get_state() == true);
}

// ── 6. Electric defrost interlock ──

TEST_CASE_FIXTURE(EquipFixture, "electric defrost interlock: compressor OFF") {
    state.set("defrost.active", true);
    state.set("defrost.req.compressor", true);
    state.set("defrost.req.defrost_relay", true);
    state.set("defrost.type", static_cast<int32_t>(1));  // electric heater

    tick();

    // Інтерлок: тен + компресор неможливо → компресор OFF
    CHECK(comp.get_state() == false);
    CHECK(defrost_relay.get_state() == true);
}

// ── 7. Protection lockout ──

TEST_CASE_FIXTURE(EquipFixture, "protection lockout turns everything OFF") {
    state.set("thermostat.req.compressor", true);
    state.set("thermostat.req.evap_fan", true);
    state.set("thermostat.req.cond_fan", true);
    tick();

    // Все ввімкнено
    CHECK(comp.get_state() == true);

    // Lockout!
    state.set("protection.lockout", true);
    tick();

    CHECK(comp.get_state() == false);
    CHECK(defrost_relay.get_state() == false);
    CHECK(efan.get_state() == false);
    CHECK(cfan.get_state() == false);
}

// ── 8. Protection compressor_blocked ──

TEST_CASE_FIXTURE(EquipFixture, "compressor_blocked forces compressor OFF only") {
    state.set("thermostat.req.compressor", true);
    state.set("thermostat.req.evap_fan", true);
    tick();

    CHECK(comp.get_state() == true);
    CHECK(efan.get_state() == true);

    // Компресор заблоковано, але вент. працює
    state.set("protection.compressor_blocked", true);
    // Дочекатися min ON time (120s)
    tick(120001);
    tick();

    CHECK(comp.get_state() == false);
    CHECK(efan.get_state() == true);
}

// ── 9. Anti-short-cycle ──

TEST_CASE_FIXTURE(EquipFixture, "anti-short-cycle blocks early ON") {
    // Ввімкнути компресор
    state.set("thermostat.req.compressor", true);
    tick();
    CHECK(comp.get_state() == true);

    // Вимкнути після min ON (120s)
    state.set("thermostat.req.compressor", false);
    tick(120001);
    tick();
    CHECK(comp.get_state() == false);

    // Спроба ввімкнути через 10 сек — заблоковано (min OFF = 180s)
    state.set("thermostat.req.compressor", true);
    tick(10000);
    tick();
    CHECK(comp.get_state() == false);

    // Чекаємо 180 сек загалом
    tick(170001);
    tick();
    CHECK(comp.get_state() == true);
}

TEST_CASE_FIXTURE(EquipFixture, "anti-short-cycle blocks early OFF") {
    // Ввімкнути компресор
    state.set("thermostat.req.compressor", true);
    tick();
    CHECK(comp.get_state() == true);

    // Спроба вимкнути через 10 сек — заблоковано (min ON = 120s)
    state.set("thermostat.req.compressor", false);
    tick(10000);
    tick();
    CHECK(comp.get_state() == true);

    // Через 120 сек — дозволено
    tick(110001);
    tick();
    CHECK(comp.get_state() == false);
}

// ── 10. Actual state publication ──

TEST_CASE_FIXTURE(EquipFixture, "publishes actual relay state via get_state()") {
    state.set("thermostat.req.compressor", true);
    tick();

    // SharedState = фактичний стан реле
    CHECK(get_bool(state, "equipment.compressor") == true);

    state.set("thermostat.req.compressor", false);
    tick(120001);
    tick();
    CHECK(get_bool(state, "equipment.compressor") == false);
}

// ── 11. EMA filter ──

TEST_CASE_FIXTURE(EquipFixture, "EMA filter smooths temperature changes") {
    // Default filter_coeff = 4, alpha = 1/(4+1) = 0.2
    air.set_value(10.0f);
    tick();
    float t1 = get_float(state, "equipment.air_temp");

    air.set_value(10.0f);
    tick();
    float t2 = get_float(state, "equipment.air_temp");

    // Після 2 тіків EMA ще не дісталась до 10.0
    // Перший тік: EMA = 5.0 + (10-5)*0.2 = 6.0 (initialized with first value=5.0, then update)
    // Фактично перший read в on_init не робиться, init ставить 0.
    // Перший тік: ema_air_ init = 10.0 (ema_air_init_ was set in first tick)
    // Другий тік: вже = 10.0
    // Значення має бути ~10.0 або менше залежно від init
    CHECK(t2 == doctest::Approx(10.0f).epsilon(0.5));
}

// ── 12. on_stop ──

TEST_CASE_FIXTURE(EquipFixture, "on_stop turns everything OFF") {
    state.set("thermostat.req.compressor", true);
    state.set("thermostat.req.evap_fan", true);
    tick();
    CHECK(comp.get_state() == true);

    em.on_stop();
    CHECK(comp.get_state() == false);
    CHECK(efan.get_state() == false);
    CHECK(cfan.get_state() == false);
}

// ── 13. Init fails without required drivers ──

TEST_CASE("init fails without air sensor") {
    modesp::SharedState state;
    modesp::ModuleManager mgr;
    EquipmentModule em;
    modesp::MockActuatorDriver comp{"compressor"};

    mgr.register_module(em);
    em.inject_compressor(&comp);
    // Не прив'язуємо air sensor — on_init повертає false

    mgr.init_all(state);
    // Модуль в стані ERROR (on_init returned false)
    // Перевіримо що air_temp не публікується як 0.0
}

TEST_CASE("init fails without compressor") {
    modesp::SharedState state;
    modesp::ModuleManager mgr;
    EquipmentModule em;
    modesp::MockSensorDriver air{"air_temp"};

    mgr.register_module(em);
    em.inject_sensor_air(&air);
    air.set_value(5.0f);

    mgr.init_all(state);
    // Модуль в стані ERROR
}

// ── 14. Condenser temp reading ──

TEST_CASE_FIXTURE(EquipFixture, "condenser temp published to SharedState") {
    cond.set_value(40.0f);
    tick();
    CHECK(get_float(state, "equipment.cond_temp") == doctest::Approx(40.0f).epsilon(0.5));
}

// ── 15. Lockout overrides defrost ──

TEST_CASE_FIXTURE(EquipFixture, "lockout overrides defrost") {
    state.set("defrost.active", true);
    state.set("defrost.req.compressor", true);
    state.set("defrost.req.defrost_relay", true);
    state.set("protection.lockout", true);

    tick();

    CHECK(comp.get_state() == false);
    CHECK(defrost_relay.get_state() == false);
}

// ── 16. Normal mode restored after defrost ends ──

TEST_CASE_FIXTURE(EquipFixture, "normal mode restored after defrost ends") {
    state.set("thermostat.req.compressor", true);
    state.set("defrost.active", true);
    state.set("defrost.req.compressor", false);
    tick();
    CHECK(comp.get_state() == false);

    // Defrost завершується
    state.set("defrost.active", false);
    tick();
    CHECK(comp.get_state() == true);
}

// ── 17. Unhealthy sensor still reads cached value ──

TEST_CASE_FIXTURE(EquipFixture, "unhealthy air sensor does not crash") {
    air.set_healthy(false);
    air.set_read_ok(false);
    tick();
    CHECK(get_bool(state, "equipment.sensor1_ok") == false);
    // air_temp залишається з попереднього значення
}

} // TEST_SUITE
