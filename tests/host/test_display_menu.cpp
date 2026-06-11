/**
 * @file test_display_menu.cpp
 * @brief HOST unit tests for MenuEngine (display module).
 *
 * MenuEngine отримує дерево меню через DI (MenuData), тому тести
 * використовують власні фікстурні масиви з тими ж структурами, що
 * генерує generate_ui.py — незалежно від project.json.
 *
 * Tests cover:
 *   - MAIN screen: main values rendering, [OK] hint
 *   - MENU navigation: up/down/select, submenu enter/exit, scroll, back
 *   - EDIT: float clamp min/max/step, enum cycle, bool toggle, save
 *   - Idle timeout повертає на MAIN (зміни відкидаються)
 *   - consume_dirty семантика
 */

#include "mocks/freertos_mock.h"
#include "mocks/esp_log_mock.h"

#include <cstring>
#include <string>

#include "doctest.h"
#include "modesp/shared_state.h"
#include "display/menu_engine.h"

using namespace modesp;
using namespace modesp::display;
namespace dgen = modesp::gen;

// ── Фікстурне дерево меню (як згенерував би generate_ui.py) ──

static constexpr dgen::DisplayEnumOption MODE_OPTS[] = {
    {"Auto", 0}, {"Manual", 1}, {"Off", 2},
};
static constexpr dgen::DisplayEnumOption HEAT_OPTS[] = {
    {"OFF", 0}, {"ON", 1},
};

static constexpr dgen::DisplayMenuNode NODES[] = {
    // 0: submenu "Thermo" → children [2..6)
    {"Thermo", "", "", "", dgen::DisplayItemType::SUBMENU,
     0, 0, 0, nullptr, 0, 2, 4},
    // 1: submenu "System" → children [6..7)
    {"System", "", "", "", dgen::DisplayItemType::SUBMENU,
     0, 0, 0, nullptr, 0, 6, 1},
    // 2: EDIT_FLOAT 5..40 step 0.5
    {"Setpoint", "t.sp", "%.1f", "C", dgen::DisplayItemType::EDIT_FLOAT,
     5.0f, 40.0f, 0.5f, nullptr, 0, 0, 0},
    // 3: EDIT_ENUM
    {"Mode", "t.mode", "%d", "", dgen::DisplayItemType::EDIT_ENUM,
     0, 0, 0, MODE_OPTS, 3, 0, 0},
    // 4: EDIT_BOOL
    {"Heat", "t.heat", "%s", "", dgen::DisplayItemType::EDIT_BOOL,
     0, 0, 0, HEAT_OPTS, 2, 0, 0},
    // 5: VALUE (read-only float)
    {"Temp", "t.temp", "%.1f", "C", dgen::DisplayItemType::VALUE,
     0, 0, 0, nullptr, 0, 0, 0},
    // 6: VALUE (read-only int)
    {"Uptime", "sys.up", "%d", "s", dgen::DisplayItemType::VALUE,
     0, 0, 0, nullptr, 0, 0, 0},
};

static constexpr dgen::DisplayMainValue MAINS[] = {
    {"thermo", "Thermo", "t.temp", "%.1f"},
};

// ── Test IO + fixture ──

struct TestIO : IMenuStateIO {
    SharedState state;

    etl::optional<StateValue> get(const char* key) const override {
        return state.get(key);
    }
    bool set(const char* key, const StateValue& value) override {
        return state.set(StateKey(key), value);
    }
};

struct EngineFixture {
    TestIO io;
    MenuEngine eng;

    EngineFixture()
        : eng(io, MenuData{NODES, 7, 0, 2, MAINS, 1})
    {
        io.state.set("t.temp", 21.5f);
        io.state.set("t.sp", 22.0f);
        io.state.set("t.mode", static_cast<int32_t>(1));
        io.state.set("t.heat", true);
        io.state.set("sys.up", static_cast<int32_t>(123));
        eng.tick(MenuEngine::REFRESH_MS);  // перший кадр
    }

    float get_float(const char* key) {
        auto v = io.state.get(key);
        const auto* f = v.has_value() ? etl::get_if<float>(&v.value()) : nullptr;
        return f ? *f : -999.0f;
    }
    int32_t get_int(const char* key) {
        auto v = io.state.get(key);
        const auto* i = v.has_value() ? etl::get_if<int32_t>(&v.value()) : nullptr;
        return i ? *i : -999;
    }
    bool get_bool(const char* key) {
        auto v = io.state.get(key);
        const auto* b = v.has_value() ? etl::get_if<bool>(&v.value()) : nullptr;
        return b && *b;
    }

    bool frame_contains(const char* text) const {
        for (const auto& row : eng.frame().rows) {
            if (strstr(row.c_str(), text)) return true;
        }
        return false;
    }
};

// ── MAIN screen ──

TEST_CASE("MenuEngine: starts on MAIN with main values") {
    EngineFixture f;
    CHECK(f.eng.screen() == MenuEngine::Screen::MAIN);
    CHECK(std::string(f.eng.screen_name()) == "main");
    CHECK(f.frame_contains("Thermo 21.5"));
    CHECK(f.frame_contains("[OK]"));
}

TEST_CASE("MenuEngine: MAIN refreshes values periodically") {
    EngineFixture f;
    f.eng.consume_dirty();
    f.io.state.set("t.temp", 25.0f);
    f.eng.tick(MenuEngine::REFRESH_MS);
    CHECK(f.eng.consume_dirty());
    CHECK(f.frame_contains("Thermo 25.0"));
}

TEST_CASE("MenuEngine: consume_dirty returns true once") {
    EngineFixture f;
    CHECK(f.eng.consume_dirty());
    CHECK_FALSE(f.eng.consume_dirty());
}

// ── MENU navigation ──

TEST_CASE("MenuEngine: SELECT opens root menu") {
    EngineFixture f;
    f.eng.handle_event(MenuEvent::SELECT);
    CHECK(f.eng.screen() == MenuEngine::Screen::MENU);
    CHECK(std::string(f.eng.screen_name()) == "menu:root");
    CHECK(f.frame_contains("Thermo"));
    CHECK(f.frame_contains("System"));
}

TEST_CASE("MenuEngine: enter submenu and show live value") {
    EngineFixture f;
    f.eng.handle_event(MenuEvent::SELECT);  // MENU root
    f.eng.handle_event(MenuEvent::DOWN);    // cursor → System
    f.eng.handle_event(MenuEvent::SELECT);  // enter System
    CHECK(std::string(f.eng.screen_name()) == "menu:System");
    CHECK(f.frame_contains("Uptime: 123 s"));
}

TEST_CASE("MenuEngine: back returns to parent, exit returns to MAIN") {
    EngineFixture f;
    f.eng.handle_event(MenuEvent::SELECT);  // root
    f.eng.handle_event(MenuEvent::DOWN);
    f.eng.handle_event(MenuEvent::SELECT);  // System
    f.eng.handle_event(MenuEvent::DOWN);    // cursor → "< Назад"
    f.eng.handle_event(MenuEvent::SELECT);  // back to root
    CHECK(std::string(f.eng.screen_name()) == "menu:root");

    f.eng.handle_event(MenuEvent::DOWN);    // System (cursor restored = 1)
    f.eng.handle_event(MenuEvent::DOWN);    // "< Вихід"
    f.eng.handle_event(MenuEvent::SELECT);
    CHECK(f.eng.screen() == MenuEngine::Screen::MAIN);
}

TEST_CASE("MenuEngine: scroll window follows cursor") {
    EngineFixture f;
    f.eng.handle_event(MenuEvent::SELECT);  // root
    f.eng.handle_event(MenuEvent::SELECT);  // enter Thermo (4 items + back)
    for (int i = 0; i < 4; ++i) f.eng.handle_event(MenuEvent::DOWN);
    // cursor на "< Назад" (індекс 4), вікно прокручене
    CHECK(f.frame_contains(">< Назад"));
    CHECK_FALSE(f.frame_contains("Setpoint"));
}

TEST_CASE("MenuEngine: UP does not go above first item") {
    EngineFixture f;
    f.eng.handle_event(MenuEvent::SELECT);
    f.eng.handle_event(MenuEvent::UP);
    f.eng.handle_event(MenuEvent::UP);
    CHECK(f.frame_contains(">Thermo"));
}

TEST_CASE("MenuEngine: VALUE item is not editable") {
    EngineFixture f;
    f.eng.handle_event(MenuEvent::SELECT);  // root
    f.eng.handle_event(MenuEvent::SELECT);  // Thermo
    f.eng.handle_event(MenuEvent::DOWN);    // Mode
    f.eng.handle_event(MenuEvent::DOWN);    // Heat
    f.eng.handle_event(MenuEvent::DOWN);    // Temp (VALUE)
    f.eng.handle_event(MenuEvent::SELECT);
    CHECK(f.eng.screen() == MenuEngine::Screen::MENU);  // лишились у меню
}

// ── EDIT ──

TEST_CASE("MenuEngine: edit float — step, clamp, save") {
    EngineFixture f;
    f.eng.handle_event(MenuEvent::SELECT);  // root
    f.eng.handle_event(MenuEvent::SELECT);  // Thermo
    f.eng.handle_event(MenuEvent::SELECT);  // edit Setpoint
    CHECK(f.eng.screen() == MenuEngine::Screen::EDIT);
    CHECK(std::string(f.eng.screen_name()) == "edit:Setpoint");

    SUBCASE("UP додає step, SELECT зберігає") {
        f.eng.handle_event(MenuEvent::UP);   // 22.0 → 22.5
        f.eng.handle_event(MenuEvent::SELECT);
        CHECK(f.get_float("t.sp") == doctest::Approx(22.5f));
        CHECK(f.eng.screen() == MenuEngine::Screen::MENU);
    }

    SUBCASE("clamp до max") {
        for (int i = 0; i < 100; ++i) f.eng.handle_event(MenuEvent::UP);
        f.eng.handle_event(MenuEvent::SELECT);
        CHECK(f.get_float("t.sp") == doctest::Approx(40.0f));
    }

    SUBCASE("clamp до min") {
        for (int i = 0; i < 100; ++i) f.eng.handle_event(MenuEvent::DOWN);
        f.eng.handle_event(MenuEvent::SELECT);
        CHECK(f.get_float("t.sp") == doctest::Approx(5.0f));
    }
}

TEST_CASE("MenuEngine: edit enum cycles options and saves value") {
    EngineFixture f;
    f.eng.handle_event(MenuEvent::SELECT);  // root
    f.eng.handle_event(MenuEvent::SELECT);  // Thermo
    f.eng.handle_event(MenuEvent::DOWN);    // Mode
    f.eng.handle_event(MenuEvent::SELECT);  // edit (current = Manual)
    CHECK(f.frame_contains("Manual"));

    f.eng.handle_event(MenuEvent::UP);      // Manual → Off
    CHECK(f.frame_contains("Off"));
    f.eng.handle_event(MenuEvent::SELECT);
    CHECK(f.get_int("t.mode") == 2);
}

TEST_CASE("MenuEngine: edit enum wraps around") {
    EngineFixture f;
    f.eng.handle_event(MenuEvent::SELECT);
    f.eng.handle_event(MenuEvent::SELECT);
    f.eng.handle_event(MenuEvent::DOWN);
    f.eng.handle_event(MenuEvent::SELECT);  // Manual (index 1)
    f.eng.handle_event(MenuEvent::DOWN);    // → Auto
    f.eng.handle_event(MenuEvent::DOWN);    // → Off (wrap)
    f.eng.handle_event(MenuEvent::SELECT);
    CHECK(f.get_int("t.mode") == 2);
}

TEST_CASE("MenuEngine: edit bool toggles") {
    EngineFixture f;
    f.eng.handle_event(MenuEvent::SELECT);
    f.eng.handle_event(MenuEvent::SELECT);
    f.eng.handle_event(MenuEvent::DOWN);
    f.eng.handle_event(MenuEvent::DOWN);    // Heat
    f.eng.handle_event(MenuEvent::SELECT);  // edit (true → "ON")
    CHECK(f.frame_contains("ON"));
    f.eng.handle_event(MenuEvent::UP);      // toggle → OFF
    f.eng.handle_event(MenuEvent::SELECT);
    CHECK_FALSE(f.get_bool("t.heat"));
}

// ── Idle timeout ──

TEST_CASE("MenuEngine: idle timeout returns to MAIN and discards edit") {
    EngineFixture f;
    f.eng.handle_event(MenuEvent::SELECT);
    f.eng.handle_event(MenuEvent::SELECT);
    f.eng.handle_event(MenuEvent::SELECT);  // edit Setpoint
    f.eng.handle_event(MenuEvent::UP);      // 22.5 (не збережено)
    f.eng.tick(MenuEngine::IDLE_TIMEOUT_MS);
    CHECK(f.eng.screen() == MenuEngine::Screen::MAIN);
    CHECK(f.get_float("t.sp") == doctest::Approx(22.0f));  // відкинуто
}

TEST_CASE("MenuEngine: events reset idle timer") {
    EngineFixture f;
    f.eng.handle_event(MenuEvent::SELECT);
    f.eng.tick(MenuEngine::IDLE_TIMEOUT_MS - 1000);
    f.eng.handle_event(MenuEvent::DOWN);    // скидає таймер
    f.eng.tick(MenuEngine::IDLE_TIMEOUT_MS - 1000);
    CHECK(f.eng.screen() == MenuEngine::Screen::MENU);
}

// ── Empty tree ──

TEST_CASE("MenuEngine: empty tree — SELECT ignored on MAIN") {
    TestIO io;
    MenuEngine eng(io, MenuData{nullptr, 0, 0, 0, nullptr, 0});
    eng.tick(MenuEngine::REFRESH_MS);
    eng.handle_event(MenuEvent::SELECT);
    CHECK(eng.screen() == MenuEngine::Screen::MAIN);
}
