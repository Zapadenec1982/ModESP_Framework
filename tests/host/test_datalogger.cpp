/**
 * @file test_datalogger.cpp
 * @brief HOST unit tests for DataLoggerModule (manifest-driven).
 *
 * Tests cover:
 *   - TempRecord/EventRecord binary layout
 *   - Generated channel/event tables (datalogger_channels.h / datalogger_events.h)
 *   - Init: POWER_ON event + counter keys
 *   - Sample timer (datalogger.sample_interval)
 *   - Event edge-detect (poll_events, generated LOG_EVENTS)
 *   - Disabled module skips update
 *   - Summary output
 *
 * Note: File I/O (flush/rotate/serialize) requires a real filesystem.
 * These tests focus on in-memory logic accessible through SharedState.
 * On host /data/log may not exist so flush_to_flash() fails silently
 * (fopen returns NULL) — this is acceptable; tests use relative deltas.
 */

#include "mocks/freertos_mock.h"
#include "mocks/esp_log_mock.h"
#include "mocks/esp_timer_mock.h"

#include "doctest.h"
#include "modesp/shared_state.h"
#include "modesp/module_manager.h"
#include "datalogger_module.h"

#include <cstring>

// ── Helpers ──

static int32_t get_int(modesp::SharedState& s, const char* key, int32_t def = -1) {
    auto v = s.get(key);
    if (!v.has_value()) return def;
    const auto* ip = etl::get_if<int32_t>(&v.value());
    return ip ? *ip : def;
}

// ── Test fixture ──
//
// Generated tables (from module manifests):
//   LOG_CHANNELS[0] = equipment.air_temp        (always-on)
//   LOG_CHANNELS[1] = simple_thermo.temperature (always-on)
//   LOG_EVENTS[0]   = simple_thermo.output, EdgeType::BOTH (id 30/31)

struct DLFixture {
    modesp::SharedState state;
    modesp::ModuleManager mgr;
    DataLoggerModule dl;

    int32_t records_at_init = 0;
    int32_t events_at_init  = 0;

    DLFixture() {
        mgr.register_module(dl);

        // Стан каналів/подій (зазвичай публікують equipment + simple_thermo)
        state.set("equipment.air_temp", 4.5f);
        state.set("simple_thermo.temperature", 21.0f);
        state.set("simple_thermo.output", false);

        // Settings — sample_interval=1s для швидких тестів
        state.set("datalogger.enabled", true);
        state.set("datalogger.sample_interval", static_cast<int32_t>(1));
        state.set("datalogger.retention_hours", static_cast<int32_t>(48));

        // init_all sets shared_state_ + calls on_init
        // mkdir may fail on host (no /data/log), but that's OK
        mgr.init_all(state);

        // Лічильники після init можуть включати записи з файлів попередніх
        // запусків — тести працюють з відносними дельтами.
        records_at_init = get_int(state, "datalogger.records_count");
        events_at_init  = get_int(state, "datalogger.events_count");
    }

    void tick(uint32_t ms = 100) { dl.on_update(ms); }
};

// ═══════════════════════════════════════════════════════════════
// TEST CASES
// ═══════════════════════════════════════════════════════════════

TEST_SUITE("DataLogger") {

// ── 1. Binary layout ──

TEST_CASE("DataLogger: TempRecord is 16 bytes") {
    CHECK(sizeof(TempRecord) == 16);
}

TEST_CASE("DataLogger: EventRecord is 8 bytes") {
    CHECK(sizeof(EventRecord) == 8);
}

TEST_CASE("DataLogger: TEMP_NO_DATA is INT16_MIN") {
    CHECK(TEMP_NO_DATA == INT16_MIN);
}

TEST_CASE("DataLogger: TEMP_NO_DATA channel value preserved in record") {
    TempRecord rec;
    rec.timestamp = 1700000001;
    rec.ch[0] = 45;  // 4.5 °C
    rec.ch[1] = TEMP_NO_DATA;
    CHECK(rec.ch[1] == INT16_MIN);
}

// ── 2. Generated tables ──

TEST_CASE("DataLogger: generated channel table fits binary format") {
    CHECK(modesp::gen::LOG_CHANNELS_COUNT >= 1);
    CHECK(modesp::gen::LOG_CHANNELS_COUNT <= modesp::gen::MAX_LOG_CHANNELS);
    for (size_t i = 0; i < modesp::gen::LOG_CHANNELS_COUNT; i++) {
        CHECK(modesp::gen::LOG_CHANNELS[i].id != nullptr);
        CHECK(modesp::gen::LOG_CHANNELS[i].state_key != nullptr);
    }
}

TEST_CASE("DataLogger: channel 0 is always-on equipment.air_temp") {
    CHECK(modesp::gen::LOG_CHANNELS[0].enable_key == nullptr);
    CHECK(strcmp(modesp::gen::LOG_CHANNELS[0].state_key, "equipment.air_temp") == 0);
}

TEST_CASE("DataLogger: generated event table has valid keys") {
    for (size_t i = 0; i < modesp::gen::LOG_EVENTS_COUNT; i++) {
        CHECK(modesp::gen::LOG_EVENTS[i].state_key != nullptr);
        CHECK(modesp::gen::LOG_EVENTS[i].label != nullptr);
    }
}

// ── 3. Init publishes state keys ──

TEST_CASE_FIXTURE(DLFixture, "DataLogger: init publishes counter keys") {
    CHECK(records_at_init >= 0);
    CHECK(get_int(state, "datalogger.flash_used") >= 0);
    // POWER_ON маркер логується в on_init → events_count містить мінімум 1 подію
    CHECK(events_at_init >= 1);
}

// ── 4. Disabled module skips update ──

TEST_CASE_FIXTURE(DLFixture, "DataLogger: disabled datalogger skips update") {
    state.set("datalogger.enabled", false);
    state.set("simple_thermo.output", true);  // edge, який мав би дати подію

    // Тікаємо 1.5 с — більше за sample_interval (1 с)
    for (int i = 0; i < 15; i++) tick(100);

    CHECK(get_int(state, "datalogger.records_count") == records_at_init);
    CHECK(get_int(state, "datalogger.events_count") == events_at_init);
}

// ── 5. Sample timer ──

TEST_CASE_FIXTURE(DLFixture, "DataLogger: samples after interval elapsed") {
    // 0.9 s — недостатньо (interval = 1 s)
    for (int i = 0; i < 9; i++) tick(100);
    CHECK(get_int(state, "datalogger.records_count") == records_at_init);

    // Ще 100 мс — рівно 1 с
    tick(100);
    CHECK(get_int(state, "datalogger.records_count") == records_at_init + 1);

    // Таймер скидається — наступний семпл знову через 1 с
    for (int i = 0; i < 9; i++) tick(100);
    CHECK(get_int(state, "datalogger.records_count") == records_at_init + 1);
    tick(100);
    CHECK(get_int(state, "datalogger.records_count") == records_at_init + 2);
}

// ── 6. Event edge-detect (BOTH: simple_thermo.output) ──

TEST_CASE_FIXTURE(DLFixture, "DataLogger: BOTH edge logs ON and OFF events") {
    // Rising edge → подія (id)
    state.set("simple_thermo.output", true);
    tick();
    CHECK(get_int(state, "datalogger.events_count") == events_at_init + 1);

    // Falling edge → подія (id + 1)
    state.set("simple_thermo.output", false);
    tick();
    CHECK(get_int(state, "datalogger.events_count") == events_at_init + 2);
}

TEST_CASE_FIXTURE(DLFixture, "DataLogger: no event when state unchanged") {
    state.set("simple_thermo.output", true);
    tick();
    int32_t after_on = get_int(state, "datalogger.events_count");
    CHECK(after_on == events_at_init + 1);

    // Той самий стан — подій не додається
    tick();
    tick();
    CHECK(get_int(state, "datalogger.events_count") == after_on);
}

// ── 7. Summary output ──

TEST_CASE_FIXTURE(DLFixture, "DataLogger: serialize_summary produces valid JSON") {
    char buf[256];
    bool ok = dl.serialize_summary(buf, sizeof(buf));
    CHECK(ok == true);
    CHECK(strstr(buf, "\"hours\"") != nullptr);
    CHECK(strstr(buf, "\"temp_count\"") != nullptr);
    CHECK(strstr(buf, "\"event_count\"") != nullptr);
    CHECK(strstr(buf, "\"flash_kb\"") != nullptr);
    CHECK(strstr(buf, "\"channels\"") != nullptr);
}

TEST_CASE_FIXTURE(DLFixture, "DataLogger: summary reflects retention_hours setting") {
    char buf[256];
    REQUIRE(dl.serialize_summary(buf, sizeof(buf)));
    CHECK(strstr(buf, "\"hours\":48") != nullptr);
}

} // TEST_SUITE
