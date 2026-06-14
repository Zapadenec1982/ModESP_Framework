/**
 * @file test_osd_charmap.cpp
 * @brief HOST unit tests for osd_charmap.h (UTF-8 → glyph index).
 *
 * Чиста логіка без заліза: декодер UTF-8 і розкладка гліфів власного
 * NVM-шрифту AT7456E. Файл у UTF-8 — кириличні літерали як є.
 */

#include "doctest.h"
#include "modesp/osd/osd_charmap.h"

using namespace modesp::osd;

// ── UTF-8 decode ──

TEST_CASE("utf8_decode: ASCII single byte") {
    uint32_t cp = 0;
    CHECK(utf8_decode("A", 1, cp) == 1);
    CHECK(cp == 0x41);
}

TEST_CASE("utf8_decode: 2-byte Cyrillic") {
    uint32_t cp = 0;
    // 'Т' = U+0422 = 0xD0 0xA2
    const char s[] = "\xD0\xA2";
    CHECK(utf8_decode(s, 2, cp) == 2);
    CHECK(cp == 0x0422);
}

TEST_CASE("utf8_decode: 2-byte degree sign") {
    uint32_t cp = 0;
    // '°' = U+00B0 = 0xC2 0xB0
    const char s[] = "\xC2\xB0";
    CHECK(utf8_decode(s, 2, cp) == 2);
    CHECK(cp == 0x00B0);
}

TEST_CASE("utf8_decode: truncated sequence yields replacement, consumes 1") {
    uint32_t cp = 0;
    const char s[] = "\xD0";   // lead byte, no continuation
    CHECK(utf8_decode(s, 1, cp) == 1);
    CHECK(cp == 0xFFFD);
}

TEST_CASE("utf8_decode: bad continuation byte") {
    uint32_t cp = 0;
    const char s[] = "\xD0\x41";  // second byte not 10xxxxxx
    CHECK(utf8_decode(s, 2, cp) == 1);
    CHECK(cp == 0xFFFD);
}

// ── codepoint → glyph ──

TEST_CASE("glyph: printable ASCII is identity") {
    CHECK(osd_codepoint_to_glyph('A') == 0x41);
    CHECK(osd_codepoint_to_glyph(' ') == 0x20);
    CHECK(osd_codepoint_to_glyph('~') == 0x7E);
    CHECK(osd_codepoint_to_glyph('0') == 0x30);
}

TEST_CASE("glyph: Cyrillic main block offset from 0x80") {
    CHECK(osd_codepoint_to_glyph(0x0410) == 0x80);  // А
    CHECK(osd_codepoint_to_glyph(0x0422) == 0x92);  // Т
    CHECK(osd_codepoint_to_glyph(0x0430) == 0xA0);  // а
    CHECK(osd_codepoint_to_glyph(0x044F) == 0xBF);  // я (кінець блоку)
}

TEST_CASE("glyph: Ukrainian special letters") {
    CHECK(osd_codepoint_to_glyph(0x00B0) == GLYPH_DEGREE);  // °
    CHECK(osd_codepoint_to_glyph(0x0404) == 0xC0);  // Є
    CHECK(osd_codepoint_to_glyph(0x0406) == 0xC1);  // І
    CHECK(osd_codepoint_to_glyph(0x0407) == 0xC2);  // Ї
    CHECK(osd_codepoint_to_glyph(0x0490) == 0xC3);  // Ґ
    CHECK(osd_codepoint_to_glyph(0x0454) == 0xC4);  // є
    CHECK(osd_codepoint_to_glyph(0x0456) == 0xC5);  // і
    CHECK(osd_codepoint_to_glyph(0x0457) == 0xC6);  // ї
    CHECK(osd_codepoint_to_glyph(0x0491) == 0xC7);  // ґ
}

TEST_CASE("glyph: unknown codepoint falls back to '?'") {
    CHECK(osd_codepoint_to_glyph(0x4E2D) == GLYPH_QUESTION);  // 中
    CHECK(osd_codepoint_to_glyph(0x00A0) == 0x20);            // nbsp → space
}

// ── full string mapping ──

TEST_CASE("osd_map_utf8: maps a Ukrainian word") {
    uint8_t out[32];
    const size_t n = osd_map_utf8("Термостат", out, sizeof(out));
    REQUIRE(n == 9);
    const uint8_t expect[] = {0x92, 0xA5, 0xB0, 0xAC, 0xAE, 0xB1, 0xB2, 0xA0, 0xB2};
    for (size_t i = 0; i < n; ++i) CHECK(out[i] == expect[i]);
}

TEST_CASE("osd_map_utf8: mixed ASCII + Cyrillic + degree") {
    uint8_t out[32];
    // "22.5°C"
    const size_t n = osd_map_utf8("22.5\xC2\xB0""C", out, sizeof(out));
    REQUIRE(n == 6);
    CHECK(out[0] == '2');
    CHECK(out[1] == '2');
    CHECK(out[2] == '.');
    CHECK(out[3] == '5');
    CHECK(out[4] == GLYPH_DEGREE);
    CHECK(out[5] == 'C');
}

TEST_CASE("osd_map_utf8: respects output capacity") {
    uint8_t out[3];
    const size_t n = osd_map_utf8("ABCDEF", out, sizeof(out));
    CHECK(n == 3);
    CHECK(out[0] == 'A');
    CHECK(out[2] == 'C');
}

TEST_CASE("osd_map_utf8: empty string yields nothing") {
    uint8_t out[8];
    CHECK(osd_map_utf8("", out, sizeof(out)) == 0);
}
