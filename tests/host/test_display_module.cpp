/**
 * @file test_display_module.cpp
 * @brief HOST integration tests для DisplayModule (glue-шар, cov-1 з ревью).
 *
 * Покриває: on_message-фільтр UI_NOTICE → черга, edge-detect кнопок із
 * самоскиданням, потік present (меню + банер поверх), дзеркало display.banner*
 * у SharedState, gate display.enabled. Інжекція через set_port(FakePort).
 */

#include "mocks/freertos_mock.h"
#include "mocks/esp_log_mock.h"

#include <string>

#include "doctest.h"
#include "modesp/shared_state.h"
#include "modesp/module_manager.h"
#include "modesp/message_types.h"
#include "display_module.h"

using namespace modesp;
using namespace modesp::display;

namespace {

struct FakePort : IDisplayPort {
    int    main_n = 0, menu_n = 0, edit_n = 0, notice_n = 0, clear_n = 0;
    Notice last_notice;
    void present_main(const MainView&) override   { ++main_n; }
    void present_menu(const MenuView&) override   { ++menu_n; }
    void present_edit(const EditView&) override   { ++edit_n; }
    void present_notice(const Notice& n) override { ++notice_n; last_notice = n; }
    void clear_notice() override                  { ++clear_n; }
};

struct DisplayFixture {
    SharedState   state;
    ModuleManager mgr;
    DisplayModule dm;
    FakePort      port;

    DisplayFixture() {
        mgr.register_module(dm);
        dm.set_port(&port);
        mgr.init_all(state);    // on_init: початкові ключі + port.init()
    }
    void tick(uint32_t ms = 50) { dm.on_update(ms); }

    std::string str(const char* key) {
        auto v = state.get(key);
        if (!v.has_value()) return "<none>";
        const auto* p = etl::get_if<StringValue>(&v.value());
        return p ? std::string(p->c_str()) : "<nonstr>";
    }
    int32_t i32(const char* key) {
        auto v = state.get(key);
        if (!v.has_value()) return -999;
        const auto* p = etl::get_if<int32_t>(&v.value());
        return p ? *p : -999;
    }
    bool flag(const char* key) {
        auto v = state.get(key);
        if (!v.has_value()) return false;
        const auto* p = etl::get_if<bool>(&v.value());
        return p && *p;
    }
};

} // namespace

TEST_SUITE("DisplayModule") {

TEST_CASE_FIXTURE(DisplayFixture, "UI_NOTICE → present_notice + SharedState mirror") {
    MsgUiNotice m;
    m.level = 2; m.ttl_ms = 0; m.text = "SAFE MODE";
    dm.on_message(m);
    tick();
    CHECK(port.notice_n >= 1);
    CHECK(std::string(port.last_notice.text.c_str()) == "SAFE MODE");
    CHECK(str("display.banner") == "SAFE MODE");
    CHECK(i32("display.banner_level") == 2);
}

TEST_CASE_FIXTURE(DisplayFixture, "non-UI_NOTICE message is ignored") {
    MsgTimerTick m; m.uptime_sec = 1;
    dm.on_message(m);          // не UI_NOTICE → черга не чіпається
    tick();
    CHECK(port.notice_n == 0);
}

TEST_CASE_FIXTURE(DisplayFixture, "button edge handled once + momentary self-reset") {
    state.set("display.btn_select", true);
    tick();
    CHECK_FALSE(flag("display.btn_select"));               // самоскидання
    CHECK(str("display.screen").rfind("menu:", 0) == 0);   // SELECT відкрив меню (root_count>=1)
    CHECK(port.menu_n >= 1);
}

TEST_CASE_FIXTURE(DisplayFixture, "active banner re-presented over menu on screen change") {
    MsgUiNotice m; m.level = 2; m.ttl_ms = 0; m.text = "AL";
    dm.on_message(m);
    tick();                              // present_notice
    const int n0 = port.notice_n;
    state.set("display.btn_select", true);
    tick();                              // engine dirty → present_current + present_notice знову
    CHECK(port.menu_n >= 1);
    CHECK(port.notice_n > n0);
}

TEST_CASE_FIXTURE(DisplayFixture, "notice TTL expiry clears banner + mirror") {
    MsgUiNotice m; m.level = 1; m.ttl_ms = 100; m.text = "warn";
    dm.on_message(m);
    tick(10);
    CHECK(port.notice_n >= 1);
    tick(200);                           // вийшов TTL
    CHECK(port.clear_n >= 1);
    CHECK(str("display.banner") == "");
    CHECK(i32("display.banner_level") == 0);
}

TEST_CASE_FIXTURE(DisplayFixture, "display.enabled=false short-circuits on_update") {
    state.set("display.enabled", false);
    const int before = port.main_n + port.menu_n + port.edit_n;
    state.set("display.btn_select", true);
    tick();
    CHECK(port.main_n + port.menu_n + port.edit_n == before);  // нічого не рендериться
    CHECK(flag("display.btn_select"));                          // кнопка НЕ оброблена (ранній вихід)
}

} // TEST_SUITE
