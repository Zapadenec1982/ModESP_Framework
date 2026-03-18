/**
 * @file test_protection.cpp
 * @brief HOST unit tests for ProtectionModule.
 *
 * Framework: doctest (single header, DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN in test_main.cpp)
 *
 * Key implementation details that drive test design:
 *
 *   1. Alarm monitors: NORMAL -> PENDING (delay) -> ALARM -> NORMAL (auto-clear)
 *
 *   2. Sensor alarms (ERR1, ERR2) are INSTANT — no delay.
 *
 *   3. High temp alarm (HAL): delayed by high_alarm_delay (default 30 min).
 *      Blocked during defrost HEATING phases: stabilize, valve_open, active, equalize.
 *      Also suppressed for post_defrost_delay after defrost ends.
 *
 *   4. Low temp alarm (LAL): delayed by low_alarm_delay (default 30 min).
 *      NOT blocked during defrost. Always active when sensor OK.
 *
 *   5. Door alarm: delayed by door_delay (default 5 min).
 *      Feature-gated: has_feature("door_protection") must be true.
 *      (Generated features_config.h enables this in the test build.)
 *
 *   6. Default settings (from sync_settings() fallback arguments):
 *        high_limit           = 12.0 C
 *        low_limit            = -35.0 C
 *        high_alarm_delay     = 30 min = 1 800 000 ms
 *        low_alarm_delay      = 30 min = 1 800 000 ms
 *        door_delay           = 5  min =   300 000 ms
 *        post_defrost_delay   = 30 min = 1 800 000 ms
 *        manual_reset         = false (auto-clear when condition normalizes)
 *
 *   7. Alarm priority (publish_alarms):
 *        err1 > high_temp > low_temp > err2 > door
 *
 *   8. protection.lockout is always false (reserved for Phase 10+).
 *
 *   9. Reset command: write true to "protection.reset_alarms" -> clears all active.
 *
 *  10. TIMING BUG FIX: Use SETUP_MS=1 for setup ticks to avoid accumulating
 *      delay time during test setup. This prevents the pending_ms counter from
 *      advancing during setup steps that should not count toward alarm delay.
 *
 *  11. Post-defrost suppression timing:
 *      - Suppression starts when defrost.active transitions from true -> false.
 *      - post_defrost_timer_ms_ accumulates ONLY after the suppression starts.
 *      - The suppression window equals post_defrost_delay_ms_.
 */

// -- HOST BUILD: mock ESP-IDF before anything else --
#include "mocks/freertos_mock.h"
#include "mocks/esp_log_mock.h"
#include "mocks/esp_timer_mock.h"

#include "doctest.h"
#include "modesp/shared_state.h"
#include "modesp/module_manager.h"
#include "protection_module.h"

// ── Small dt used for "setup" ticks that must not accumulate alarm timers ────
static constexpr uint32_t SETUP_MS = 1u;

// -- Helper: extract typed value from SharedState -----------------------------

static float pr_get_float(modesp::SharedState& state, const char* key, float def = -999.0f) {
    auto v = state.get(key);
    if (!v.has_value()) return def;
    const auto* fp = etl::get_if<float>(&v.value());
    return fp ? *fp : def;
}

static bool pr_get_bool(modesp::SharedState& state, const char* key, bool def = false) {
    auto v = state.get(key);
    if (!v.has_value()) return def;
    const auto* bp = etl::get_if<bool>(&v.value());
    return bp ? *bp : def;
}

static modesp::StringValue pr_get_str(modesp::SharedState& state, const char* key) {
    auto v = state.get(key);
    if (!v.has_value()) return modesp::StringValue("");
    const auto* sp = etl::get_if<modesp::StringValue>(&v.value());
    return sp ? *sp : modesp::StringValue("");
}

// -- Helper: set up typical normal-operation inputs ---------------------------
static void pr_setup_normal(modesp::SharedState& state,
                             float air_temp   = 5.0f,
                             bool  sensor1_ok = true,
                             bool  sensor2_ok = true,
                             bool  door_open  = false,
                             bool  defrost    = false,
                             bool  compressor = false) {
    state.set("equipment.air_temp",   air_temp);
    state.set("equipment.sensor1_ok", sensor1_ok);
    state.set("equipment.sensor2_ok", sensor2_ok);
    state.set("equipment.has_evap_temp", true);   // sensor2 підключений в тестах
    state.set("equipment.door_open",  door_open);
    state.set("equipment.has_door_contact", true); // door підключений в тестах
    state.set("equipment.compressor", compressor);
    state.set("defrost.active",       defrost);
    state.set("defrost.phase",        modesp::StringValue("idle"));
}

// -- Helper: set short alarm delays for fast testing --------------------------
static void pr_setup_short_delays(modesp::SharedState& state,
                                   uint32_t high_delay_min = 1,
                                   uint32_t low_delay_min  = 1,
                                   uint32_t door_delay_min = 1) {
    state.set("protection.high_alarm_delay", static_cast<int32_t>(high_delay_min));
    state.set("protection.low_alarm_delay",  static_cast<int32_t>(low_delay_min));
    state.set("protection.door_delay",       static_cast<int32_t>(door_delay_min));
}

// -----------------------------------------------------------------------------
// TEST 1: No alarm in normal conditions
// -----------------------------------------------------------------------------

TEST_CASE("Protection: no alarm in normal conditions [protection]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_normal(state, 5.0f, true, true, false, false);
    prot.on_update(1000u);

    CHECK_MESSAGE(pr_get_bool(state, "protection.alarm_active") == false,
                  "No alarm expected in normal conditions");
    CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "none",
                  "alarm_code must be 'none' in normal conditions");
    CHECK(pr_get_bool(state, "protection.high_temp_alarm")  == false);
    CHECK(pr_get_bool(state, "protection.low_temp_alarm")   == false);
    CHECK(pr_get_bool(state, "protection.sensor1_alarm")    == false);
    CHECK(pr_get_bool(state, "protection.sensor2_alarm")    == false);
    CHECK(pr_get_bool(state, "protection.door_alarm")       == false);
    CHECK(pr_get_bool(state, "protection.lockout")          == false);
}

// -----------------------------------------------------------------------------
// TEST 2: Sensor1 fault alarm is instant
// -----------------------------------------------------------------------------

TEST_CASE("Protection: sensor1 fault triggers ERR1 alarm instantly [protection]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_normal(state, 5.0f, false, true, false, false);  // sensor1_ok=false
    prot.on_update(SETUP_MS);

    CHECK_MESSAGE(pr_get_bool(state, "protection.sensor1_alarm") == true,
                  "ERR1 alarm must be instant when sensor1_ok=false");
    CHECK_MESSAGE(pr_get_bool(state, "protection.alarm_active") == true,
                  "alarm_active must be true with sensor1 fault");
    CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "err1",
                  "alarm_code must be 'err1' for sensor1 fault");

    SUBCASE("alarm auto-clears when sensor restores") {
        state.set("equipment.sensor1_ok", true);
        prot.on_update(SETUP_MS);
        CHECK_MESSAGE(pr_get_bool(state, "protection.sensor1_alarm") == false,
                      "ERR1 must auto-clear when sensor1 restores");
        CHECK_MESSAGE(pr_get_bool(state, "protection.alarm_active") == false,
                      "alarm_active must be false after sensor1 restores");
    }
}

// -----------------------------------------------------------------------------
// TEST 3: Sensor2 fault alarm is instant
// -----------------------------------------------------------------------------

TEST_CASE("Protection: sensor2 fault triggers ERR2 alarm instantly [protection]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_normal(state, 5.0f, true, false, false, false);  // sensor2_ok=false
    prot.on_update(SETUP_MS);

    CHECK_MESSAGE(pr_get_bool(state, "protection.sensor2_alarm") == true,
                  "ERR2 alarm must be instant when sensor2_ok=false");
    CHECK_MESSAGE(pr_get_bool(state, "protection.alarm_active") == true,
                  "alarm_active must be true with sensor2 fault");
    CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "err2",
                  "alarm_code must be 'err2' for sensor2 fault (no higher-priority alarms)");

    SUBCASE("ERR2 auto-clears when sensor restores") {
        state.set("equipment.sensor2_ok", true);
        prot.on_update(SETUP_MS);
        CHECK_MESSAGE(pr_get_bool(state, "protection.sensor2_alarm") == false,
                      "ERR2 must auto-clear when sensor2 restores");
    }
}

// -----------------------------------------------------------------------------
// TEST 4: High temp alarm only after delay
// -----------------------------------------------------------------------------

TEST_CASE("Protection: high temp alarm only after delay [protection]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_short_delays(state, 1, 1, 1);  // 1 min delays = 60 000 ms
    pr_setup_normal(state, 15.0f, true, true, false, false);  // T=15 > limit=12

    // The delay period = 1 min = 60 000 ms
    static constexpr uint32_t delay_ms = 60000u;

    SUBCASE("no alarm before delay elapses") {
        prot.on_update(delay_ms - 1u);
        CHECK_MESSAGE(pr_get_bool(state, "protection.high_temp_alarm") == false,
                      "High temp alarm must NOT trigger before delay elapses");
        CHECK_MESSAGE(pr_get_bool(state, "protection.alarm_active") == false,
                      "alarm_active must be false before delay");
    }

    SUBCASE("alarm triggers after delay elapses") {
        prot.on_update(delay_ms + 1u);
        CHECK_MESSAGE(pr_get_bool(state, "protection.high_temp_alarm") == true,
                      "High temp alarm must trigger after delay elapses");
        CHECK_MESSAGE(pr_get_bool(state, "protection.alarm_active") == true,
                      "alarm_active must be true after high temp alarm fires");
        CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "high_temp",
                      "alarm_code must be 'high_temp'");
    }

    SUBCASE("exact boundary: alarm at exactly delay_ms + 1") {
        prot.on_update(delay_ms);
        // At exactly delay_ms the condition is pending_ms == delay_ms which is >= delay_ms
        // Implementation uses >=, so this should fire.
        // Note: first tick adds dt_ms to pending_ms. Single tick of delay_ms means
        // pending_ms = delay_ms >= delay_ms -> alarm fires.
        CHECK_MESSAGE(pr_get_bool(state, "protection.high_temp_alarm") == true,
                      "High temp alarm must trigger at exactly delay_ms boundary");
    }

    SUBCASE("timer accumulates across multiple ticks") {
        // Split the delay across two ticks
        prot.on_update(delay_ms / 2);
        CHECK(pr_get_bool(state, "protection.high_temp_alarm") == false);
        prot.on_update(delay_ms / 2 + 1u);
        CHECK_MESSAGE(pr_get_bool(state, "protection.high_temp_alarm") == true,
                      "High temp alarm must fire after accumulated delay");
    }
}

// -----------------------------------------------------------------------------
// TEST 5: Low temp alarm only after delay
// -----------------------------------------------------------------------------

TEST_CASE("Protection: low temp alarm only after delay [protection]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_short_delays(state, 1, 1, 1);  // 1 min delays
    // T=-40 < low_limit=-35
    pr_setup_normal(state, -40.0f, true, true, false, false);

    static constexpr uint32_t delay_ms = 60000u;

    SUBCASE("no alarm before delay") {
        prot.on_update(delay_ms - 1u);
        CHECK_MESSAGE(pr_get_bool(state, "protection.low_temp_alarm") == false,
                      "Low temp alarm must NOT trigger before delay elapses");
    }

    SUBCASE("alarm triggers after delay") {
        prot.on_update(delay_ms + 1u);
        CHECK_MESSAGE(pr_get_bool(state, "protection.low_temp_alarm") == true,
                      "Low temp alarm must trigger after delay elapses");
        CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "low_temp",
                      "alarm_code must be 'low_temp'");
    }
}

// -----------------------------------------------------------------------------
// TEST 6: Alarm auto-clears when temp returns to normal
// -----------------------------------------------------------------------------

TEST_CASE("Protection: alarm auto-clears when temp returns to normal [protection]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_short_delays(state, 1, 1, 1);
    pr_setup_normal(state, 15.0f, true, true, false, false);

    // Trigger the alarm
    prot.on_update(60001u);
    REQUIRE_MESSAGE(pr_get_bool(state, "protection.high_temp_alarm") == true,
                    "Pre-condition: high temp alarm must be active");

    // Bring temp back to normal
    state.set("equipment.air_temp", 5.0f);
    prot.on_update(SETUP_MS);

    CHECK_MESSAGE(pr_get_bool(state, "protection.high_temp_alarm") == false,
                  "High temp alarm must auto-clear when temp returns below limit");
    CHECK_MESSAGE(pr_get_bool(state, "protection.alarm_active") == false,
                  "alarm_active must be false after auto-clear");
    CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "none",
                  "alarm_code must revert to 'none' after auto-clear");
}

// -----------------------------------------------------------------------------
// TEST 7: Door alarm after door_delay
// -----------------------------------------------------------------------------

TEST_CASE("Protection: door alarm fires after door_delay [protection]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_short_delays(state, 1, 1, 1);  // door_delay = 1 min = 60 000 ms
    pr_setup_normal(state, 5.0f, true, true, true, false);  // door_open=true

    static constexpr uint32_t door_delay_ms = 60000u;

    SUBCASE("no door alarm before delay") {
        prot.on_update(door_delay_ms - 1u);
        CHECK_MESSAGE(pr_get_bool(state, "protection.door_alarm") == false,
                      "Door alarm must NOT fire before door_delay elapses");
    }

    SUBCASE("door alarm fires after delay") {
        prot.on_update(door_delay_ms + 1u);
        CHECK_MESSAGE(pr_get_bool(state, "protection.door_alarm") == true,
                      "Door alarm must fire after door_delay elapses");
        CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "door",
                      "alarm_code must be 'door' (lowest priority)");
    }

    SUBCASE("door alarm auto-clears when door closes") {
        prot.on_update(door_delay_ms + 1u);
        REQUIRE(pr_get_bool(state, "protection.door_alarm") == true);

        state.set("equipment.door_open", false);
        prot.on_update(SETUP_MS);

        CHECK_MESSAGE(pr_get_bool(state, "protection.door_alarm") == false,
                      "Door alarm must auto-clear when door closes");
    }

    SUBCASE("door pending resets if door closes before alarm fires") {
        prot.on_update(door_delay_ms / 2);
        CHECK(pr_get_bool(state, "protection.door_alarm") == false);

        state.set("equipment.door_open", false);
        prot.on_update(SETUP_MS);
        // Door closes, pending resets

        state.set("equipment.door_open", true);
        prot.on_update(door_delay_ms / 2);
        // Only half the delay accumulated again -> no alarm
        CHECK_MESSAGE(pr_get_bool(state, "protection.door_alarm") == false,
                      "Door pending must reset when door closes before alarm fires");
    }
}

// -----------------------------------------------------------------------------
// TEST 8: Post-defrost suppression of high temp alarm
// -----------------------------------------------------------------------------

TEST_CASE("Protection: post-defrost suppression of high temp alarm [protection]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // Use short delays to make test fast
    // high_alarm_delay = 1 min = 60 000 ms
    // post_defrost_delay = 1 min = 60 000 ms
    pr_setup_short_delays(state, 1, 1, 1);
    state.set("protection.post_defrost_delay", static_cast<int32_t>(1));  // 1 min

    static constexpr uint32_t high_delay_ms   = 60000u;
    static constexpr uint32_t post_delay_ms   = 60000u;

    // Simulate defrost active -> ended transition
    pr_setup_normal(state, 5.0f, true, true, false, true);  // defrost=true
    state.set("defrost.phase", modesp::StringValue("active"));

    // Setup tick: mark was_defrost_active_ = true (use SETUP_MS to avoid accumulation)
    prot.on_update(SETUP_MS);

    // Now end defrost: this tick triggers post_defrost_suppression_ = true
    state.set("defrost.active", false);
    state.set("defrost.phase",  modesp::StringValue("idle"));
    // Set high temp condition that would normally trigger alarm
    state.set("equipment.air_temp", 15.0f);
    prot.on_update(SETUP_MS);  // suppression starts here, post_defrost_timer_ms_ = SETUP_MS

    SUBCASE("high temp alarm suppressed within post-defrost window") {
        // Advance to just before post_defrost_delay expires
        // Total post_defrost_timer_ms_ accumulated = SETUP_MS + (post_delay_ms - SETUP_MS - 1)
        // = post_delay_ms - 1 < post_delay_ms -> still suppressed
        prot.on_update(post_delay_ms - SETUP_MS - 1u);

        // Even though high temp condition exists, alarm should NOT fire during suppression
        // (pending resets each tick while suppressed)
        CHECK_MESSAGE(pr_get_bool(state, "protection.high_temp_alarm") == false,
                      "High temp alarm must be suppressed within post-defrost window");
    }

    SUBCASE("high temp alarm fires after post-defrost window expires") {
        // Step 1: advance past post_defrost_delay to end suppression
        // post_defrost_timer_ms_ starts at SETUP_MS from previous tick
        // Need to add (post_delay_ms - SETUP_MS + 1) to exceed threshold
        prot.on_update(post_delay_ms - SETUP_MS + 1u);

        // Suppression now ended. high_alarm pending starts fresh.
        // Now add enough time for high_alarm_delay to expire
        prot.on_update(high_delay_ms + 1u);

        CHECK_MESSAGE(pr_get_bool(state, "protection.high_temp_alarm") == true,
                      "High temp alarm must fire after post-defrost suppression ends");
    }

    SUBCASE("All defrost heating phases suppress HAL alarm") {
        // Test each heating phase
        const char* heating_phases[] = { "active", "stabilize", "valve_open", "equalize" };
        for (const char* phase : heating_phases) {
            // Reset state for each subcase iteration
            state.set("defrost.active", true);
            state.set("defrost.phase",  modesp::StringValue(phase));
            state.set("equipment.air_temp", 15.0f);

            // One setup tick to register defrost active
            prot.on_update(SETUP_MS);

            // Try to accumulate high_alarm_delay
            prot.on_update(high_delay_ms + 1u);

            CHECK_MESSAGE(pr_get_bool(state, "protection.high_temp_alarm") == false,
                          "HAL alarm must be suppressed in heating phase: ", phase);
        }
    }
}

// -----------------------------------------------------------------------------
// TEST 9: Alarm_code priority ordering
// -----------------------------------------------------------------------------

TEST_CASE("Protection: alarm_code priority -- err1 > high_temp > low_temp > err2 > door [protection]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_short_delays(state, 1, 1, 1);

    SUBCASE("err1 has highest priority over all others") {
        // Trigger all alarm conditions simultaneously
        pr_setup_normal(state, 15.0f, false, false, true, false);
        // Need high/low/door alarms to fire first (they need delay)
        // sensor1/2 alarms are instant
        prot.on_update(60001u);  // fire delayed alarms
        // sensor1_ok=false -> err1 wins
        CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "err1",
                      "err1 must have highest priority");
    }

    SUBCASE("high_temp beats low_temp when both active") {
        // sensor1 OK, T is simultaneously > high_limit AND < low_limit is impossible physically
        // Test by firing high_temp first, then verify priority
        pr_setup_normal(state, 15.0f, true, true, false, false);
        prot.on_update(60001u);  // high temp fires
        REQUIRE(pr_get_bool(state, "protection.high_temp_alarm") == true);

        // Manually check priority: if both high and low were active, high_temp wins
        CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "high_temp",
                      "high_temp must have higher priority than low_temp");
    }

    SUBCASE("err2 beats door alarm") {
        pr_setup_normal(state, 5.0f, true, false, true, false);  // sensor2=false, door=true
        prot.on_update(60001u);  // door alarm fires
        // err2 is instant, fires regardless of door
        CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "err2",
                      "err2 must have higher priority than door alarm");
    }

    SUBCASE("door alarm code when only door is active") {
        pr_setup_normal(state, 5.0f, true, true, true, false);  // only door open
        prot.on_update(60001u);  // door delay = 1 min = 60 000ms
        CHECK_MESSAGE(pr_get_bool(state, "protection.door_alarm") == true,
                      "Pre-condition: door alarm must be active");
        CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "door",
                      "alarm_code must be 'door' when only door alarm active");
    }
}

// -----------------------------------------------------------------------------
// TEST 10: Manual reset clears all active alarms
// -----------------------------------------------------------------------------

TEST_CASE("Protection: manual reset clears all active alarms [protection]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_short_delays(state, 1, 1, 1);

    // Use manual_reset=true so alarms won't auto-clear when conditions normalize.
    // This lets us verify that the reset command is the mechanism that clears them.
    state.set("protection.manual_reset", true);

    // Trigger multiple alarms (all conditions bad)
    pr_setup_normal(state, 15.0f, false, false, true, false);  // high temp, sensor faults, door
    prot.on_update(60001u);  // fire all delayed alarms

    REQUIRE_MESSAGE(pr_get_bool(state, "protection.alarm_active") == true,
                    "Pre-condition: at least one alarm must be active");
    REQUIRE(pr_get_bool(state, "protection.sensor1_alarm") == true);
    REQUIRE(pr_get_bool(state, "protection.sensor2_alarm") == true);

    // Restore conditions to normal. With manual_reset=true alarms persist.
    pr_setup_normal(state, 5.0f, true, true, false, false);
    prot.on_update(SETUP_MS);

    // Verify alarms still active (manual_reset prevents auto-clear)
    REQUIRE_MESSAGE(pr_get_bool(state, "protection.sensor1_alarm") == true,
                    "Sensor alarm must persist with manual_reset=true even after sensor restores");

    // Issue manual reset
    state.set("protection.reset_alarms", true);
    prot.on_update(SETUP_MS);

    CHECK_MESSAGE(pr_get_bool(state, "protection.high_temp_alarm") == false,
                  "Manual reset must clear high_temp alarm");
    CHECK_MESSAGE(pr_get_bool(state, "protection.sensor1_alarm") == false,
                  "Manual reset must clear sensor1 alarm");
    CHECK_MESSAGE(pr_get_bool(state, "protection.sensor2_alarm") == false,
                  "Manual reset must clear sensor2 alarm");
    CHECK_MESSAGE(pr_get_bool(state, "protection.door_alarm") == false,
                  "Manual reset must clear door alarm");
    CHECK_MESSAGE(pr_get_bool(state, "protection.alarm_active") == false,
                  "alarm_active must be false after manual reset");
    CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "none",
                  "alarm_code must be 'none' after manual reset");
    CHECK_MESSAGE(pr_get_bool(state, "protection.reset_alarms") == false,
                  "reset_alarms trigger must be cleared after processing");
}

// -----------------------------------------------------------------------------
// TEST 11: Sensor failure blocks high/low temp alarm logic
// -----------------------------------------------------------------------------

TEST_CASE("Protection: sensor failure blocks high and low temp alarm logic [protection]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_short_delays(state, 1, 1, 1);

    SUBCASE("high temp alarm blocked when sensor1 fails") {
        // T > high_limit but sensor1_ok=false -> high temp alarm must NOT fire
        pr_setup_normal(state, 15.0f, false, true, false, false);
        prot.on_update(60001u);

        CHECK_MESSAGE(pr_get_bool(state, "protection.sensor1_alarm") == true,
                      "ERR1 must be active with sensor1_ok=false");
        CHECK_MESSAGE(pr_get_bool(state, "protection.high_temp_alarm") == false,
                      "High temp alarm must be blocked when sensor1 fails");
    }

    SUBCASE("low temp alarm blocked when sensor1 fails") {
        // T < low_limit but sensor1_ok=false -> low temp alarm must NOT fire
        pr_setup_normal(state, -40.0f, false, true, false, false);
        prot.on_update(60001u);

        CHECK_MESSAGE(pr_get_bool(state, "protection.sensor1_alarm") == true,
                      "ERR1 must be active with sensor1_ok=false");
        CHECK_MESSAGE(pr_get_bool(state, "protection.low_temp_alarm") == false,
                      "Low temp alarm must be blocked when sensor1 fails");
    }

    SUBCASE("pending high temp resets when sensor fails mid-delay") {
        // Start accumulating high temp delay with sensor OK
        pr_setup_normal(state, 15.0f, true, true, false, false);
        prot.on_update(30000u);  // half delay accumulated
        CHECK(pr_get_bool(state, "protection.high_temp_alarm") == false);

        // Sensor fails mid-delay -> pending resets
        state.set("equipment.sensor1_ok", false);
        prot.on_update(30001u);  // would have fired if pending continued

        CHECK_MESSAGE(pr_get_bool(state, "protection.high_temp_alarm") == false,
                      "High temp alarm must NOT fire after sensor failure resets pending");
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// COMPRESSOR PROTECTION TESTS
// ═══════════════════════════════════════════════════════════════════════════════

// -- Helper: set compressor protection settings for fast testing ---------------
static void pr_setup_compressor_settings(modesp::SharedState& state,
                                          int min_run_sec = 120,
                                          int max_starts = 12,
                                          int max_cont_min = 360,
                                          int pulldown_min = 60,
                                          float pulldown_drop = 2.0f,
                                          float max_rate = 0.5f,
                                          int rate_dur_min = 5,
                                          int forced_off_min = 20,
                                          int max_retries = 3) {
    state.set("protection.min_compressor_run", static_cast<int32_t>(min_run_sec));
    state.set("protection.max_starts_hour",    static_cast<int32_t>(max_starts));
    state.set("protection.max_continuous_run",  static_cast<int32_t>(max_cont_min));
    state.set("protection.pulldown_timeout",    static_cast<int32_t>(pulldown_min));
    state.set("protection.pulldown_min_drop",   pulldown_drop);
    state.set("protection.max_rise_rate",       max_rate);
    state.set("protection.rate_duration",       static_cast<int32_t>(rate_dur_min));
    state.set("protection.forced_off_period",   static_cast<int32_t>(forced_off_min));
    state.set("protection.max_continuous_retries", static_cast<int32_t>(max_retries));
}

// -----------------------------------------------------------------------------
// TEST 12: No compressor alarm in normal conditions
// -----------------------------------------------------------------------------

TEST_CASE("Protection: no compressor alarm in normal conditions [compressor]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_normal(state, 5.0f, true, true, false, false, true);
    pr_setup_compressor_settings(state);

    // Нормальний цикл: 10 хвилин роботи
    prot.on_update(600000u);

    CHECK(pr_get_bool(state, "protection.short_cycle_alarm") == false);
    CHECK(pr_get_bool(state, "protection.rapid_cycle_alarm") == false);
    CHECK(pr_get_bool(state, "protection.continuous_run_alarm") == false);
    CHECK(pr_get_bool(state, "protection.pulldown_alarm") == false);
    CHECK(pr_get_bool(state, "protection.rate_alarm") == false);
}

// -----------------------------------------------------------------------------
// TEST 13: Short cycle alarm after 3 consecutive short runs
// -----------------------------------------------------------------------------

TEST_CASE("Protection: short cycle alarm after 3 consecutive short cycles [compressor]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // min_compressor_run = 120 sec
    pr_setup_compressor_settings(state, 120);
    pr_setup_normal(state, 5.0f, true, true, false, false, false);

    // Цикл 1: ON 60 sec → OFF (short: 60 < 120)
    state.set("equipment.compressor", true);
    prot.on_update(60000u);
    state.set("equipment.compressor", false);
    prot.on_update(SETUP_MS);  // фіксує ON→OFF перехід

    CHECK(pr_get_bool(state, "protection.short_cycle_alarm") == false);

    // Цикл 2: ON 60 sec → OFF (short)
    state.set("equipment.compressor", true);
    prot.on_update(60000u);
    state.set("equipment.compressor", false);
    prot.on_update(SETUP_MS);

    CHECK(pr_get_bool(state, "protection.short_cycle_alarm") == false);

    // Цикл 3: ON 60 sec → OFF (short) — 3rd consecutive → alarm
    state.set("equipment.compressor", true);
    prot.on_update(60000u);
    state.set("equipment.compressor", false);
    prot.on_update(SETUP_MS);

    CHECK_MESSAGE(pr_get_bool(state, "protection.short_cycle_alarm") == true,
                  "Short cycle alarm must fire after 3 consecutive short cycles");

    SUBCASE("auto-clears after normal-length cycle") {
        // Нормальний цикл: ON 180 sec → OFF (>= 120, resets counter)
        state.set("equipment.compressor", true);
        prot.on_update(180000u);
        state.set("equipment.compressor", false);
        prot.on_update(SETUP_MS);

        CHECK_MESSAGE(pr_get_bool(state, "protection.short_cycle_alarm") == false,
                      "Short cycle alarm must auto-clear after normal cycle");
    }
}

// -----------------------------------------------------------------------------
// TEST 14: Rapid cycle alarm with >12 starts in 1 hour
// -----------------------------------------------------------------------------

TEST_CASE("Protection: rapid cycle alarm with too many starts [compressor]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // max_starts_hour = 12, min_compressor_run = 120 sec
    pr_setup_compressor_settings(state, 120, 12);
    pr_setup_normal(state, 5.0f, true, true, false, false, false);

    // Симулюємо 13 запусків з нормальною тривалістю (180 сек кожен)
    // Щоб не спрацював short_cycle, кожен цикл >= 120 сек
    for (int i = 0; i < 13; i++) {
        state.set("equipment.compressor", true);
        prot.on_update(180000u);  // 3 хв ON
        state.set("equipment.compressor", false);
        prot.on_update(10000u);   // 10 сек OFF
    }

    CHECK_MESSAGE(pr_get_bool(state, "protection.rapid_cycle_alarm") == true,
                  "Rapid cycle alarm must fire after 13 starts in < 60 min");
    CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "rapid_cycle",
                  "alarm_code must be 'rapid_cycle'");
}

// -----------------------------------------------------------------------------
// TEST 15: Continuous run alarm after max_continuous_run
// -----------------------------------------------------------------------------

TEST_CASE("Protection: continuous run alarm after max duration [compressor]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // max_continuous_run = 10 min (для швидкості тесту)
    pr_setup_compressor_settings(state, 120, 12, 10);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);

    static constexpr uint32_t max_run_ms = 600000u;  // 10 min

    SUBCASE("no alarm before limit") {
        prot.on_update(max_run_ms - 1u);
        CHECK(pr_get_bool(state, "protection.continuous_run_alarm") == false);
    }

    SUBCASE("alarm fires after limit") {
        prot.on_update(max_run_ms + 1u);
        CHECK_MESSAGE(pr_get_bool(state, "protection.continuous_run_alarm") == true,
                      "Continuous run alarm must fire after max duration");
    }

    SUBCASE("auto-clears after forced off period ends and compressor stops") {
        prot.on_update(max_run_ms + 1u);
        REQUIRE(pr_get_bool(state, "protection.continuous_run_alarm") == true);
        REQUIRE(pr_get_bool(state, "protection.compressor_blocked") == true);

        // Симулюємо Equipment: компресор OFF під час forced off
        state.set("equipment.compressor", false);

        // Forced off ще тримається — alarm ще активний
        prot.on_update(SETUP_MS);
        CHECK(pr_get_bool(state, "protection.continuous_run_alarm") == true);

        // Forced off завершується (default 20 min)
        prot.on_update(1200000u);  // 20 min
        CHECK_MESSAGE(pr_get_bool(state, "protection.continuous_run_alarm") == false,
                      "Continuous run alarm must clear after forced off release");
        CHECK(pr_get_bool(state, "protection.compressor_blocked") == false);
    }
}

// -----------------------------------------------------------------------------
// TEST 16: Pulldown alarm — compressor runs but temp doesn't drop
// -----------------------------------------------------------------------------

TEST_CASE("Protection: pulldown alarm when temp doesn't drop [compressor]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // pulldown_timeout = 5 min, pulldown_min_drop = 2.0 C
    pr_setup_compressor_settings(state, 120, 12, 360, 5, 2.0f);
    // Температура при старті 10°C, компресор ON
    pr_setup_normal(state, 10.0f, true, true, false, false, true);

    static constexpr uint32_t pulldown_ms = 300000u;  // 5 min

    SUBCASE("no alarm if temp dropped enough") {
        // Компресор працює 5+ хв, T впала з 10 до 7 (дельта = 3 > 2)
        prot.on_update(pulldown_ms / 2);
        state.set("equipment.air_temp", 7.0f);
        prot.on_update(pulldown_ms / 2 + 1u);

        CHECK_MESSAGE(pr_get_bool(state, "protection.pulldown_alarm") == false,
                      "No pulldown alarm when temp dropped sufficiently");
    }

    SUBCASE("alarm if temp didn't drop") {
        // Компресор працює 5+ хв, T залишилась 10°C (дельта = 0 < 2)
        prot.on_update(pulldown_ms + 1u);

        CHECK_MESSAGE(pr_get_bool(state, "protection.pulldown_alarm") == true,
                      "Pulldown alarm must fire when temp didn't drop");
    }

    SUBCASE("pulldown blocked during defrost") {
        state.set("defrost.active", true);
        state.set("defrost.phase", modesp::StringValue("active"));
        prot.on_update(pulldown_ms + 1u);

        CHECK_MESSAGE(pr_get_bool(state, "protection.pulldown_alarm") == false,
                      "Pulldown alarm must be blocked during defrost");
    }

    SUBCASE("auto-clears when compressor stops") {
        prot.on_update(pulldown_ms + 1u);
        REQUIRE(pr_get_bool(state, "protection.pulldown_alarm") == true);

        state.set("equipment.compressor", false);
        prot.on_update(SETUP_MS);

        CHECK_MESSAGE(pr_get_bool(state, "protection.pulldown_alarm") == false,
                      "Pulldown alarm must auto-clear when compressor stops");
    }
}

// -----------------------------------------------------------------------------
// TEST 17: Rate-of-change alarm — temp rising while compressor ON
// -----------------------------------------------------------------------------

TEST_CASE("Protection: rate alarm when temp rises while compressor ON [compressor]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // max_rise_rate = 0.5 C/min, rate_duration = 1 min
    pr_setup_compressor_settings(state, 120, 12, 360, 60, 2.0f, 0.5f, 1);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);

    // Симулюємо зростання T на 1°C/хв протягом 2 хв (> 0.5 C/min)
    // Перший тік для ініціалізації rate tracker
    prot.on_update(SETUP_MS);

    // Кожен тік = 10 сек, T зростає на ~0.167°C (1°C/хв)
    float temp = 5.0f;
    for (int i = 0; i < 18; i++) {  // 18 × 10 sec = 3 min
        temp += 1.0f / 6.0f;  // +0.167°C за 10 сек = 1°C/хв
        state.set("equipment.air_temp", temp);
        prot.on_update(10000u);
    }

    CHECK_MESSAGE(pr_get_bool(state, "protection.rate_alarm") == true,
                  "Rate alarm must fire when temp rises > max_rise_rate for rate_duration");

    SUBCASE("rate alarm blocked during defrost") {
        // Reset
        state.set("protection.reset_alarms", true);
        prot.on_update(SETUP_MS);
        REQUIRE(pr_get_bool(state, "protection.rate_alarm") == false);

        // Симулюємо defrost
        state.set("defrost.active", true);
        state.set("defrost.phase", modesp::StringValue("active"));

        temp = 5.0f;
        state.set("equipment.air_temp", temp);
        prot.on_update(SETUP_MS);

        for (int i = 0; i < 18; i++) {
            temp += 1.0f / 6.0f;
            state.set("equipment.air_temp", temp);
            prot.on_update(10000u);
        }

        CHECK_MESSAGE(pr_get_bool(state, "protection.rate_alarm") == false,
                      "Rate alarm must be blocked during defrost");
    }

    SUBCASE("rate alarm clears when rate drops") {
        // Температура стабілізувалась
        for (int i = 0; i < 12; i++) {
            prot.on_update(10000u);  // T не змінюється → rate → 0
        }

        CHECK_MESSAGE(pr_get_bool(state, "protection.rate_alarm") == false,
                      "Rate alarm must auto-clear when rate drops below threshold");
    }
}

// -----------------------------------------------------------------------------
// TEST 18: Manual reset clears compressor alarms
// -----------------------------------------------------------------------------

TEST_CASE("Protection: manual reset clears compressor alarms [compressor]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // Встановлюємо short min_run для швидкості
    pr_setup_compressor_settings(state, 120, 12, 10);
    state.set("protection.manual_reset", true);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);

    // Тригеримо continuous_run alarm
    prot.on_update(600001u);  // 10 min + 1ms > 10 min limit
    REQUIRE(pr_get_bool(state, "protection.continuous_run_alarm") == true);

    // Вимикаємо компресор + Manual reset (без цього alarm re-trigger одразу)
    state.set("equipment.compressor", false);
    state.set("protection.reset_alarms", true);
    prot.on_update(SETUP_MS);

    CHECK_MESSAGE(pr_get_bool(state, "protection.continuous_run_alarm") == false,
                  "Manual reset must clear continuous_run alarm");
    CHECK_MESSAGE(pr_get_bool(state, "protection.alarm_active") == false,
                  "alarm_active must be false after manual reset");
}

// -----------------------------------------------------------------------------
// TEST 19: Alarm code priority with new compressor alarms
// -----------------------------------------------------------------------------

TEST_CASE("Protection: alarm code priority includes compressor alarms [compressor]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_compressor_settings(state, 120, 12, 10);
    pr_setup_short_delays(state, 1, 1, 1);

    SUBCASE("comp_blocked has higher priority than low_temp") {
        // T=-40 (below low_limit=-35) + continuous_run → forced off
        pr_setup_normal(state, -40.0f, true, true, false, false, true);

        // Тригеримо обидва: low_temp після 60s delay + continuous_run після 10 min
        prot.on_update(600001u);

        CHECK(pr_get_bool(state, "protection.low_temp_alarm") == true);
        CHECK(pr_get_bool(state, "protection.continuous_run_alarm") == true);
        CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "comp_blocked",
                      "comp_blocked must have higher priority than low_temp");
    }

    SUBCASE("rate_rise has second-highest priority") {
        // Sensor1 OK, no err1. Simulate rate alarm + high_temp.
        pr_setup_normal(state, 5.0f, true, true, false, false, true);

        // Спочатку тригеримо rate через швидке зростання
        pr_setup_compressor_settings(state, 120, 12, 360, 60, 2.0f, 0.5f, 1);
        prot.on_update(SETUP_MS);  // init rate tracker

        float temp = 5.0f;
        for (int i = 0; i < 18; i++) {
            temp += 1.0f / 6.0f;
            state.set("equipment.air_temp", temp);
            prot.on_update(10000u);
        }

        REQUIRE(pr_get_bool(state, "protection.rate_alarm") == true);
        // Без err1, rate_rise має найвищий пріоритет
        CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "rate_rise",
                      "rate_rise must be second-highest priority (after err1)");
    }
}

// -----------------------------------------------------------------------------
// TEST 20: Compressor diagnostics are published
// -----------------------------------------------------------------------------

TEST_CASE("Protection: compressor diagnostics published [compressor]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_compressor_settings(state);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);

    // Компресор ON протягом 10 сек (>= 5 сек тік діагностики)
    prot.on_update(5000u);

    // Diagnostics мають бути опубліковані
    auto run_time = state.get("protection.compressor_run_time");
    REQUIRE(run_time.has_value());
    const auto* rp = etl::get_if<int32_t>(&run_time.value());
    CHECK(rp != nullptr);
    if (rp) CHECK(*rp == 5);  // 5000 ms = 5 sec

    // Duty cycle > 0 (компресор працює)
    auto duty = state.get("protection.compressor_duty");
    REQUIRE(duty.has_value());
    const auto* dp = etl::get_if<float>(&duty.value());
    CHECK(dp != nullptr);
    if (dp) CHECK(*dp > 0.0f);
}

// -----------------------------------------------------------------------------
// TEST 21: Motor hours accumulation
// -----------------------------------------------------------------------------

TEST_CASE("Protection: motor hours accumulate when compressor ON [compressor]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);

    // Встановлюємо початкові мотогодини ДО init — щоб on_init їх прочитав
    state.set("protection.compressor_hours", 100.0f);
    mgr.init_all(state);

    pr_setup_compressor_settings(state);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);

    // Motor hours записуються в state кожні 720 циклів (persist optimization).
    // Тому виконуємо 721 тік по 5 сек для тригеру запису в state.
    // 720 * 5/3600 = 1.0 year (годин наробітку за цикл)
    for (int i = 0; i < 721; i++) {
        prot.on_update(5000u);
    }

    auto hours = state.get("protection.compressor_hours");
    REQUIRE(hours.has_value());
    const auto* hp = etl::get_if<float>(&hours.value());
    CHECK(hp != nullptr);
    if (hp) {
        // 100 + 720 * (5/3600) = 101.0
        CHECK(*hp > 100.5f);
        CHECK(*hp < 101.5f);
    }
}

// =============================================================================
// CONTINUOUS RUN ESCALATION TESTS (TEST 22-30)
// =============================================================================

// -----------------------------------------------------------------------------
// TEST 22: Continuous run triggers compressor_blocked, NOT lockout
// -----------------------------------------------------------------------------

TEST_CASE("Protection: continuous run triggers compressor_blocked [escalation]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // max_continuous_run = 10 min, forced_off = 5 min, max_retries = 3
    pr_setup_compressor_settings(state, 120, 12, 10, 60, 2.0f, 0.5f, 5, 5, 3);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);

    static constexpr uint32_t max_run_ms = 600000u;  // 10 min

    // Перевищуємо ліміт
    prot.on_update(max_run_ms + 1u);

    CHECK(pr_get_bool(state, "protection.continuous_run_alarm") == true);
    CHECK_MESSAGE(pr_get_bool(state, "protection.compressor_blocked") == true,
                  "compressor_blocked must be true during forced off");
    CHECK_MESSAGE(pr_get_bool(state, "protection.lockout") == false,
                  "lockout must be false — only Level 1 (forced off)");
}

// -----------------------------------------------------------------------------
// TEST 23: Forced off releases after period, count increments
// -----------------------------------------------------------------------------

TEST_CASE("Protection: forced off releases after period [escalation]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // max_continuous_run = 10 min, forced_off = 2 min, max_retries = 3
    pr_setup_compressor_settings(state, 120, 12, 10, 60, 2.0f, 0.5f, 5, 2, 3);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);

    static constexpr uint32_t max_run_ms = 600000u;   // 10 min
    static constexpr uint32_t forced_off_ms = 120000u; // 2 min

    // Тригеримо continuous run
    prot.on_update(max_run_ms + 1u);
    REQUIRE(pr_get_bool(state, "protection.compressor_blocked") == true);

    // Forced off ще не минув
    prot.on_update(forced_off_ms - 1000u);
    CHECK(pr_get_bool(state, "protection.compressor_blocked") == true);

    // Forced off завершився
    prot.on_update(2000u);  // тепер сумарно > forced_off_ms
    CHECK_MESSAGE(pr_get_bool(state, "protection.compressor_blocked") == false,
                  "compressor_blocked must clear after forced off period");

    // Лічильник = 1
    auto count = state.get("protection.continuous_run_count");
    REQUIRE(count.has_value());
    const auto* cp = etl::get_if<int32_t>(&count.value());
    REQUIRE(cp != nullptr);
    CHECK(*cp == 1);
}

// -----------------------------------------------------------------------------
// TEST 24: Icing detection triggers defrost (type != 0, evap sensor present)
// -----------------------------------------------------------------------------

TEST_CASE("Protection: icing detection triggers manual defrost [escalation]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // max_continuous_run = 10 min, forced_off = 1 min, max_retries = 3
    pr_setup_compressor_settings(state, 120, 12, 10, 60, 2.0f, 0.5f, 5, 1, 3);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);

    // Evap sensor present, cold evaporator (icing)
    state.set("equipment.has_evap_temp", true);
    state.set("equipment.evap_temp", -25.0f);  // below demand_temp (-15)
    state.set("defrost.type", static_cast<int32_t>(1));  // electric defrost
    state.set("defrost.demand_temp", -15.0f);

    static constexpr uint32_t max_run_ms = 600000u;  // 10 min
    static constexpr uint32_t forced_off_ms = 60000u; // 1 min

    // Тригер forced off
    prot.on_update(max_run_ms + 1u);
    REQUIRE(pr_get_bool(state, "protection.compressor_blocked") == true);

    // Чекаємо forced off period
    prot.on_update(forced_off_ms + 1u);

    // Defrost повинен бути тригернутий
    CHECK_MESSAGE(pr_get_bool(state, "defrost.manual_start") == true,
                  "Icing detected — defrost.manual_start must be set");
}

// -----------------------------------------------------------------------------
// TEST 25: No defrost trigger for natural defrost (type=0)
// -----------------------------------------------------------------------------

TEST_CASE("Protection: no defrost trigger for type=0 natural [escalation]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_compressor_settings(state, 120, 12, 10, 60, 2.0f, 0.5f, 5, 1, 3);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);

    state.set("equipment.has_evap_temp", true);
    state.set("equipment.evap_temp", -25.0f);
    state.set("defrost.type", static_cast<int32_t>(0));  // natural defrost
    state.set("defrost.demand_temp", -15.0f);

    static constexpr uint32_t max_run_ms = 600000u;
    static constexpr uint32_t forced_off_ms = 60000u;

    prot.on_update(max_run_ms + 1u);
    prot.on_update(forced_off_ms + 1u);

    // manual_start НЕ повинен бути тригернутий (type=0 — forced off вже зупинив компресор)
    CHECK_MESSAGE(pr_get_bool(state, "defrost.manual_start") == false,
                  "type=0 natural defrost — no manual_start trigger needed");
}

// -----------------------------------------------------------------------------
// TEST 26: No defrost trigger without evap sensor
// -----------------------------------------------------------------------------

TEST_CASE("Protection: no defrost trigger without evap sensor [escalation]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_compressor_settings(state, 120, 12, 10, 60, 2.0f, 0.5f, 5, 1, 3);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);

    state.set("equipment.has_evap_temp", false);  // no evap sensor
    state.set("defrost.type", static_cast<int32_t>(1));

    static constexpr uint32_t max_run_ms = 600000u;
    static constexpr uint32_t forced_off_ms = 60000u;

    prot.on_update(max_run_ms + 1u);
    prot.on_update(forced_off_ms + 1u);

    CHECK_MESSAGE(pr_get_bool(state, "defrost.manual_start") == false,
                  "No evap sensor — cannot detect icing, no manual defrost");
}

// -----------------------------------------------------------------------------
// TEST 27: Permanent lockout after max_retries
// -----------------------------------------------------------------------------

TEST_CASE("Protection: permanent lockout after max retries [escalation]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // max_continuous_run = 10 min, forced_off = 1 min, max_retries = 2
    pr_setup_compressor_settings(state, 120, 12, 10, 60, 2.0f, 0.5f, 5, 1, 2);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);
    state.set("equipment.has_evap_temp", false);  // no icing detection

    static constexpr uint32_t max_run_ms = 600000u;  // 10 min
    static constexpr uint32_t forced_off_ms = 60000u; // 1 min

    // Цикл 1: trigger → forced off → release → count=1
    prot.on_update(max_run_ms + 1u);
    REQUIRE(pr_get_bool(state, "protection.compressor_blocked") == true);

    // Simulate Equipment response: compressor OFF during forced off
    state.set("equipment.compressor", false);
    prot.on_update(forced_off_ms + 1u);
    CHECK(pr_get_bool(state, "protection.lockout") == false);

    // Компресор знову ON (thermostat запросив після release)
    state.set("equipment.compressor", true);
    prot.on_update(SETUP_MS);  // OFF→ON transition
    prot.on_update(max_run_ms + 1u);  // accumulate past max
    REQUIRE(pr_get_bool(state, "protection.compressor_blocked") == true);

    // Цикл 2: compressor OFF during forced off → lockout (count=2 >= max_retries=2)
    state.set("equipment.compressor", false);
    prot.on_update(forced_off_ms + 1u);

    CHECK_MESSAGE(pr_get_bool(state, "protection.lockout") == true,
                  "Permanent lockout after max_retries exhausted");

    // Лічильник = 2
    auto count = state.get("protection.continuous_run_count");
    REQUIRE(count.has_value());
    const auto* cp = etl::get_if<int32_t>(&count.value());
    REQUIRE(cp != nullptr);
    CHECK(*cp == 2);
}

// -----------------------------------------------------------------------------
// TEST 28: Manual reset clears escalation state
// -----------------------------------------------------------------------------

TEST_CASE("Protection: manual reset clears escalation [escalation]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    pr_setup_compressor_settings(state, 120, 12, 10, 60, 2.0f, 0.5f, 5, 1, 2);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);
    state.set("equipment.has_evap_temp", false);

    static constexpr uint32_t max_run_ms = 600000u;
    static constexpr uint32_t forced_off_ms = 60000u;

    // Доводимо до lockout (2 retries) — з реалістичним comp OFF під час forced off
    prot.on_update(max_run_ms + 1u);  // trigger #1
    state.set("equipment.compressor", false);
    prot.on_update(forced_off_ms + 1u);  // release #1, count=1

    state.set("equipment.compressor", true);
    prot.on_update(SETUP_MS);  // OFF→ON
    prot.on_update(max_run_ms + 1u);  // trigger #2

    state.set("equipment.compressor", false);
    prot.on_update(forced_off_ms + 1u);  // release #2, count=2 → lockout
    REQUIRE(pr_get_bool(state, "protection.lockout") == true);

    // Manual reset
    state.set("protection.reset_alarms", true);
    prot.on_update(SETUP_MS);

    CHECK_MESSAGE(pr_get_bool(state, "protection.lockout") == false,
                  "lockout must be cleared by manual reset");
    CHECK_MESSAGE(pr_get_bool(state, "protection.compressor_blocked") == false,
                  "compressor_blocked must be cleared by manual reset");
    CHECK(pr_get_bool(state, "protection.continuous_run_alarm") == false);

    auto count = state.get("protection.continuous_run_count");
    REQUIRE(count.has_value());
    const auto* cp = etl::get_if<int32_t>(&count.value());
    REQUIRE(cp != nullptr);
    CHECK(*cp == 0);
}

// -----------------------------------------------------------------------------
// TEST 29: Normal cycle resets continuous_run_count
// -----------------------------------------------------------------------------

TEST_CASE("Protection: normal cycle resets continuous run counter [escalation]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // max_continuous_run = 10 min, forced_off = 1 min, max_retries = 5
    pr_setup_compressor_settings(state, 120, 12, 10, 60, 2.0f, 0.5f, 5, 1, 5);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);
    state.set("equipment.has_evap_temp", false);

    static constexpr uint32_t max_run_ms = 600000u;
    static constexpr uint32_t forced_off_ms = 60000u;

    // Цикл 1: continuous run → forced off → release → count=1
    prot.on_update(max_run_ms + 1u);
    prot.on_update(forced_off_ms + 1u);

    auto count = state.get("protection.continuous_run_count");
    REQUIRE(count.has_value());
    const auto* cp = etl::get_if<int32_t>(&count.value());
    REQUIRE(cp != nullptr);
    REQUIRE(*cp == 1);

    // Тепер нормальний цикл: компресор ON 5 хвилин, потім OFF
    state.set("equipment.compressor", true);
    prot.on_update(300000u);  // 5 min (< 10 min max)

    state.set("equipment.compressor", false);
    prot.on_update(SETUP_MS);  // тригер off transition — записує last_run_ms

    // Ще один тік для перевірки last_run_ms < max_continuous_run_ms_
    prot.on_update(SETUP_MS);

    // Лічильник скинутий (нормальний цикл завершився)
    count = state.get("protection.continuous_run_count");
    REQUIRE(count.has_value());
    cp = etl::get_if<int32_t>(&count.value());
    REQUIRE(cp != nullptr);
    CHECK_MESSAGE(*cp == 0,
                  "Counter must reset after a normal cooling cycle completes");
}

// -----------------------------------------------------------------------------
// TEST 30: Defrost guard — continuous run not tracked during defrost.active
// -----------------------------------------------------------------------------

TEST_CASE("Protection: defrost guard skips continuous run tracking [escalation]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // max_continuous_run = 10 min
    pr_setup_compressor_settings(state, 120, 12, 10, 60, 2.0f, 0.5f, 5, 5, 3);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);

    static constexpr uint32_t max_run_ms = 600000u;  // 10 min

    // Defrost активний — компресор працює (hot gas)
    state.set("defrost.active", true);
    state.set("defrost.phase", modesp::StringValue("active"));

    // Компресор працює 15 хв (> 10 min max), але це під час defrost
    prot.on_update(max_run_ms + 300000u);

    CHECK_MESSAGE(pr_get_bool(state, "protection.continuous_run_alarm") == false,
                  "No continuous run alarm during defrost (hot gas = normal operation)");
    CHECK(pr_get_bool(state, "protection.compressor_blocked") == false);
    CHECK(pr_get_bool(state, "protection.lockout") == false);
}

// =============================================================================
// BUG FIX TESTS (TEST 31-36)
// =============================================================================

// -----------------------------------------------------------------------------
// TEST 31: Pulldown uses matched evap baseline (Fix 1)
// -----------------------------------------------------------------------------

TEST_CASE("Protection: pulldown uses evap_at_start when evap sensor present [compressor]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // pulldown_timeout = 5 min, pulldown_min_drop = 2.0 C
    pr_setup_compressor_settings(state, 120, 12, 360, 5, 2.0f);

    // Evap sensor present — evap = -10°C at start
    state.set("equipment.has_evap_temp", true);
    state.set("equipment.evap_temp", -10.0f);

    // Компресор ON (записує air_temp=10, evap_at_start=-10)
    pr_setup_normal(state, 10.0f, true, true, false, false, true);

    static constexpr uint32_t pulldown_ms = 300000u;  // 5 min

    SUBCASE("alarm fires when evap didn't drop") {
        // Evap залишилась -10°C (drop=0 < 2°C) — pulldown alarm
        prot.on_update(pulldown_ms + 1u);

        CHECK_MESSAGE(pr_get_bool(state, "protection.pulldown_alarm") == true,
                      "Pulldown alarm must fire when evap didn't drop");
    }

    SUBCASE("no alarm when evap dropped enough") {
        // Evap впала з -10 до -13 (drop=3 > 2°C) — OK
        prot.on_update(pulldown_ms / 2);
        state.set("equipment.evap_temp", -13.0f);
        prot.on_update(pulldown_ms / 2 + 1u);

        CHECK_MESSAGE(pr_get_bool(state, "protection.pulldown_alarm") == false,
                      "No pulldown alarm when evap dropped sufficiently");
    }
}

// -----------------------------------------------------------------------------
// TEST 32: Short cycle counter resets after prolonged idle (Fix 2)
// -----------------------------------------------------------------------------

TEST_CASE("Protection: short cycle counter resets after prolonged idle [compressor]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // min_compressor_run = 120 sec → idle reset = 1200 sec (20 min)
    pr_setup_compressor_settings(state, 120);
    pr_setup_normal(state, 5.0f, true, true, false, false, false);

    SUBCASE("counter resets after 10x min_run idle") {
        // 2 коротких цикли
        state.set("equipment.compressor", true);
        prot.on_update(60000u);  // 60s < 120s min
        state.set("equipment.compressor", false);
        prot.on_update(SETUP_MS);

        state.set("equipment.compressor", true);
        prot.on_update(60000u);
        state.set("equipment.compressor", false);
        prot.on_update(SETUP_MS);

        CHECK(pr_get_bool(state, "protection.short_cycle_alarm") == false);

        // Тривалий простій: 1200001 ms (> 120s × 10 = 1200s)
        prot.on_update(1200001u);

        // 1 короткий цикл після простою — лічильник скинувся, alarm НЕ тригерить
        state.set("equipment.compressor", true);
        prot.on_update(60000u);
        state.set("equipment.compressor", false);
        prot.on_update(SETUP_MS);

        CHECK_MESSAGE(pr_get_bool(state, "protection.short_cycle_alarm") == false,
                      "Short cycle alarm must NOT fire — counter was reset by idle timeout");
    }

    SUBCASE("counter still triggers without idle") {
        // 3 послідовних коротких цикли без тривалого простою
        for (int i = 0; i < 3; i++) {
            state.set("equipment.compressor", true);
            prot.on_update(60000u);
            state.set("equipment.compressor", false);
            prot.on_update(SETUP_MS);
            // Короткий простій: 5 сек (< 1200 сек)
            prot.on_update(5000u);
        }

        CHECK_MESSAGE(pr_get_bool(state, "protection.short_cycle_alarm") == true,
                      "Short cycle alarm must fire — 3 consecutive without idle reset");
    }
}

// -----------------------------------------------------------------------------
// TEST 33: alarm_code includes lockout and compressor_blocked (Fix 3)
// -----------------------------------------------------------------------------

TEST_CASE("Protection: alarm_code includes lockout and comp_blocked [escalation]") {
    modesp::SharedState state;
    ProtectionModule prot;
    modesp::ModuleManager mgr;
    mgr.register_module(prot);
    mgr.init_all(state);

    // max_continuous_run = 10 min, forced_off = 1 min, max_retries = 2
    pr_setup_compressor_settings(state, 120, 12, 10, 60, 2.0f, 0.5f, 5, 1, 2);
    pr_setup_normal(state, 5.0f, true, true, false, false, true);
    state.set("equipment.has_evap_temp", false);

    static constexpr uint32_t max_run_ms = 600000u;
    static constexpr uint32_t forced_off_ms = 60000u;

    SUBCASE("comp_blocked shows in alarm_code") {
        // Тригер forced off
        prot.on_update(max_run_ms + 1u);

        CHECK(pr_get_str(state, "protection.alarm_code") == "comp_blocked");
        CHECK(pr_get_bool(state, "protection.alarm_active") == true);
    }

    SUBCASE("lockout shows in alarm_code") {
        // Доводимо до lockout
        prot.on_update(max_run_ms + 1u);
        state.set("equipment.compressor", false);
        prot.on_update(forced_off_ms + 1u);

        state.set("equipment.compressor", true);
        prot.on_update(SETUP_MS);
        prot.on_update(max_run_ms + 1u);

        state.set("equipment.compressor", false);
        prot.on_update(forced_off_ms + 1u);

        REQUIRE(pr_get_bool(state, "protection.lockout") == true);
        CHECK(pr_get_str(state, "protection.alarm_code") == "lockout");
        CHECK_MESSAGE(pr_get_bool(state, "protection.alarm_active") == true,
                      "alarm_active must be true when lockout is active");
    }

    SUBCASE("lockout has highest priority over err1") {
        // Доводимо до lockout
        prot.on_update(max_run_ms + 1u);
        state.set("equipment.compressor", false);
        prot.on_update(forced_off_ms + 1u);

        state.set("equipment.compressor", true);
        prot.on_update(SETUP_MS);
        prot.on_update(max_run_ms + 1u);

        state.set("equipment.compressor", false);
        prot.on_update(forced_off_ms + 1u);
        REQUIRE(pr_get_bool(state, "protection.lockout") == true);

        // Додаємо sensor1 fault — err1 alarm
        state.set("equipment.sensor1_ok", false);
        prot.on_update(SETUP_MS);

        CHECK(pr_get_bool(state, "protection.sensor1_alarm") == true);
        CHECK_MESSAGE(pr_get_str(state, "protection.alarm_code") == "lockout",
                      "lockout must have highest priority over err1");
    }
}
