/**
 * @file test_defrost.cpp
 * @brief HOST unit tests for DefrostModule.
 *
 * Framework: doctest (single header, DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN in test_main.cpp)
 *
 * Key implementation details that drive test design:
 *
 *   1. Phase state machine: IDLE -> [STABILIZE -> VALVE_OPEN ->] ACTIVE
 *                                -> [EQUALIZE ->] DRIP -> FAD -> IDLE
 *      Stabilize/VALVE_OPEN/EQUALIZE only for defrost_type=2 (hot gas).
 *
 *   2. Defrost types (defrost.type):
 *        0 = Natural  — compressor stops, no relay (comp=F, relay=F)
 *        1 = Heater   — relay ON, compressor OFF (comp=F, relay=T)
 *        2 = Hot gas  — compressor ON, relay ON  (comp=T, relay=T)
 *
 *   3. Initiation modes (defrost.initiation):
 *        0 = Timer only  (interval_timer >= interval)
 *        1 = Demand only (T_evap < demand_temp AND interval >= interval/4)
 *        2 = Combined    (first of timer or demand)
 *        3 = Disabled
 *
 *   4. Termination modes (defrost.termination):
 *        0 = By temperature: T_evap >= end_temp (after 60s MIN_ACTIVE_CHECK_MS)
 *        1 = By timer: max_duration only
 *
 *   5. Default settings:
 *        defrost.type         = 0     (natural)
 *        defrost.interval     = 8     h  = 28 800 000 ms
 *        defrost.counter_mode = 1     (real time always ticks)
 *        defrost.initiation   = 0     (timer)
 *        defrost.termination  = 0     (by temp)
 *        defrost.end_temp     = 8.0  C
 *        defrost.max_duration = 30   min = 1 800 000 ms
 *        defrost.demand_temp  = -25.0 C
 *        defrost.drip_time    = 2    min = 120 000 ms
 *        defrost.fan_delay    = 2    min = 120 000 ms
 *        defrost.fad_temp     = -5.0 C
 *        defrost.stabilize_time = 1  min = 60 000 ms
 *        defrost.equalize_time  = 2  min = 120 000 ms (read as 1.5min = 90s in code)
 *        defrost.valve_delay    = 3  s  = 3 000 ms
 *
 *   6. Manual start: write true to "defrost.manual_start" -> triggers immediately.
 *
 *   7. FAD phase: compressor ON, cond_fan ON, relay OFF.
 *      FAD ends by timer (fan_delay_ms) OR T_evap < fad_temp (if sensor2_ok).
 *
 *   8. Skip optimization: if termination=0 and sensor2_ok and T_evap > end_temp
 *      at timer trigger time -> skip defrost (evap is clean).
 *
 *   9. Protection lockout aborts any active defrost phase immediately.
 *
 *  10. interval_timer_ms_ restored from SharedState on init via
 *      read_int("defrost.interval_timer") * 1000.
 */

// -- HOST BUILD: mock ESP-IDF before anything else --
#include "mocks/freertos_mock.h"
#include "mocks/esp_log_mock.h"
#include "mocks/esp_timer_mock.h"

#include "doctest.h"
#include "modesp/shared_state.h"
#include "modesp/module_manager.h"
#include "defrost_module.h"

// -- Helper: extract typed value from SharedState -----------------------------

static float df_get_float(modesp::SharedState& state, const char* key, float def = -999.0f) {
    auto v = state.get(key);
    if (!v.has_value()) return def;
    const auto* fp = etl::get_if<float>(&v.value());
    return fp ? *fp : def;
}

static bool df_get_bool(modesp::SharedState& state, const char* key, bool def = false) {
    auto v = state.get(key);
    if (!v.has_value()) return def;
    const auto* bp = etl::get_if<bool>(&v.value());
    return bp ? *bp : def;
}

static int32_t df_get_int(modesp::SharedState& state, const char* key, int32_t def = -1) {
    auto v = state.get(key);
    if (!v.has_value()) return def;
    const auto* ip = etl::get_if<int32_t>(&v.value());
    return ip ? *ip : def;
}

static modesp::StringValue df_get_str(modesp::SharedState& state, const char* key) {
    auto v = state.get(key);
    if (!v.has_value()) return modesp::StringValue("");
    const auto* sp = etl::get_if<modesp::StringValue>(&v.value());
    return sp ? *sp : modesp::StringValue("");
}

// -- Helper: set up typical defrost inputs ------------------------------------
static void df_setup_inputs(modesp::SharedState& state,
                             float  evap_temp   = 0.0f,
                             bool   sensor2_ok  = false,
                             bool   compressor  = false,
                             bool   lockout     = false) {
    state.set("equipment.evap_temp",  evap_temp);
    state.set("equipment.sensor2_ok", sensor2_ok);
    state.set("equipment.compressor", compressor);
    state.set("protection.lockout",   lockout);
}

// -- Helper: configure short interval for fast timer-triggered defrost --------
// Sets defrost.interval to 1 hour and pre-loads interval_timer close to threshold.
static void df_setup_timer_almost_due(modesp::SharedState& state) {
    state.set("defrost.interval",     static_cast<int32_t>(1));      // 1 hour interval
    state.set("defrost.initiation",   static_cast<int32_t>(0));      // timer
    state.set("defrost.termination",  static_cast<int32_t>(1));      // by timer (simple)
    // Pre-load interval_timer to just below threshold (interval_ms - 1 tick)
    // interval_ms = 1h = 3 600 000 ms. interval_timer is stored in seconds.
    // Loading 3599s means after 1001ms tick it will be >= 3600000ms
    state.set("defrost.interval_timer", static_cast<int32_t>(3599)); // seconds
}

// -----------------------------------------------------------------------------
// TEST 1: Initial state after init -- IDLE, no active signals
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: initial state after init is IDLE [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    CHECK_MESSAGE(df_get_bool(state, "defrost.active") == false,
                  "defrost.active must be false after init");
    CHECK_MESSAGE(df_get_str(state, "defrost.phase") == "idle",
                  "defrost.phase must be 'idle' after init");
    CHECK_MESSAGE(df_get_bool(state, "defrost.req.compressor") == false,
                  "req.compressor must be false in IDLE");
    CHECK_MESSAGE(df_get_bool(state, "defrost.req.defrost_relay") == false,
                  "req.defrost_relay must be false in IDLE");
    CHECK_MESSAGE(df_get_bool(state, "defrost.req.evap_fan") == false,
                  "req.evap_fan must be false in IDLE");
    CHECK_MESSAGE(df_get_bool(state, "defrost.req.cond_fan") == false,
                  "req.cond_fan must be false in IDLE");
}

// -----------------------------------------------------------------------------
// TEST 2: Manual start triggers defrost immediately
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: manual start triggers defrost immediately [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    df_setup_inputs(state);
    state.set("defrost.type",        static_cast<int32_t>(0));  // natural
    state.set("defrost.manual_start", true);

    defrost.on_update(100u);

    CHECK_MESSAGE(df_get_bool(state, "defrost.active") == true,
                  "defrost.active must be true immediately after manual start");
    CHECK_MESSAGE(df_get_str(state, "defrost.phase") == "active",
                  "Phase must be 'active' for natural defrost type=0");

    // Natural defrost: all OFF
    CHECK_MESSAGE(df_get_bool(state, "defrost.req.compressor") == false,
                  "Natural defrost: compressor must be OFF");
    CHECK_MESSAGE(df_get_bool(state, "defrost.req.defrost_relay") == false,
                  "Natural defrost: relay must be OFF");
}

// -----------------------------------------------------------------------------
// TEST 3: Natural defrost (type=0) -- compressor and relay both OFF
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: type=0 natural -- compressor and relay OFF [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    df_setup_inputs(state);
    state.set("defrost.type",         static_cast<int32_t>(0));
    state.set("defrost.manual_start", true);
    defrost.on_update(100u);

    REQUIRE_MESSAGE(df_get_str(state, "defrost.phase") == "active",
                    "Pre-condition: must be in active phase");

    CHECK(df_get_bool(state, "defrost.req.compressor")    == false);
    CHECK(df_get_bool(state, "defrost.req.defrost_relay") == false);
    CHECK(df_get_bool(state, "defrost.req.evap_fan")      == false);
    CHECK(df_get_bool(state, "defrost.req.cond_fan")      == false);
}

// -----------------------------------------------------------------------------
// TEST 4: Heater defrost (type=1) -- relay ON, compressor OFF
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: type=1 heater -- relay ON compressor OFF [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    df_setup_inputs(state);
    // Must have a defrost relay configured for type=1 to actually use heater
    state.set("equipment.has_defrost_relay", true);
    state.set("defrost.type",         static_cast<int32_t>(1));
    state.set("defrost.manual_start", true);
    defrost.on_update(100u);

    REQUIRE_MESSAGE(df_get_str(state, "defrost.phase") == "active",
                    "Pre-condition: must be in active phase");

    CHECK_MESSAGE(df_get_bool(state, "defrost.req.compressor") == false,
                  "Heater defrost: compressor must be OFF");
    CHECK_MESSAGE(df_get_bool(state, "defrost.req.defrost_relay") == true,
                  "Heater defrost: relay must be ON");
}

// -----------------------------------------------------------------------------
// TEST 5: Hot gas defrost (type=2) -- 7-phase sequence
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: type=2 hot gas -- STABILIZE -> VALVE_OPEN -> ACTIVE [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    df_setup_inputs(state);
    state.set("equipment.has_defrost_relay", true);
    state.set("defrost.type",           static_cast<int32_t>(2));
    state.set("defrost.stabilize_time", static_cast<int32_t>(1));  // 1 min = 60 000 ms
    state.set("defrost.valve_delay",    static_cast<int32_t>(3));  // 3 s = 3 000 ms
    state.set("defrost.termination",    static_cast<int32_t>(1));  // by timer
    state.set("defrost.max_duration",   static_cast<int32_t>(30)); // 30 min
    state.set("defrost.manual_start",   true);
    defrost.on_update(100u);

    SUBCASE("starts in STABILIZE phase") {
        auto ph = df_get_str(state, "defrost.phase");
        CHECK_MESSAGE(ph == "stabilize",
                      "Hot gas must start in STABILIZE, got: ", ph.c_str());
        CHECK_MESSAGE(df_get_bool(state, "defrost.req.compressor") == true,
                      "STABILIZE: compressor must be ON");
        CHECK_MESSAGE(df_get_bool(state, "defrost.req.defrost_relay") == false,
                      "STABILIZE: relay must be OFF");
        CHECK_MESSAGE(df_get_bool(state, "defrost.req.cond_fan") == true,
                      "STABILIZE: cond_fan must be ON");
    }

    SUBCASE("advances to VALVE_OPEN after stabilize time") {
        // stabilize_ms = 1 min = 60 000 ms; advance past it
        defrost.on_update(60001u);
        auto ph = df_get_str(state, "defrost.phase");
        CHECK_MESSAGE(ph == "valve_open",
                      "Must be in VALVE_OPEN after stabilize, got: ", ph.c_str());
        CHECK_MESSAGE(df_get_bool(state, "defrost.req.compressor") == true,
                      "VALVE_OPEN: compressor ON");
        CHECK_MESSAGE(df_get_bool(state, "defrost.req.defrost_relay") == true,
                      "VALVE_OPEN: relay ON");
        CHECK_MESSAGE(df_get_bool(state, "defrost.req.cond_fan") == true,
                      "VALVE_OPEN: cond_fan ON");
    }

    SUBCASE("advances to ACTIVE after valve delay") {
        defrost.on_update(60001u);  // past stabilize
        REQUIRE(df_get_str(state, "defrost.phase") == "valve_open");
        defrost.on_update(3001u);   // past valve_delay (3s)
        auto ph = df_get_str(state, "defrost.phase");
        CHECK_MESSAGE(ph == "active",
                      "Must be in ACTIVE after valve delay, got: ", ph.c_str());
        // Hot gas active: compressor ON, relay ON
        CHECK(df_get_bool(state, "defrost.req.compressor")    == true);
        CHECK(df_get_bool(state, "defrost.req.defrost_relay") == true);
    }
}

// -----------------------------------------------------------------------------
// TEST 6: Timer-triggered defrost initiation
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: timer-triggered initiation after interval [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);

    // interval_timer MUST be set BEFORE init_all() because on_init() reads it
    // to restore the elapsed counter (interval_timer_ms_ = value * 1000).
    // df_setup_timer_almost_due sets 3599s so after 1001ms tick elapsed >= 3600s.
    state.set("defrost.interval_timer", static_cast<int32_t>(3599)); // 3599 s elapsed
    state.set("defrost.interval",       static_cast<int32_t>(1));    // 1h interval
    state.set("defrost.initiation",     static_cast<int32_t>(0));    // timer
    state.set("defrost.termination",    static_cast<int32_t>(1));    // by timer

    mgr.init_all(state);

    df_setup_inputs(state);
    // type=0 natural, sensor not connected
    state.set("defrost.type", static_cast<int32_t>(0));

    SUBCASE("no defrost before interval elapses") {
        // Timer loaded at 3599s; advance 999ms -> total will be < 3 600 000ms
        defrost.on_update(999u);
        CHECK_MESSAGE(df_get_bool(state, "defrost.active") == false,
                      "Must not start defrost before timer interval");
        CHECK_MESSAGE(df_get_str(state, "defrost.phase") == "idle",
                      "Phase must remain idle before timer fires");
    }

    SUBCASE("defrost starts after interval elapses") {
        // Advance 1001ms -> interval_timer_ms_ = 3599*1000 + 1001 >= 3 600 000ms
        defrost.on_update(1001u);
        CHECK_MESSAGE(df_get_bool(state, "defrost.active") == true,
                      "Defrost must start when timer interval is reached");
        CHECK_MESSAGE(df_get_str(state, "defrost.phase") == "active",
                      "Phase must be 'active' for natural defrost after timer");
    }
}

// -----------------------------------------------------------------------------
// TEST 7: Demand-triggered defrost when T_evap is cold enough
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: demand-triggered when evap temp below demand threshold [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    state.set("defrost.initiation",  static_cast<int32_t>(1));   // demand only
    state.set("defrost.termination", static_cast<int32_t>(1));   // by timer
    state.set("defrost.type",        static_cast<int32_t>(0));   // natural
    state.set("defrost.demand_temp", -20.0f);                    // demand threshold
    state.set("defrost.interval",    static_cast<int32_t>(8));   // 8h interval (minimum guard)

    SUBCASE("no demand trigger without sensor") {
        df_setup_inputs(state, -25.0f, false); // sensor2_ok=false
        // Advance past minimum interval guard (8h/4 = 2h = 7 200 000ms)
        // Demand trigger requires interval_timer >= interval/4 = 7 200 000ms
        defrost.on_update(7200001u);
        CHECK_MESSAGE(df_get_bool(state, "defrost.active") == false,
                      "Demand trigger must not fire without sensor");
    }

    SUBCASE("demand trigger fires when T_evap < demand_temp and sensor OK") {
        df_setup_inputs(state, -25.0f, true); // T=-25 < demand=-20, sensor OK
        // Advance past minimum guard: 8h/4 = 2h = 7 200 000ms
        defrost.on_update(7200001u);
        CHECK_MESSAGE(df_get_bool(state, "defrost.active") == true,
                      "Demand trigger must fire when T_evap < demand_temp");
    }

    SUBCASE("no demand trigger when T_evap >= demand_temp") {
        df_setup_inputs(state, -15.0f, true); // T=-15 >= demand=-20 -> no trigger
        defrost.on_update(7200001u);
        CHECK_MESSAGE(df_get_bool(state, "defrost.active") == false,
                      "Demand trigger must NOT fire when T_evap >= demand_temp");
    }
}

// -----------------------------------------------------------------------------
// TEST 8: Termination by temperature (type=0, termination=0)
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: termination by temperature when T_evap >= end_temp [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    df_setup_inputs(state, 0.0f, true);  // sensor2_ok=true, T_evap=0
    state.set("defrost.type",         static_cast<int32_t>(0));  // natural
    state.set("defrost.termination",  static_cast<int32_t>(0));  // by temp
    state.set("defrost.end_temp",     8.0f);
    state.set("defrost.max_duration", static_cast<int32_t>(30)); // 30 min safety
    state.set("defrost.drip_time",    static_cast<int32_t>(2));  // 2 min
    state.set("defrost.fan_delay",    static_cast<int32_t>(0));  // 0 = skip FAD
    state.set("defrost.manual_start", true);

    defrost.on_update(100u);
    REQUIRE(df_get_str(state, "defrost.phase") == "active");

    // Simulate evap warming: T_evap now > end_temp
    // Must wait MIN_ACTIVE_CHECK_MS = 60 000ms before temp check applies
    state.set("equipment.evap_temp",  9.0f);  // above end_temp=8.0
    state.set("equipment.sensor2_ok", true);

    SUBCASE("stays active before MIN_ACTIVE_CHECK_MS (60s)") {
        defrost.on_update(30000u);
        auto ph = df_get_str(state, "defrost.phase");
        CHECK_MESSAGE(ph == "active",
                      "Must stay in ACTIVE before 60s min check time, got: ", ph.c_str());
    }

    SUBCASE("terminates after MIN_ACTIVE_CHECK_MS and T_evap >= end_temp") {
        defrost.on_update(60001u);  // past 60s check window
        auto ph = df_get_str(state, "defrost.phase");
        // With fan_delay=0, after ACTIVE we go DRIP then immediately IDLE
        // But drip_time=2min=120 000ms must elapse. So after 60001ms, in DRIP.
        // Actually: ACTIVE ends -> enter DRIP. drip_time=120 000ms.
        // After 60001ms total we are in DRIP now.
        CHECK_MESSAGE((ph == "drip" || ph == "idle"),
                      "Must transition out of ACTIVE after end_temp reached, got: ", ph.c_str());
        CHECK_MESSAGE(df_get_str(state, "defrost.last_termination") == "temp",
                      "Last termination must be 'temp'");
    }
}

// -----------------------------------------------------------------------------
// TEST 9: Termination by timer (termination=1) -- safety timer fires
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: termination by timer when max_duration elapses [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    df_setup_inputs(state, 0.0f, false);  // no sensor
    state.set("defrost.type",         static_cast<int32_t>(0));  // natural
    state.set("defrost.termination",  static_cast<int32_t>(1));  // by timer
    state.set("defrost.max_duration", static_cast<int32_t>(1));  // 1 min = 60 000ms
    state.set("defrost.drip_time",    static_cast<int32_t>(2));  // 2 min
    state.set("defrost.fan_delay",    static_cast<int32_t>(0));  // skip FAD
    state.set("defrost.manual_start", true);

    defrost.on_update(100u);
    REQUIRE(df_get_str(state, "defrost.phase") == "active");

    // Advance past max_duration (60 001ms > 60 000ms = 1min)
    defrost.on_update(60001u);

    auto ph = df_get_str(state, "defrost.phase");
    CHECK_MESSAGE(ph == "drip",
                  "After timer termination must be in DRIP, got: ", ph.c_str());
    CHECK_MESSAGE(df_get_str(state, "defrost.last_termination") == "timeout",
                  "last_termination must be 'timeout'");
    CHECK_MESSAGE(df_get_int(state, "defrost.defrost_count") == 1,
                  "defrost_count must increment after ACTIVE phase");
}

// -----------------------------------------------------------------------------
// TEST 10: DRIP phase -- all requests OFF, waits drip_time
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: DRIP phase keeps all requests OFF [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    df_setup_inputs(state, 0.0f, false);
    state.set("defrost.type",         static_cast<int32_t>(0));
    state.set("defrost.termination",  static_cast<int32_t>(1));
    state.set("defrost.max_duration", static_cast<int32_t>(1));  // 1 min
    state.set("defrost.drip_time",    static_cast<int32_t>(2));  // 2 min
    state.set("defrost.fan_delay",    static_cast<int32_t>(0));  // skip FAD
    state.set("defrost.manual_start", true);

    defrost.on_update(100u);  // ACTIVE
    defrost.on_update(60001u);  // timer fires -> DRIP

    REQUIRE(df_get_str(state, "defrost.phase") == "drip");

    // All requests must be OFF during drip
    CHECK(df_get_bool(state, "defrost.req.compressor")    == false);
    CHECK(df_get_bool(state, "defrost.req.defrost_relay") == false);
    CHECK(df_get_bool(state, "defrost.req.evap_fan")      == false);
    CHECK(df_get_bool(state, "defrost.req.cond_fan")      == false);

    // defrost.active must still be true during drip
    CHECK_MESSAGE(df_get_bool(state, "defrost.active") == true,
                  "defrost.active must be true during DRIP phase");

    // Advance past drip_time (2min=120 000ms) -> should go to IDLE (fan_delay=0 skips FAD)
    defrost.on_update(120001u);
    auto ph = df_get_str(state, "defrost.phase");
    CHECK_MESSAGE(ph == "idle",
                  "After drip_time with fan_delay=0 must go to IDLE, got: ", ph.c_str());
    CHECK_MESSAGE(df_get_bool(state, "defrost.active") == false,
                  "defrost.active must be false after returning to IDLE");
}

// -----------------------------------------------------------------------------
// TEST 11: FAD phase (Fan After Defrost) -- compressor ON, cond_fan ON
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: FAD phase -- compressor ON cond_fan ON [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    df_setup_inputs(state, 0.0f, false);
    state.set("defrost.type",         static_cast<int32_t>(0));
    state.set("defrost.termination",  static_cast<int32_t>(1));
    state.set("defrost.max_duration", static_cast<int32_t>(1));   // 1 min
    state.set("defrost.drip_time",    static_cast<int32_t>(1));   // 1 min
    state.set("defrost.fan_delay",    static_cast<int32_t>(2));   // 2 min FAD
    state.set("defrost.manual_start", true);

    defrost.on_update(100u);    // -> ACTIVE
    defrost.on_update(60001u);  // ACTIVE timer -> DRIP
    REQUIRE(df_get_str(state, "defrost.phase") == "drip");

    defrost.on_update(60001u);  // drip done -> FAD
    auto ph = df_get_str(state, "defrost.phase");
    REQUIRE_MESSAGE(ph == "fad", "Must be in FAD, got: ", ph.c_str());

    CHECK_MESSAGE(df_get_bool(state, "defrost.req.compressor") == true,
                  "FAD: compressor must be ON");
    CHECK_MESSAGE(df_get_bool(state, "defrost.req.cond_fan") == true,
                  "FAD: cond_fan must be ON");
    CHECK_MESSAGE(df_get_bool(state, "defrost.req.defrost_relay") == false,
                  "FAD: relay must be OFF");
    CHECK_MESSAGE(df_get_bool(state, "defrost.req.evap_fan") == false,
                  "FAD: evap_fan must be OFF");

    SUBCASE("FAD ends by timer") {
        defrost.on_update(120001u);  // past fan_delay (2 min = 120 000ms)
        CHECK_MESSAGE(df_get_str(state, "defrost.phase") == "idle",
                      "FAD must end by timer and return to IDLE");
        CHECK(df_get_bool(state, "defrost.active") == false);
    }

    SUBCASE("FAD ends early by T_evap < fad_temp") {
        state.set("defrost.fad_temp",         -5.0f);
        state.set("equipment.evap_temp",      -8.0f);  // below fad_temp
        state.set("equipment.sensor2_ok",     true);
        defrost.on_update(1000u);  // one tick
        CHECK_MESSAGE(df_get_str(state, "defrost.phase") == "idle",
                      "FAD must end early when T_evap < fad_temp");
    }
}

// -----------------------------------------------------------------------------
// TEST 12: Protection lockout aborts active defrost
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: protection lockout aborts active defrost [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    df_setup_inputs(state);
    state.set("defrost.type",         static_cast<int32_t>(1));
    state.set("equipment.has_defrost_relay", true);
    state.set("defrost.manual_start", true);
    defrost.on_update(100u);

    REQUIRE_MESSAGE(df_get_bool(state, "defrost.active") == true,
                    "Pre-condition: defrost must be active");

    // Trigger lockout
    state.set("protection.lockout", true);
    defrost.on_update(100u);

    CHECK_MESSAGE(df_get_bool(state, "defrost.active") == false,
                  "Lockout must abort defrost -> defrost.active = false");
    CHECK_MESSAGE(df_get_str(state, "defrost.phase") == "idle",
                  "Lockout must return phase to IDLE");
    CHECK_MESSAGE(df_get_bool(state, "defrost.req.compressor") == false,
                  "After lockout abort: all requests OFF");
    CHECK_MESSAGE(df_get_bool(state, "defrost.req.defrost_relay") == false,
                  "After lockout abort: relay must be OFF");
}

// -----------------------------------------------------------------------------
// TEST 13: Manual stop aborts active defrost
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: manual stop aborts active defrost [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    df_setup_inputs(state);
    state.set("defrost.type",         static_cast<int32_t>(0));
    state.set("defrost.manual_start", true);
    defrost.on_update(100u);

    REQUIRE(df_get_bool(state, "defrost.active") == true);

    state.set("defrost.manual_stop", true);
    defrost.on_update(100u);

    CHECK_MESSAGE(df_get_bool(state, "defrost.active") == false,
                  "Manual stop must abort defrost");
    CHECK_MESSAGE(df_get_str(state, "defrost.phase") == "idle",
                  "Manual stop must return to IDLE");
    CHECK_MESSAGE(df_get_bool(state, "defrost.manual_stop") == false,
                  "manual_stop trigger must be cleared after processing");
}

// -----------------------------------------------------------------------------
// TEST 14: Skip optimization -- evap clean, no defrost needed
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: skip when evap temp > end_temp (clean evaporator) [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    // Evap sensor OK, T_evap above end_temp -> evap is clean -> skip
    df_setup_inputs(state, 10.0f, true);  // T_evap=10 > end_temp=8 -> skip
    df_setup_timer_almost_due(state);
    state.set("defrost.type",        static_cast<int32_t>(0));
    state.set("defrost.termination", static_cast<int32_t>(0));  // by temp (enables skip logic)
    state.set("defrost.end_temp",    8.0f);

    // Trigger timer
    defrost.on_update(1001u);

    CHECK_MESSAGE(df_get_bool(state, "defrost.active") == false,
                  "Defrost must be skipped when T_evap > end_temp (evap is clean)");
    CHECK_MESSAGE(df_get_str(state, "defrost.phase") == "idle",
                  "Phase must stay IDLE after skip");
}

// -----------------------------------------------------------------------------
// TEST 15: defrost.initiation=3 (disabled) -- never starts
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: compressor_blocked aborts hot gas defrost [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    df_setup_inputs(state);
    state.set("equipment.has_defrost_relay", true);
    state.set("defrost.type",           static_cast<int32_t>(2));  // hot gas
    state.set("defrost.stabilize_time", static_cast<int32_t>(1));  // 1 min
    state.set("defrost.valve_delay",    static_cast<int32_t>(3));  // 3 s
    state.set("defrost.termination",    static_cast<int32_t>(1));  // by timer

    SUBCASE("aborts hot gas in STABILIZE phase") {
        state.set("defrost.manual_start", true);
        defrost.on_update(100u);
        REQUIRE(df_get_str(state, "defrost.phase") == "stabilize");

        state.set("protection.compressor_blocked", true);
        defrost.on_update(100u);

        CHECK(df_get_bool(state, "defrost.active") == false);
        CHECK(df_get_str(state, "defrost.phase") == "idle");
        CHECK(df_get_bool(state, "defrost.req.compressor") == false);
        CHECK(df_get_bool(state, "defrost.req.defrost_relay") == false);
    }

    SUBCASE("aborts hot gas in ACTIVE phase") {
        state.set("defrost.manual_start", true);
        defrost.on_update(100u);      // → STABILIZE
        defrost.on_update(60001u);    // → VALVE_OPEN
        defrost.on_update(3001u);     // → ACTIVE
        REQUIRE(df_get_str(state, "defrost.phase") == "active");

        state.set("protection.compressor_blocked", true);
        defrost.on_update(100u);

        CHECK(df_get_bool(state, "defrost.active") == false);
        CHECK(df_get_str(state, "defrost.phase") == "idle");
    }

    SUBCASE("does NOT abort heater defrost (type=1)") {
        state.set("defrost.type", static_cast<int32_t>(1));  // heater
        state.set("defrost.manual_start", true);
        defrost.on_update(100u);
        REQUIRE(df_get_str(state, "defrost.phase") == "active");

        state.set("protection.compressor_blocked", true);
        defrost.on_update(100u);

        // Heater defrost continues — compressor not needed
        CHECK(df_get_bool(state, "defrost.active") == true);
        CHECK(df_get_str(state, "defrost.phase") == "active");
    }

    SUBCASE("does NOT abort hot gas in DRIP/FAD phases") {
        state.set("defrost.manual_start", true);
        state.set("defrost.max_duration", static_cast<int32_t>(1));  // 1 min
        state.set("defrost.drip_time",    static_cast<int32_t>(2));  // 2 min
        state.set("defrost.fan_delay",    static_cast<int32_t>(2));  // 2 min FAD
        defrost.on_update(100u);      // → STABILIZE
        defrost.on_update(60001u);    // → VALVE_OPEN
        defrost.on_update(3001u);     // → ACTIVE
        defrost.on_update(60001u);    // → EQUALIZE (timer termination)
        defrost.on_update(120001u);   // → DRIP
        REQUIRE(df_get_str(state, "defrost.phase") == "drip");

        state.set("protection.compressor_blocked", true);
        defrost.on_update(100u);

        // DRIP phase continues — no compressor needed
        CHECK(df_get_bool(state, "defrost.active") == true);
        CHECK(df_get_str(state, "defrost.phase") == "drip");
    }
}

// -----------------------------------------------------------------------------
// TEST 16: defrost.initiation=3 (disabled) -- never starts
// -----------------------------------------------------------------------------

TEST_CASE("Defrost: initiation=3 disabled -- never auto-starts [defrost]") {
    modesp::SharedState state;
    DefrostModule defrost;
    modesp::ModuleManager mgr;
    mgr.register_module(defrost);
    mgr.init_all(state);

    df_setup_inputs(state, -30.0f, true);  // very cold evap, sensor OK
    state.set("defrost.initiation",  static_cast<int32_t>(3));  // disabled
    state.set("defrost.interval",    static_cast<int32_t>(1));  // short interval

    // Load interval timer past threshold
    state.set("defrost.interval_timer", static_cast<int32_t>(3600));  // exactly at 1h

    // Tick well past any timer
    defrost.on_update(3600001u);

    CHECK_MESSAGE(df_get_bool(state, "defrost.active") == false,
                  "With initiation=3 (disabled), defrost must never auto-start");
    CHECK_MESSAGE(df_get_str(state, "defrost.phase") == "idle",
                  "Phase must remain IDLE with initiation disabled");
}
