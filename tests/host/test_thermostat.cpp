/**
 * @file test_thermostat.cpp
 * @brief HOST unit tests for ThermostatModule.
 *
 * Framework: doctest (single header, DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN in test_main.cpp)
 *
 * Key implementation details that drive test design:
 *
 *   1. comp_off_time_ms_ initialises to TIMER_SATISFIED (999999) so the
 *      min_off constraint is already met the very first time IDLE evaluates.
 *
 *   2. comp_on/off timers accumulate from read_bool("equipment.compressor")
 *      (the ACTUAL relay state written by EquipmentModule), NOT from the
 *      internal compressor_on_ request flag.  In host tests we control this
 *      key manually to simulate Equipment applying the request.
 *
 *   3. sync_settings() is called every on_update() tick, so writing a setting
 *      key into SharedState before the tick is sufficient.
 *
 *   4. state machine strings are lowercase: "startup", "idle", "cooling",
 *      "safety_run".
 *
 *   5. Features (from generated/features_config.h):
 *        thermostat.fan_control      = true   -> evap fan logic executes
 *        thermostat.condenser_fan    = true   -> cond fan logic executes
 *        thermostat.fan_temp_control = true   -> mode-2 evap fan works
 *        thermostat.night_setback    = true
 *        thermostat.night_di         = false  -> mode-2 DI night falls back
 *
 *   6. Default settings (from sync_settings() fallback arguments):
 *        setpoint      = 4.0  C
 *        differential  = 2.0  C   ->  ON threshold = 6.0, OFF = 4.0
 *        min_off_time  = 3    min  = 180 000 ms
 *        min_on_time   = 2    min  = 120 000 ms
 *        startup_delay = 1    min  =  60 000 ms
 *        evap_fan_mode = 1         (follows compressor)
 *        cond_fan_delay= 30   s   =  30 000 ms
 *        safety_run_on = 20   min = 1 200 000 ms
 *        safety_run_off= 10   min =   600 000 ms
 */

// -- HOST BUILD: mock ESP-IDF before anything else --
#include "mocks/freertos_mock.h"
#include "mocks/esp_log_mock.h"
#include "mocks/esp_timer_mock.h"

#include "doctest.h"
#include "modesp/shared_state.h"
#include "modesp/module_manager.h"
#include "thermostat_module.h"

// -- Helper: extract typed value from SharedState ------------------------------

static float get_float(modesp::SharedState& state, const char* key, float def = -999.0f) {
    auto v = state.get(key);
    if (!v.has_value()) return def;
    const auto* fp = etl::get_if<float>(&v.value());
    return fp ? *fp : def;
}

static bool get_bool(modesp::SharedState& state, const char* key, bool def = false) {
    auto v = state.get(key);
    if (!v.has_value()) return def;
    const auto* bp = etl::get_if<bool>(&v.value());
    return bp ? *bp : def;
}

static int32_t get_int(modesp::SharedState& state, const char* key, int32_t def = -1) {
    auto v = state.get(key);
    if (!v.has_value()) return def;
    const auto* ip = etl::get_if<int32_t>(&v.value());
    return ip ? *ip : def;
}

static modesp::StringValue get_str(modesp::SharedState& state, const char* key) {
    auto v = state.get(key);
    if (!v.has_value()) return modesp::StringValue("");
    const auto* sp = etl::get_if<modesp::StringValue>(&v.value());
    return sp ? *sp : modesp::StringValue("");
}

// -- Helper: standard test environment setup ----------------------------------
static void setup_normal_inputs(modesp::SharedState& state,
                                float air_temp      = 3.0f,
                                bool  sensor1_ok    = true,
                                bool  defrost       = false,
                                bool  lockout       = false) {
    state.set("equipment.air_temp",    air_temp);
    state.set("equipment.sensor1_ok",  sensor1_ok);
    state.set("equipment.sensor2_ok",  false);
    state.set("equipment.compressor",  false);  // actual relay state
    state.set("defrost.active",        defrost);
    state.set("protection.lockout",    lockout);
}

// -- Helper: skip startup delay (60 000 ms default) ---------------------------
// First tick transitions STARTUP → IDLE. Second tick evaluates IDLE (may go to
// COOLING or SAFETY_RUN depending on inputs set before calling skip_startup).
static void skip_startup(ThermostatModule& therm) {
    therm.on_update(60001u);  // 60 001 ms > 60 000 ms → STARTUP exits to IDLE
    therm.on_update(1u);      // evaluate IDLE (transition to COOLING / SAFETY_RUN if needed)
}

// -----------------------------------------------------------------------------
// TEST 1: STARTUP state immediately after init_all()
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: STARTUP state on init [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    auto state_val = get_str(state, "thermostat.state");
    CHECK_MESSAGE(state_val == "startup",
                  "Expected 'startup' immediately after init_all(), got: ", state_val.c_str());

    CHECK(get_bool(state, "thermostat.req.compressor") == false);
    CHECK(get_bool(state, "thermostat.req.evap_fan")   == false);
    CHECK(get_bool(state, "thermostat.req.cond_fan")   == false);
}

// -----------------------------------------------------------------------------
// TEST 2: STARTUP -> IDLE after startup delay elapses
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: STARTUP to IDLE after startup delay [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    setup_normal_inputs(state, 3.0f, true, false, false);

    SUBCASE("still STARTUP before delay elapses") {
        therm.on_update(30000u);
        auto s = get_str(state, "thermostat.state");
        CHECK_MESSAGE(s == "startup", "Should still be 'startup' at 30s, got: ", s.c_str());
        CHECK(get_bool(state, "thermostat.req.compressor") == false);
    }

    SUBCASE("transitions to IDLE after delay") {
        therm.on_update(60001u);
        auto s = get_str(state, "thermostat.state");
        CHECK_MESSAGE(s == "idle", "Expected 'idle' after startup delay, got: ", s.c_str());
        CHECK(get_bool(state, "thermostat.req.compressor") == false);
    }
}

// -----------------------------------------------------------------------------
// TEST 3: IDLE -> COOLING when T >= SP + diff (and min_off satisfied)
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: IDLE to COOLING when T >= SP + diff [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    setup_normal_inputs(state, 6.5f, true, false, false);
    skip_startup(therm);

    auto s = get_str(state, "thermostat.state");
    CHECK_MESSAGE(s == "cooling", "Expected 'cooling' at T=6.5 >= SP+diff=6.0, got: ", s.c_str());
    CHECK_MESSAGE(get_bool(state, "thermostat.req.compressor") == true,
                  "Compressor request should be true in COOLING");
}

// -----------------------------------------------------------------------------
// TEST 4: stays IDLE when T < SP + diff
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: stays IDLE when T < SP + diff [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    setup_normal_inputs(state, 5.9f, true, false, false);
    skip_startup(therm);

    auto s = get_str(state, "thermostat.state");
    CHECK_MESSAGE(s == "idle", "Expected 'idle' at T=5.9 (below threshold 6.0), got: ", s.c_str());
    CHECK(get_bool(state, "thermostat.req.compressor") == false);
}

// -----------------------------------------------------------------------------
// TEST 5: COOLING -> IDLE when T <= SP and min_on_time elapsed
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: COOLING to IDLE when T <= SP and min_on_time elapsed [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    setup_normal_inputs(state, 6.5f, true, false, false);
    skip_startup(therm);

    auto s = get_str(state, "thermostat.state");
    REQUIRE_MESSAGE(s == "cooling", "Pre-condition: must be in COOLING, got: ", s.c_str());

    state.set("equipment.compressor", true);
    state.set("equipment.air_temp", 3.0f);
    therm.on_update(120001u);

    s = get_str(state, "thermostat.state");
    CHECK_MESSAGE(s == "idle", "Expected 'idle' after T<=SP and min_on elapsed, got: ", s.c_str());
    CHECK(get_bool(state, "thermostat.req.compressor") == false);
}

// -----------------------------------------------------------------------------
// TEST 6: min_on_time prevents early shutdown while in COOLING
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: min ON time prevents early COOLING shutdown [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    setup_normal_inputs(state, 6.5f, true, false, false);
    skip_startup(therm);

    auto s = get_str(state, "thermostat.state");
    REQUIRE_MESSAGE(s == "cooling", "Pre-condition: must be in COOLING");

    state.set("equipment.compressor", true);
    state.set("equipment.air_temp", 3.0f);
    therm.on_update(60000u);

    s = get_str(state, "thermostat.state");
    CHECK_MESSAGE(s == "cooling",
                  "Should still be 'cooling' -- min_on_time not yet elapsed, got: ", s.c_str());
    CHECK_MESSAGE(get_bool(state, "thermostat.req.compressor") == true,
                  "Compressor request must stay true while min_on not elapsed");
}

// -----------------------------------------------------------------------------
// TEST 7: min_off_time prevents rapid restart from IDLE
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: min OFF time prevents rapid compressor restart [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    setup_normal_inputs(state, 6.5f, true, false, false);
    skip_startup(therm);
    REQUIRE(get_str(state, "thermostat.state") == "cooling");

    state.set("equipment.compressor", true);
    state.set("equipment.air_temp", 3.0f);
    therm.on_update(120001u);
    REQUIRE(get_str(state, "thermostat.state") == "idle");

    state.set("equipment.compressor", false);
    state.set("equipment.air_temp", 6.5f);
    therm.on_update(60000u);

    auto s = get_str(state, "thermostat.state");
    CHECK_MESSAGE(s == "idle",
                  "Should stay 'idle' -- min_off_time not elapsed, got: ", s.c_str());
    CHECK_MESSAGE(get_bool(state, "thermostat.req.compressor") == false,
                  "Compressor request must be false while min_off not elapsed");

    therm.on_update(120001u);

    s = get_str(state, "thermostat.state");
    CHECK_MESSAGE(s == "cooling",
                  "Should transition to 'cooling' after min_off_time elapsed, got: ", s.c_str());
}

// -----------------------------------------------------------------------------
// TEST 8: defrost.active pauses thermostat (resets requests)
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: defrost.active pauses thermostat [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    setup_normal_inputs(state, 6.5f, true, false, false);
    skip_startup(therm);
    REQUIRE(get_str(state, "thermostat.state") == "cooling");
    REQUIRE(get_bool(state, "thermostat.req.compressor") == true);

    state.set("defrost.active", true);
    state.set("equipment.air_temp", 6.5f);
    therm.on_update(1000u);

    CHECK_MESSAGE(get_bool(state, "thermostat.req.compressor") == false,
                  "Compressor request must be false during defrost pause");
    CHECK_MESSAGE(get_bool(state, "thermostat.req.evap_fan") == false,
                  "Evap fan request must be false during defrost pause");
    CHECK_MESSAGE(get_bool(state, "thermostat.req.cond_fan") == false,
                  "Cond fan request must be false during defrost pause");

    SUBCASE("thermostat resumes IDLE after defrost ends") {
        state.set("defrost.active", false);
        state.set("equipment.air_temp", 3.0f);
        therm.on_update(1000u);

        auto s = get_str(state, "thermostat.state");
        CHECK_MESSAGE(s == "idle",
                      "Should return to 'idle' after defrost ends, got: ", s.c_str());
    }
}

// -----------------------------------------------------------------------------
// TEST 9: sensor1_ok=false triggers SAFETY_RUN
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: sensor1_ok=false triggers SAFETY_RUN [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    SUBCASE("SAFETY_RUN entered from IDLE when sensor fails") {
        setup_normal_inputs(state, 3.0f, true, false, false);
        skip_startup(therm);
        REQUIRE(get_str(state, "thermostat.state") == "idle");

        state.set("equipment.sensor1_ok", false);
        therm.on_update(100u);

        auto s = get_str(state, "thermostat.state");
        CHECK_MESSAGE(s == "safety_run",
                      "Expected 'safety_run' when sensor1 fails from IDLE, got: ", s.c_str());
        CHECK_MESSAGE(get_bool(state, "thermostat.req.compressor") == true,
                      "SAFETY_RUN starts in ON phase -> compressor request = true");
    }

    SUBCASE("SAFETY_RUN entered from COOLING when sensor fails") {
        setup_normal_inputs(state, 6.5f, true, false, false);
        skip_startup(therm);
        REQUIRE(get_str(state, "thermostat.state") == "cooling");

        state.set("equipment.sensor1_ok", false);
        therm.on_update(100u);

        auto s = get_str(state, "thermostat.state");
        CHECK_MESSAGE(s == "safety_run",
                      "Expected 'safety_run' when sensor1 fails from COOLING, got: ", s.c_str());
    }

    SUBCASE("exits SAFETY_RUN when sensor restores") {
        setup_normal_inputs(state, 3.0f, true, false, false);
        skip_startup(therm);
        state.set("equipment.sensor1_ok", false);
        therm.on_update(100u);
        REQUIRE(get_str(state, "thermostat.state") == "safety_run");

        state.set("equipment.sensor1_ok", true);
        state.set("equipment.air_temp", 3.0f);
        therm.on_update(100u);

        auto s = get_str(state, "thermostat.state");
        CHECK_MESSAGE(s == "idle",
                      "Expected 'idle' after sensor restored from SAFETY_RUN, got: ", s.c_str());
    }
}

// -----------------------------------------------------------------------------
// TEST 10: night_mode=3 (manual) increases effective setpoint
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: night_mode=3 manual increases effective setpoint [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    state.set("thermostat.setpoint",      4.0f);
    state.set("thermostat.differential",  2.0f);
    state.set("thermostat.night_setback", 3.0f);
    state.set("thermostat.night_mode",    static_cast<int32_t>(3));
    state.set("thermostat.night_active",  true);

    setup_normal_inputs(state, 3.0f, true, false, false);
    skip_startup(therm);

    float eff_sp = get_float(state, "thermostat.effective_setpoint");
    CHECK_MESSAGE(eff_sp == doctest::Approx(7.0f).epsilon(0.01f),
                  "Effective setpoint should be 7.0 (4+3 night_setback)");

    SUBCASE("T=6.5 stays IDLE (below night ON threshold=9.0)") {
        state.set("equipment.air_temp", 6.5f);
        therm.on_update(100u);
        auto s = get_str(state, "thermostat.state");
        CHECK_MESSAGE(s == "idle",
                      "T=6.5 < night ON threshold=9.0 should stay IDLE, got: ", s.c_str());
        CHECK(get_bool(state, "thermostat.req.compressor") == false);
    }

    SUBCASE("T=9.5 enters COOLING (above night ON threshold=9.0)") {
        state.set("equipment.air_temp", 9.5f);
        therm.on_update(100u);
        auto s = get_str(state, "thermostat.state");
        CHECK_MESSAGE(s == "cooling",
                      "T=9.5 >= night ON threshold=9.0 should enter COOLING, got: ", s.c_str());
        CHECK(get_bool(state, "thermostat.req.compressor") == true);
    }

    SUBCASE("night_active=false restores original ON threshold=6.0") {
        state.set("thermostat.night_active", false);
        state.set("equipment.air_temp", 6.5f);
        therm.on_update(100u);

        float eff = get_float(state, "thermostat.effective_setpoint");
        CHECK_MESSAGE(eff == doctest::Approx(4.0f).epsilon(0.01f),
                      "With night off, effective setpoint should revert to 4.0");

        auto s = get_str(state, "thermostat.state");
        CHECK_MESSAGE(s == "cooling",
                      "T=6.5 >= threshold=6.0 with night off should enter COOLING, got: ", s.c_str());
    }
}

// -----------------------------------------------------------------------------
// TEST 11: evap_fan_mode=0 -- evap fan always ON
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: evap_fan_mode=0 always on [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    state.set("thermostat.evap_fan_mode", static_cast<int32_t>(0));
    setup_normal_inputs(state, 3.0f, true, false, false);
    skip_startup(therm);

    auto s = get_str(state, "thermostat.state");
    REQUIRE_MESSAGE(s == "idle", "Pre-condition: should be IDLE");
    CHECK_MESSAGE(get_bool(state, "thermostat.req.compressor") == false,
                  "Compressor should be off (T below threshold)");
    CHECK_MESSAGE(get_bool(state, "thermostat.req.evap_fan") == true,
                  "Evap fan mode=0 must be always ON regardless of compressor state");

    SUBCASE("evap fan stays on even after additional ticks") {
        therm.on_update(1000u);
        CHECK(get_bool(state, "thermostat.req.evap_fan") == true);
    }
}

// -----------------------------------------------------------------------------
// TEST 12: evap_fan_mode=1 -- evap fan follows compressor
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: evap_fan_mode=1 follows compressor [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    state.set("thermostat.evap_fan_mode", static_cast<int32_t>(1));
    setup_normal_inputs(state, 3.0f, true, false, false);
    skip_startup(therm);

    SUBCASE("evap fan OFF when compressor is OFF") {
        auto s = get_str(state, "thermostat.state");
        REQUIRE_MESSAGE(s == "idle", "Pre-condition: IDLE");
        CHECK_MESSAGE(get_bool(state, "thermostat.req.compressor") == false,
                      "Compressor should be off (T below threshold)");
        CHECK_MESSAGE(get_bool(state, "thermostat.req.evap_fan") == false,
                      "Evap fan mode=1: fan should be OFF when compressor is OFF");
    }

    SUBCASE("evap fan ON when compressor turns ON") {
        state.set("equipment.air_temp", 6.5f);
        therm.on_update(100u);

        auto s = get_str(state, "thermostat.state");
        CHECK_MESSAGE(s == "cooling", "Expected COOLING at T=6.5 >= threshold=6.0, got: ", s.c_str());
        CHECK_MESSAGE(get_bool(state, "thermostat.req.compressor") == true,
                      "Compressor should be ON in COOLING");
        CHECK_MESSAGE(get_bool(state, "thermostat.req.evap_fan") == true,
                      "Evap fan mode=1: fan should be ON when compressor is ON");
    }
}

// -----------------------------------------------------------------------------
// TEST 13: protection.lockout stops all requests
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: protection.lockout clears all requests [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    setup_normal_inputs(state, 6.5f, true, false, false);
    skip_startup(therm);
    REQUIRE(get_str(state, "thermostat.state") == "cooling");
    REQUIRE(get_bool(state, "thermostat.req.compressor") == true);

    state.set("protection.lockout", true);
    state.set("equipment.air_temp", 6.5f);
    therm.on_update(1000u);

    CHECK_MESSAGE(get_bool(state, "thermostat.req.compressor") == false,
                  "Lockout must clear compressor request");
    CHECK_MESSAGE(get_bool(state, "thermostat.req.evap_fan") == false,
                  "Lockout must clear evap fan request");
    CHECK_MESSAGE(get_bool(state, "thermostat.req.cond_fan") == false,
                  "Lockout must clear cond fan request");
}

// -----------------------------------------------------------------------------
// TEST 14: display_temp during defrost modes
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: display_temp during defrost [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    setup_normal_inputs(state, 5.5f, true, false, false);
    skip_startup(therm);

    SUBCASE("display_defrost=0 shows real temp during defrost") {
        state.set("thermostat.display_defrost", static_cast<int32_t>(0));
        state.set("equipment.air_temp", 5.5f);
        therm.on_update(100u);

        state.set("defrost.active", true);
        state.set("equipment.air_temp", 8.0f);
        therm.on_update(100u);

        float disp = get_float(state, "thermostat.display_temp");
        CHECK_MESSAGE(disp == doctest::Approx(8.0f).epsilon(0.01f),
                      "display_defrost=0: should show real current temp during defrost");
    }

    SUBCASE("display_defrost=1 shows frozen temp during defrost") {
        state.set("thermostat.display_defrost", static_cast<int32_t>(1));
        // Defrost starts while air_temp=5.5 → frozen_temp is captured as 5.5
        state.set("equipment.air_temp", 5.5f);
        state.set("defrost.active", true);
        therm.on_update(100u);  // first defrost tick: frozen_temp = 5.5

        // Temp rises during defrost (but display should still show frozen 5.5)
        state.set("equipment.air_temp", 8.0f);
        therm.on_update(100u);

        float disp = get_float(state, "thermostat.display_temp");
        CHECK_MESSAGE(disp == doctest::Approx(5.5f).epsilon(0.01f),
                      "display_defrost=1: should show frozen temp (5.5) not rising temp");
    }

    SUBCASE("display_defrost=2 shows -999 sentinel (dashes)") {
        state.set("thermostat.display_defrost", static_cast<int32_t>(2));
        state.set("equipment.air_temp", 5.5f);
        therm.on_update(100u);

        state.set("defrost.active", true);
        state.set("equipment.air_temp", 8.0f);
        therm.on_update(100u);

        float disp = get_float(state, "thermostat.display_temp");
        CHECK_MESSAGE(disp == doctest::Approx(-999.0f).epsilon(1.0f),
                      "display_defrost=2: should show -999 sentinel for dash display");
    }
}

// -----------------------------------------------------------------------------
// TEST 15: SAFETY_RUN cyclic ON/OFF phases
// -----------------------------------------------------------------------------

TEST_CASE("Thermostat: SAFETY_RUN cycles between ON and OFF phases [thermostat]") {
    modesp::SharedState state;
    ThermostatModule therm;
    modesp::ModuleManager mgr;
    mgr.register_module(therm);
    mgr.init_all(state);

    // Set short safety_run times for fast testing (override defaults)
    state.set("thermostat.safety_run_on",  static_cast<int32_t>(1));  // 1 min ON
    state.set("thermostat.safety_run_off", static_cast<int32_t>(1));  // 1 min OFF

    setup_normal_inputs(state, 3.0f, false, false, false);  // sensor1_ok=false
    skip_startup(therm);

    // Should be in SAFETY_RUN, ON phase
    auto s = get_str(state, "thermostat.state");
    REQUIRE_MESSAGE(s == "safety_run", "Must be in safety_run, got: ", s.c_str());
    CHECK_MESSAGE(get_bool(state, "thermostat.req.compressor") == true,
                  "SAFETY_RUN ON phase: compressor must be ON");

    // Advance past ON time (60 001 ms > 60 000 ms) -> switches to OFF phase
    therm.on_update(60001u);
    CHECK_MESSAGE(get_bool(state, "thermostat.req.compressor") == false,
                  "SAFETY_RUN OFF phase: compressor must be OFF");

    // Advance past OFF time -> switches back to ON phase
    therm.on_update(60001u);
    CHECK_MESSAGE(get_bool(state, "thermostat.req.compressor") == true,
                  "SAFETY_RUN ON phase (cycle 2): compressor must be ON again");
}
