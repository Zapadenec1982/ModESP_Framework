/**
 * @file test_char_grid.cpp
 * @brief HOST unit tests для CharGridLayout (ADR-002, Крок 2).
 *
 * Перевіряють ЧИСТУ універсальну поведінку (НЕ legacy AT7456E-вивід):
 *   - glyph_len рахує кодпойнти, не байти
 *   - layout_menu: заголовок, ролі, скрол навколо selected
 *   - UTF-8 обрізання по гліфах (кирилиця не розривається)
 *   - right-align значень у пунктах
 *   - layout_edit / layout_main
 */

#include <cstdio>
#include <string>

#include "doctest.h"
#include "modesp/hal/char_grid.h"

using namespace modesp::display;

static MenuItem mk(const char* label, const char* value = "",
                   bool submenu = false, bool back = false) {
    MenuItem it;
    it.label = label;
    it.value = value;
    it.is_submenu = submenu;
    it.is_back = back;
    return it;
}

static int role_i(RowRole r) { return static_cast<int>(r); }

// ── glyph_len ──

TEST_CASE("CharGridLayout::glyph_len counts codepoints, not bytes") {
    CHECK(CharGridLayout::glyph_len("ABC") == 3);
    CHECK(CharGridLayout::glyph_len("Привіт") == 6);   // 12 байтів, 6 гліфів
    CHECK(CharGridLayout::glyph_len("°C") == 2);        // ° = 2 байти
    CHECK(CharGridLayout::glyph_len("") == 0);
}

// ── menu ──

TEST_CASE("layout_menu: title + items with roles") {
    MenuView v;
    v.title = "Меню";
    v.items.push_back(mk("Thermo", "", true));
    v.items.push_back(mk("Setpoint", "22.0 C"));
    v.items.push_back(mk("Назад", "", false, true));
    v.selected = 1;

    CharGrid g;
    CharGridLayout::layout_menu(v, 20, 6, g);

    REQUIRE(g.lines.size() == 4);                       // заголовок + 3 пункти
    CHECK(role_i(g.lines[0].role) == role_i(RowRole::TITLE));
    CHECK(std::string(g.lines[0].text.c_str()) == "Меню");
    CHECK(role_i(g.lines[2].role) == role_i(RowRole::SELECTED));  // selected=1 → рядок 2
    CHECK(role_i(g.lines[1].role) == role_i(RowRole::NORMAL));

    // значення вирівняне праворуч
    std::string l2 = g.lines[2].text.c_str();
    CHECK(l2.rfind("22.0 C") != std::string::npos);
    CHECK(CharGridLayout::glyph_len(l2.c_str()) <= 20);
}

TEST_CASE("layout_menu: scroll keeps selected visible") {
    MenuView v;
    v.title = "T";
    for (int i = 0; i < 10; ++i) {
        char b[8];
        snprintf(b, sizeof(b), "i%d", i);
        v.items.push_back(mk(b));
    }
    v.selected = 9;

    CharGrid g;
    CharGridLayout::layout_menu(v, 20, 4, g);           // rows=4 → заголовок + 3 видимих

    REQUIRE(g.lines.size() == 4);
    bool found = false;
    for (size_t i = 1; i < g.lines.size(); ++i) {
        if (role_i(g.lines[i].role) == role_i(RowRole::SELECTED)) {
            found = true;
            CHECK(std::string(g.lines[i].text.c_str()) == "i9");
        }
    }
    CHECK(found);
    // верхній видимий пункт уже прокручений (не "i0")
    CHECK(std::string(g.lines[1].text.c_str()) != "i0");
}

TEST_CASE("layout_menu: selected near top shows from first item") {
    MenuView v;
    v.title = "T";
    for (int i = 0; i < 10; ++i) {
        char b[8]; snprintf(b, sizeof(b), "i%d", i);
        v.items.push_back(mk(b));
    }
    v.selected = 0;

    CharGrid g;
    CharGridLayout::layout_menu(v, 20, 4, g);
    CHECK(std::string(g.lines[1].text.c_str()) == "i0");
    CHECK(role_i(g.lines[1].role) == role_i(RowRole::SELECTED));
}

// ── UTF-8 truncation ──

TEST_CASE("layout: UTF-8 truncation by glyphs, no split") {
    MenuView v;
    v.title = "Привіт-Світ";                            // довгий кириличний
    v.items.push_back(mk("x"));

    CharGrid g;
    CharGridLayout::layout_menu(v, 5, 3, g);

    // рівно 5 гліфів, рядок лишається валідним UTF-8 (re-count == 5)
    CHECK(CharGridLayout::glyph_len(g.lines[0].text.c_str()) == 5);
    CHECK(std::string(g.lines[0].text.c_str()) == "Приві");
}

// ── edit ──

TEST_CASE("layout_edit: title / value / range roles") {
    EditView e;
    e.title = "Setpoint";
    e.value = "22.0 C";
    e.range = "5..40 /0.5";

    CharGrid g;
    CharGridLayout::layout_edit(e, 20, 4, g);

    REQUIRE(g.lines.size() == 3);
    CHECK(role_i(g.lines[0].role) == role_i(RowRole::TITLE));
    CHECK(role_i(g.lines[1].role) == role_i(RowRole::SELECTED));
    CHECK(std::string(g.lines[1].text.c_str()) == "22.0 C");
    CHECK(role_i(g.lines[2].role) == role_i(RowRole::HINT));
    CHECK(std::string(g.lines[2].text.c_str()) == "5..40 /0.5");
}

TEST_CASE("layout_edit: empty range omits hint row") {
    EditView e;
    e.title = "Mode";
    e.value = "Auto";
    // range порожній

    CharGrid g;
    CharGridLayout::layout_edit(e, 20, 4, g);
    REQUIRE(g.lines.size() == 2);
}

// ── main ──

TEST_CASE("layout_main: label + right-aligned value") {
    MainView m;
    MainItem it;
    it.label = "Thermo";
    it.value = "21.5";
    it.unit = "C";
    m.items.push_back(it);

    CharGrid g;
    CharGridLayout::layout_main(m, 20, 4, g);

    REQUIRE(g.lines.size() == 1);
    std::string l = g.lines[0].text.c_str();
    CHECK(l.rfind("21.5 C") != std::string::npos);
    CHECK(CharGridLayout::glyph_len(l.c_str()) <= 20);
}

TEST_CASE("layout_main: empty falls back to ModESP title") {
    MainView m;
    CharGrid g;
    CharGridLayout::layout_main(m, 20, 4, g);
    REQUIRE(g.lines.size() == 1);
    CHECK(std::string(g.lines[0].text.c_str()) == "ModESP");
    CHECK(role_i(g.lines[0].role) == role_i(RowRole::TITLE));
}
