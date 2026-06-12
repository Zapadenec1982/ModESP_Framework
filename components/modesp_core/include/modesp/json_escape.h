/**
 * @file json_escape.h
 * @brief Minimal JSON string escaper for embedding runtime strings into JSON
 *        documents (AWS Shadow, MQTT discovery, HTTP responses).
 *
 * Escapes ", \, and control characters (< 0x20) so a state string value cannot
 * break or inject into the surrounding JSON. Writes at most dest_size-1 bytes
 * plus a NUL; truncates safely if the buffer is too small. Returns the number
 * of bytes written (excluding the NUL).
 */
#pragma once

#include <cstddef>
#include <cstdio>

namespace modesp {

inline size_t json_escape(char* dest, size_t dest_size, const char* src) {
    if (dest_size == 0) return 0;
    size_t w = 0;
    for (const char* p = src; *p && w + 2 < dest_size; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        switch (c) {
            case '"':  dest[w++] = '\\'; dest[w++] = '"';  break;
            case '\\': dest[w++] = '\\'; dest[w++] = '\\'; break;
            case '\n': dest[w++] = '\\'; dest[w++] = 'n';  break;
            case '\r': dest[w++] = '\\'; dest[w++] = 'r';  break;
            case '\t': dest[w++] = '\\'; dest[w++] = 't';  break;
            default:
                if (c < 0x20) {
                    if (w + 6 >= dest_size) break;          // \u00XX needs 6
                    w += snprintf(dest + w, dest_size - w, "\\u%04x", c);
                } else {
                    dest[w++] = static_cast<char>(c);
                }
        }
    }
    dest[w] = '\0';
    return w;
}

} // namespace modesp
