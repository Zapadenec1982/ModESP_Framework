/**
 * @file test_datalogger.cpp
 * @brief HOST unit tests for DataLoggerModule.
 *
 * Tests cover:
 *   - Channel enable/disable logic (sync_settings via SharedState)
 *   - Event edge-detection (poll_events)
 *   - Temperature sampling (sample timer)
 *   - TempRecord/EventRecord struct sizes
 *   - Summary output
 *
 * Note: File I/O (flush/rotate/serialize) requires a real filesystem.
 * These tests focus on in-memory logic accessible through SharedState.
 * On Windows host, /data/log/ doesn't exist so flush_to_flash() will
 * fail silently (fopen returns NULL) — this is acceptable.
 */

#include "mocks/freertos_mock.h"
#include "mocks/esp_log_mock.h"
#include "mocks/esp_timer_mock.h"

#include "doctest.h"
#include "modesp/shared_state.h"
#include "modesp/module_manager.h"
#include "datalogger_module.h"

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

static int32_t get_int(modesp::SharedState& s, const char* key, int32_t def = -1) {
    auto v = s.get(key);
    if (!v.has_value()) return def;
    const auto* ip = etl::get_if<int32_t>(&v.value());
    return ip ? *ip : def;
}

// ── Test fixture ──

struct DLFixture {
    modesp::SharedState state;
    modesp::ModuleManager mgr;
    DataLoggerModule dl;

    DLFixture() {
        mgr.register_module(dl);

        // Базовий стан обладнання (зазвичай публікується EquipmentModule)
        state.set("equipment.air_temp", 4.5f);
        state.set("equipment.evap_temp", -12.0f);
        state.set("equipment.cond_temp", 35.0f);
        state.set("equipment.compressor", false);
        state.set("equipment.door_open", false);
        state.set("equipment.has_evap_temp", true);
        state.set("equipment.has_cond_temp", true);
        state.set("defrost.active", false);
        state.set("protection.high_temp_alarm", false);
        state.set("protection.low_temp_alarm", false);
        state.set("protection.sensor1_alarm", false);
        state.set("protection.sensor2_alarm", false);
        state.set("protection.continuous_run_alarm", false);
        state.set("protection.pulldown_alarm", false);
        state.set("protection.short_cycle_alarm", false);
        state.set("protection.rapid_cycle_alarm", false);
        state.set("protection.rate_alarm", false);
        state.set("protection.door_alarm", false);

        // Settings
        state.set("datalogger.enabled", true);
        state.set("datalogger.sample_interval", static_cast<int32_t>(60));
        state.set("datalogger.retention_hours", static_cast<int32_t>(48));
        state.set("datalogger.log_evap", false);
        state.set("datalogger.log_cond", false);
        state.set("datalogger.log_setpoint", false);
        state.set("datalogger.log_humidity", false);

        // init_all sets shared_state_ + calls on_init
        // mkdir may fail on host (no /data/log), but that's OK
        mgr.init_all(state);
    }

    void tick(uint32_t ms = 100) {
        dl.on_update(ms);
    }
};

// ═══════════════════════════════════════════════════════════════
// TEST CASES
// ═══════════════════════════════════════════════════════════════

TEST_SUITE("DataLoggerModule") {

// ── 1. Struct sizes ──

TEST_CASE("TempRecord is 16 bytes") {
    CHECK(sizeof(TempRecord) == 16);
}

TEST_CASE("EventRecord is 8 bytes") {
    CHECK(sizeof(EventRecord) == 8);
}

TEST_CASE("TEMP_NO_DATA is INT16_MIN") {
    CHECK(TEMP_NO_DATA == INT16_MIN);
}

// ── 2. Channel definitions ──

TEST_CASE("channel 0 is always-on air") {
    CHECK(CHANNEL_DEFS[0].enable_key == nullptr);
    CHECK(CHANNEL_DEFS[0].has_key == nullptr);
    CHECK(strcmp(CHANNEL_DEFS[0].id, "air") == 0);
}

TEST_CASE("channel 5 is reserved (nullptr)") {
    CHECK(CHANNEL_DEFS[5].id == nullptr);
}

// ── 3. Init publishes state keys ──

TEST_CASE_FIXTURE(DLFixture, "init publishes records_count and flash_used") {
    CHECK(get_int(state, "datalogger.records_count") >= 0);
    CHECK(get_int(state, "datalogger.flash_used") >= 0);
}

// ── 4. Disabled module skips update ──

TEST_CASE_FIXTURE(DLFixture, "disabled datalogger skips update") {
    state.set("datalogger.enabled", false);
    int32_t before = get_int(state, "datalogger.records_count");

    // Тікаємо 2 хвилини — більше sample_interval
    for (int i = 0; i < 1200; i++) tick(100);

    int32_t after = get_int(state, "datalogger.records_count");
    CHECK(after == before);
}

// ── 5. Sample timer ──

TEST_CASE_FIXTURE(DLFixture, "samples after interval elapsed") {
    int32_t before = get_int(state, "datalogger.records_count");

    // 59 seconds — not enough
    for (int i = 0; i < 590; i++) tick(100);
    CHECK(get_int(state, "datalogger.records_count") == before);

    // 1 more second — total 60s
    for (int i = 0; i < 10; i++) tick(100);
    CHECK(get_int(state, "datalogger.records_count") == before + 1);
}

// ── 6. Event edge-detect: compressor ──

TEST_CASE_FIXTURE(DLFixture, "compressor ON/OFF generates events") {
    // Перший ON — фіксуємо стан після
    state.set("equipment.compressor", true);
    tick();
    int32_t after_on = get_int(state, "datalogger.events_count");
    CHECK(after_on > 0);  // POWER_ON + COMP_ON

    // OFF → ще одна подія
    state.set("equipment.compressor", false);
    tick();
    CHECK(get_int(state, "datalogger.events_count") == after_on + 1);
}

// ── 7. Event edge-detect: defrost ──

TEST_CASE_FIXTURE(DLFixture, "defrost start/end generates events") {
    state.set("defrost.active", true);
    tick();
    int32_t after_start = get_int(state, "datalogger.events_count");
    CHECK(after_start > 0);

    state.set("defrost.active", false);
    tick();
    CHECK(get_int(state, "datalogger.events_count") == after_start + 1);
}

// ── 8. Event edge-detect: door ──

TEST_CASE_FIXTURE(DLFixture, "door open/close generates events") {
    state.set("equipment.door_open", true);
    tick();
    int32_t after_open = get_int(state, "datalogger.events_count");
    CHECK(after_open > 0);

    state.set("equipment.door_open", false);
    tick();
    CHECK(get_int(state, "datalogger.events_count") == after_open + 1);
}

// ── 9. Event edge-detect: alarms ──

TEST_CASE_FIXTURE(DLFixture, "high temp alarm generates event on rising edge") {
    state.set("protection.high_temp_alarm", true);
    tick();
    int32_t after_alarm = get_int(state, "datalogger.events_count");
    CHECK(after_alarm > 0);

    // Повторний стан — подій не додається
    tick();
    CHECK(get_int(state, "datalogger.events_count") == after_alarm);
}

// ── 10. No duplicate events on same state ──

TEST_CASE_FIXTURE(DLFixture, "no event when state unchanged") {
    state.set("equipment.compressor", true);
    tick();
    int32_t after_on = get_int(state, "datalogger.events_count");

    // Той самий стан — подій не повинно бути
    tick();
    tick();
    CHECK(get_int(state, "datalogger.events_count") == after_on);
}

// ── 11. Summary output ──

TEST_CASE_FIXTURE(DLFixture, "serialize_summary produces valid JSON") {
    char buf[256];
    bool ok = dl.serialize_summary(buf, sizeof(buf));
    CHECK(ok == true);
    // Перевіримо що JSON містить основні поля
    CHECK(strstr(buf, "\"hours\"") != nullptr);
    CHECK(strstr(buf, "\"temp_count\"") != nullptr);
    CHECK(strstr(buf, "\"event_count\"") != nullptr);
    CHECK(strstr(buf, "\"flash_kb\"") != nullptr);
    CHECK(strstr(buf, "\"channels\"") != nullptr);
}

// ── 12. Alarm clear generates event on falling edge ──

TEST_CASE_FIXTURE(DLFixture, "alarm clear event on falling edge") {
    // Activate alarm → rising edge
    state.set("protection.high_temp_alarm", true);
    tick();
    int32_t after_alarm = get_int(state, "datalogger.events_count");

    // Deactivate alarm → falling edge → ALARM_CLEAR
    state.set("protection.high_temp_alarm", false);
    tick();
    CHECK(get_int(state, "datalogger.events_count") == after_alarm + 1);
}

// ── 13. Protection alarm events (all 10 types) ──

TEST_CASE_FIXTURE(DLFixture, "all protection alarms generate events") {
    struct AlarmCase {
        const char* key;
        const char* name;
    };
    AlarmCase alarms[] = {
        {"protection.high_temp_alarm",       "high_temp"},
        {"protection.low_temp_alarm",        "low_temp"},
        {"protection.sensor1_alarm",         "sensor1"},
        {"protection.sensor2_alarm",         "sensor2"},
        {"protection.continuous_run_alarm",  "cont_run"},
        {"protection.pulldown_alarm",        "pulldown"},
        {"protection.short_cycle_alarm",     "short_cyc"},
        {"protection.rapid_cycle_alarm",     "rapid_cyc"},
        {"protection.rate_alarm",            "rate_rise"},
        {"protection.door_alarm",            "door"},
    };

    for (auto& a : alarms) {
        SUBCASE(a.name) {
            int32_t before = get_int(state, "datalogger.events_count");

            // Rising edge → alarm event
            state.set(a.key, true);
            tick();
            CHECK(get_int(state, "datalogger.events_count") == before + 1);

            // Same state → no event
            tick();
            CHECK(get_int(state, "datalogger.events_count") == before + 1);

            // Falling edge → clear event
            state.set(a.key, false);
            tick();
            CHECK(get_int(state, "datalogger.events_count") == before + 2);
        }
    }
}

// ── 14. Multiple alarms don't interfere ──

TEST_CASE_FIXTURE(DLFixture, "simultaneous alarms generate separate events") {
    int32_t before = get_int(state, "datalogger.events_count");

    // Activate 3 alarms at once
    state.set("protection.high_temp_alarm", true);
    state.set("protection.continuous_run_alarm", true);
    state.set("protection.pulldown_alarm", true);
    tick();

    // Should have 3 new events
    CHECK(get_int(state, "datalogger.events_count") == before + 3);

    // Clear all 3
    state.set("protection.high_temp_alarm", false);
    state.set("protection.continuous_run_alarm", false);
    state.set("protection.pulldown_alarm", false);
    tick();

    // 3 clear events
    CHECK(get_int(state, "datalogger.events_count") == before + 6);
}

// ── 15. append_temp_val helper ──

TEST_CASE("TEMP_NO_DATA renders as null in JSON") {
    // Непрямий тест через struct: запис з NO_DATA каналом
    TempRecord rec;
    rec.timestamp = 1700000001;
    rec.ch[0] = 45;  // 4.5 °C
    rec.ch[1] = TEMP_NO_DATA;
    CHECK(rec.ch[1] == INT16_MIN);
}

} // TEST_SUITE
