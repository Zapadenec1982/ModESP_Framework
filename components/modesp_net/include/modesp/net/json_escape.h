/**
 * @file json_escape.h
 * @brief JSON string escapes in request bodies.
 *
 * jsmn hands out a string token's bytes as they stand in the body, escapes and all, so a
 * value sent as "a\"b" reaches a handler as the five bytes a\"b, not the three the client
 * meant. A handler that stores or compares such a value has to unescape it first.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace modesp::net {

// A JSON string's escapes turned back into UTF-8, in place. The result is never longer than
// the input. False for a malformed escape, a lone surrogate, or \u0000 (it would cut the
// value short).
inline bool json_unescape(char* s, size_t* written = nullptr) {
    if (!s) return false;
    auto hex4 = [](const char* p, uint32_t& v) {
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = p[i];
            const uint32_t d = c >= '0' && c <= '9' ? uint32_t(c - '0') :
                               c >= 'a' && c <= 'f' ? uint32_t(c - 'a' + 10) :
                               c >= 'A' && c <= 'F' ? uint32_t(c - 'A' + 10) : 16u;
            if (d > 15) return false;
            v = (v << 4) | d;
        }
        return true;
    };
    char* w = s;
    for (const char* p = s; *p;) {
        if (*p != '\\') { *w++ = *p++; continue; }
        const char e = p[1];
        p += 2;
        switch (e) {
        case '"':  *w++ = '"';  break;
        case '\\': *w++ = '\\'; break;
        case '/':  *w++ = '/';  break;
        case 'b':  *w++ = '\b'; break;
        case 'f':  *w++ = '\f'; break;
        case 'n':  *w++ = '\n'; break;
        case 'r':  *w++ = '\r'; break;
        case 't':  *w++ = '\t'; break;
        case 'u': {
            uint32_t cp;
            if (!hex4(p, cp)) return false;
            p += 4;
            if (cp >= 0xd800 && cp <= 0xdbff) {          // a pair: the low half must follow
                uint32_t lo;
                if (p[0] != '\\' || p[1] != 'u' || !hex4(p + 2, lo) || lo < 0xdc00 || lo > 0xdfff)
                    return false;
                p += 6;
                cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
            } else if ((cp >= 0xdc00 && cp <= 0xdfff) || cp == 0) {
                return false;
            }
            if (cp < 0x80) {
                *w++ = char(cp);
            } else if (cp < 0x800) {
                *w++ = char(0xc0 | (cp >> 6));
                *w++ = char(0x80 | (cp & 0x3f));
            } else if (cp < 0x10000) {
                *w++ = char(0xe0 | (cp >> 12));
                *w++ = char(0x80 | ((cp >> 6) & 0x3f));
                *w++ = char(0x80 | (cp & 0x3f));
            } else {
                *w++ = char(0xf0 | (cp >> 18));
                *w++ = char(0x80 | ((cp >> 12) & 0x3f));
                *w++ = char(0x80 | ((cp >> 6) & 0x3f));
                *w++ = char(0x80 | (cp & 0x3f));
            }
            break;
        }
        default:
            return false;                                // includes a backslash at the very end
        }
    }
    *w = '\0';
    if (written) *written = size_t(w - s);
    return true;
}

} // namespace modesp::net
