/**
 * @file test_json_escape.cpp
 * @brief modesp::net::json_unescape() — a JSON string's escapes turned back, in place.
 */

#include "doctest.h"
#include "modesp/net/json_escape.h"

#include <string>

TEST_CASE("JSON unescape turns quotes and backslashes back and leaves UTF-8 alone") {
    // As a browser's JSON.stringify sends: My "home" net\5G Я 🎵 and a control byte.
    char s[] = "My \\\"home\\\" net\\\\5G \xd0\xaf \xf0\x9f\x8e\xb5\\u0001";
    const char want[] = "My \"home\" net\\5G \xd0\xaf \xf0\x9f\x8e\xb5\x01";
    size_t n = 0;
    REQUIRE(modesp::net::json_unescape(s, &n));
    CHECK(std::string(s) == want);
    CHECK(n == sizeof(want) - 1);
}

TEST_CASE("JSON unescape reads every escape, and \\u pairs as one character") {
    char s[] = "a\\/b\\n\\t\\r\\b\\f\\u0041\\u00e9\\u042f\\ud83c\\udfb5";
    REQUIRE(modesp::net::json_unescape(s));
    CHECK(std::string(s) == "a/b\n\t\r\b\fA\xc3\xa9\xd0\xaf\xf0\x9f\x8e\xb5");
}

TEST_CASE("JSON unescape refuses what is not an escape, and leaves plain text alone") {
    char plain[] = "plain text";
    REQUIRE(modesp::net::json_unescape(plain));
    CHECK(std::string(plain) == "plain text");
    const char* bad[] = {"end\\", "\\x41", "\\u12", "\\u12zz", "\\ud83c", "\\udfb5", "\\ud83c\\u0041", "nul\\u0000"};
    for (const char* b : bad) {
        INFO(b);
        std::string copy(b);
        CHECK_FALSE(modesp::net::json_unescape(&copy[0]));
    }
}
