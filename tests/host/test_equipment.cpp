/**
 * @file test_equipment.cpp
 * @brief HOST unit tests for EquipmentBase / EquipmentModule.
 *
 * Tests cover:
 *   - inject_sensor / inject_actuator role binding
 *   - on_init: has_* capability flags, driver type detection, initial keys
 *   - Sensor reading → SharedState (value + health key, digital inputs)
 *   - EMA filter (equipment.filter_coeff)
 *   - apply_arbitration product override (request → actuator)
 *   - ShortCycleGuard min ON / min OFF
 *   - SAFE_MODE message + on_stop — all outputs OFF
 *   - on_product_init / on_product_update hooks
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

// ── Product subclass — exercises EquipmentBase protected API ──

class TestEquipment : public EquipmentBase {
public:
    TestEquipment() : EquipmentBase("equipment", modesp::ModulePriority::CRITICAL) {}

    int  init_hooks   = 0;
    int  update_hooks = 0;
    bool use_guard    = false;

protected:
    void apply_arbitration(uint32_t dt_ms) override {
        bool req = read_bool("ctrl.req.output1", false);
        if (use_guard) req = guard_.apply(req, dt_ms);
        set_actuator("output1", req);
    }

    void on_product_init() override { init_hooks++; }
    void on_product_update(uint32_t dt_ms) override { (void)dt_ms; update_hooks++; }

private:
    ShortCycleGuard guard_{2000, 3000};  // min ON 2s, min OFF 3s
};

// ── Fixtures ──

/// Template EquipmentModule (pass-through arbitration) — sensor-side tests
struct EquipFixture {
    modesp::SharedState state;
    modesp::ModuleManager mgr;
    EquipmentModule em;

    modesp::MockSensorDriver   air{"air_temp", "ds18b20"};
    modesp::MockActuatorDriver act1{"actuator_1"};

    EquipFixture() {
        mgr.register_module(em);

        // Ін'єкція mock drivers ДО init_all
        em.inject_sensor("air_temp", &air);
        em.inject_actuator("actuator_1", &act1);
        air.set_value(5.0f);

        // init_all sets shared_state_ + calls on_init
        mgr.init_all(state);
    }

    void tick(uint32_t ms = 100) { em.on_update(ms); }
};

/// Product subclass fixture — arbitration / guard / safe-mode tests
struct ProductFixture {
    modesp::SharedState state;
    modesp::ModuleManager mgr;
    TestEquipment eq;

    modesp::MockSensorDriver   air{"air_temp", "ds18b20"};
    modesp::MockActuatorDriver out1{"output1"};

    ProductFixture() {
        mgr.register_module(eq);
        eq.inject_sensor("air_temp", &air);
        eq.inject_actuator("output1", &out1);
        air.set_value(5.0f);
        mgr.init_all(state);
    }

    void tick(uint32_t ms = 100) { eq.on_update(ms); }
};

// ═══════════════════════════════════════════════════════════════
// TEST CASES
// ═══════════════════════════════════════════════════════════════

TEST_SUITE("EquipmentBase") {

// ── 1. Init: has_* flags ──

TEST_CASE_FIXTURE(EquipFixture, "EquipmentBase: init publishes has_* flags for bound roles") {
    CHECK(get_bool(state, "equipment.has_air_temp") == true);
    CHECK(get_bool(state, "equipment.has_actuator_1") == true);
    CHECK(get_bool(state, "equipment.has_ds18b20_driver") == true);  // air is DS18B20
    CHECK(get_bool(state, "equipment.has_ntc_driver") == false);
}

TEST_CASE("EquipmentBase: has_* flags absent for unbound roles") {
    modesp::SharedState state;
    modesp::ModuleManager mgr;
    EquipmentModule em;
    modesp::MockSensorDriver air{"air_temp", "ntc"};

    mgr.register_module(em);
    em.inject_sensor("air_temp", &air);
    air.set_value(5.0f);
    mgr.init_all(state);

    CHECK(get_bool(state, "equipment.has_air_temp") == true);
    CHECK(get_bool(state, "equipment.has_actuator_1") == false);  // not injected
    CHECK(get_bool(state, "equipment.has_ntc_driver") == true);
    CHECK(get_bool(state, "equipment.has_ds18b20_driver") == false);
}

// ── 2. Init: initial state keys ──

TEST_CASE_FIXTURE(EquipFixture, "EquipmentBase: init publishes initial sensor/actuator keys") {
    CHECK(get_float(state, "equipment.air_temp") == doctest::Approx(0.0f));
    CHECK(get_bool(state, "equipment.actuator_1") == false);
}

// ── 3. Sensor reading → SharedState ──

TEST_CASE_FIXTURE(EquipFixture, "EquipmentBase: sensor value published on update") {
    air.set_value(4.5f);
    tick();
    // Перший read ініціалізує EMA значенням сенсора
    CHECK(get_float(state, "equipment.air_temp") == doctest::Approx(4.5f).epsilon(0.01));
    CHECK(get_bool(state, "equipment.air_temp_ok") == true);
}

TEST_CASE_FIXTURE(EquipFixture, "EquipmentBase: health key follows is_healthy") {
    air.set_healthy(false);
    tick();
    CHECK(get_bool(state, "equipment.air_temp_ok") == false);

    air.set_healthy(true);
    tick();
    CHECK(get_bool(state, "equipment.air_temp_ok") == true);
}

TEST_CASE_FIXTURE(EquipFixture, "EquipmentBase: unhealthy sensor keeps last value") {
    air.set_value(5.0f);
    tick();
    CHECK(get_float(state, "equipment.air_temp") == doctest::Approx(5.0f));

    // read() повертає false → публікується останнє відоме значення
    air.set_value(50.0f);
    air.set_healthy(false);
    tick();
    CHECK(get_float(state, "equipment.air_temp") == doctest::Approx(5.0f));
}

// ── 4. EMA filter ──

TEST_CASE_FIXTURE(EquipFixture, "EquipmentBase: EMA filter smooths with default filter_coeff") {
    // Default filter_coeff = 4 → alpha = 1/(4+1) = 0.2
    air.set_value(5.0f);
    tick();
    CHECK(get_float(state, "equipment.air_temp") == doctest::Approx(5.0f));

    air.set_value(10.0f);
    tick();
    // EMA = 5.0 + (10.0 - 5.0) * 0.2 = 6.0
    CHECK(get_float(state, "equipment.air_temp") == doctest::Approx(6.0f).epsilon(0.01));

    tick();
    // EMA = 6.0 + (10.0 - 6.0) * 0.2 = 6.8
    CHECK(get_float(state, "equipment.air_temp") == doctest::Approx(6.8f).epsilon(0.01));
}

TEST_CASE_FIXTURE(EquipFixture, "EquipmentBase: filter_coeff=0 disables smoothing") {
    air.set_value(5.0f);
    tick();

    state.set("equipment.filter_coeff", static_cast<int32_t>(0));
    air.set_value(10.0f);
    tick();
    CHECK(get_float(state, "equipment.air_temp") == doctest::Approx(10.0f));
}

// ── 5. Digital input ──

TEST_CASE("EquipmentBase: digital input published as bool") {
    modesp::SharedState state;
    modesp::ModuleManager mgr;
    EquipmentModule em;
    modesp::MockSensorDriver air{"air_temp", "ds18b20"};
    modesp::MockSensorDriver door{"door_contact", "digital_input"};

    mgr.register_module(em);
    em.inject_sensor("air_temp", &air);
    em.inject_sensor("door_contact", &door);
    air.set_value(5.0f);
    mgr.init_all(state);

    door.set_value(1.0f);  // > 0.5 = open
    em.on_update(100);
    CHECK(get_bool(state, "equipment.door_contact") == true);

    door.set_value(0.0f);
    em.on_update(100);
    CHECK(get_bool(state, "equipment.door_contact") == false);
}

// ── 6. Arbitration (product override) ──

TEST_CASE_FIXTURE(ProductFixture, "EquipmentBase: arbitration drives actuator from SharedState request") {
    state.set("ctrl.req.output1", true);
    tick();
    CHECK(out1.get_state() == true);

    state.set("ctrl.req.output1", false);
    tick();
    CHECK(out1.get_state() == false);
}

TEST_CASE_FIXTURE(ProductFixture, "EquipmentBase: actual actuator state published to SharedState") {
    state.set("ctrl.req.output1", true);
    tick();
    CHECK(get_bool(state, "equipment.output1") == true);

    state.set("ctrl.req.output1", false);
    tick();
    CHECK(get_bool(state, "equipment.output1") == false);
}

// ── 7. ShortCycleGuard ──

TEST_CASE_FIXTURE(ProductFixture, "EquipmentBase: ShortCycleGuard blocks early OFF") {
    eq.use_guard = true;

    // since_change стартує з TIMER_SATISFIED — перший ON проходить
    state.set("ctrl.req.output1", true);
    tick();
    CHECK(out1.get_state() == true);

    // Спроба вимкнути через 100 мс — заблоковано (min ON = 2s)
    state.set("ctrl.req.output1", false);
    tick();
    CHECK(out1.get_state() == true);

    // Через 2 с загалом — дозволено
    tick(2000);
    CHECK(out1.get_state() == false);
}

TEST_CASE_FIXTURE(ProductFixture, "EquipmentBase: ShortCycleGuard blocks early ON") {
    eq.use_guard = true;

    // ON → чекаємо min ON → OFF
    state.set("ctrl.req.output1", true);
    tick();
    state.set("ctrl.req.output1", false);
    tick(2100);
    CHECK(out1.get_state() == false);

    // Спроба ввімкнути через 100 мс — заблоковано (min OFF = 3s)
    state.set("ctrl.req.output1", true);
    tick();
    CHECK(out1.get_state() == false);

    // Через 3 с загалом — дозволено
    tick(3000);
    CHECK(out1.get_state() == true);
}

// ── 8. SAFE_MODE message ──

TEST_CASE_FIXTURE(ProductFixture, "EquipmentBase: SAFE_MODE message forces outputs OFF") {
    state.set("ctrl.req.output1", true);
    tick();
    CHECK(out1.get_state() == true);

    etl::message<modesp::msg_id::SYSTEM_SAFE_MODE> safe_mode;
    eq.on_message(safe_mode);

    CHECK(out1.get_state() == false);
    CHECK(get_bool(state, "equipment.output1") == false);
}

// ── 9. on_stop ──

TEST_CASE_FIXTURE(ProductFixture, "EquipmentBase: on_stop turns outputs OFF") {
    state.set("ctrl.req.output1", true);
    tick();
    CHECK(out1.get_state() == true);

    eq.on_stop();
    CHECK(out1.get_state() == false);
}

// ── 10. Product hooks ──

TEST_CASE_FIXTURE(ProductFixture, "EquipmentBase: product hooks called") {
    CHECK(eq.init_hooks == 1);  // on_product_init — один раз в on_init

    int before = eq.update_hooks;
    tick();
    tick();
    CHECK(eq.update_hooks == before + 2);
}

} // TEST_SUITE
