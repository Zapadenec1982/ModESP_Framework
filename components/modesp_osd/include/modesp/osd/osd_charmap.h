/**
 * @file osd_charmap.h
 * @brief UTF-8 → glyph-index карта для шрифту AT7456E (наш власний NVM-шрифт).
 *
 * Оскільки ми заливаємо ВЛАСНИЙ шрифт у character-NVM, ми ж і визначаємо
 * розкладку гліфів. Конвенція (її ж використовує генератор osd_font.mcm):
 *
 *   0x00          порожній гліф (blank)
 *   0x20-0x7E     друкований ASCII — індекс = код символу (space=0x20, 'A'=0x41)
 *   0x7F          '°' (U+00B0)
 *   0x80-0xBF     кириличний блок U+0410..U+044F (А-я, безперервний)
 *   0xC0-0xC7     українські спецлітери: Є І Ї Ґ є і ї ґ
 *   0xC8-0xE0     польські+німецькі (i18n): Ä Ö Ü ä ö ü ß Ą ą Ć ć Ę ę
 *                 Ł ł Ń ń Ó ó Ś ś Ź ź Ż ż
 *
 * Чистий хедер — без ESP-IDF/heap, тестується на host.
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace modesp::osd {

// ── Фіксовані індекси (мають збігатися з gen_osd_font.py) ──
static constexpr uint8_t GLYPH_BLANK   = 0x00;
static constexpr uint8_t GLYPH_QUESTION = 0x3F;  // '?' — fallback для невідомих
static constexpr uint8_t GLYPH_DEGREE  = 0x7F;   // '°'
static constexpr uint8_t GLYPH_CYR_BASE = 0x80;  // U+0410 → 0x80

/// codepoint → індекс гліфа за нашою NVM-розкладкою.
inline uint8_t osd_codepoint_to_glyph(uint32_t cp) {
    // Друкований ASCII — тотожність
    if (cp >= 0x20 && cp <= 0x7E) {
        return static_cast<uint8_t>(cp);
    }
    // Основний кириличний блок А(0x0410)..я(0x044F)
    if (cp >= 0x0410 && cp <= 0x044F) {
        return static_cast<uint8_t>(GLYPH_CYR_BASE + (cp - 0x0410));
    }
    switch (cp) {
    case 0x00B0: return GLYPH_DEGREE;  // °
    case 0x0404: return 0xC0;          // Є
    case 0x0406: return 0xC1;          // І
    case 0x0407: return 0xC2;          // Ї
    case 0x0490: return 0xC3;          // Ґ
    case 0x0454: return 0xC4;          // є
    case 0x0456: return 0xC5;          // і
    case 0x0457: return 0xC6;          // ї
    case 0x0491: return 0xC7;          // ґ
    // PL/DE (i18n) — індекси 0xC8..0xE0; порядок = gen_osd_font.py EXT_LATIN
    case 0x00C4: return 0xC8;  // Ä
    case 0x00D6: return 0xC9;  // Ö
    case 0x00DC: return 0xCA;  // Ü
    case 0x00E4: return 0xCB;  // ä
    case 0x00F6: return 0xCC;  // ö
    case 0x00FC: return 0xCD;  // ü
    case 0x00DF: return 0xCE;  // ß
    case 0x0104: return 0xCF;  // Ą
    case 0x0105: return 0xD0;  // ą
    case 0x0106: return 0xD1;  // Ć
    case 0x0107: return 0xD2;  // ć
    case 0x0118: return 0xD3;  // Ę
    case 0x0119: return 0xD4;  // ę
    case 0x0141: return 0xD5;  // Ł
    case 0x0142: return 0xD6;  // ł
    case 0x0143: return 0xD7;  // Ń
    case 0x0144: return 0xD8;  // ń
    case 0x00D3: return 0xD9;  // Ó
    case 0x00F3: return 0xDA;  // ó
    case 0x015A: return 0xDB;  // Ś
    case 0x015B: return 0xDC;  // ś
    case 0x0179: return 0xDD;  // Ź
    case 0x017A: return 0xDE;  // ź
    case 0x017B: return 0xDF;  // Ż
    case 0x017C: return 0xE0;  // ż
    case 0x00A0: return 0x20;          // nbsp → space
    default:     return GLYPH_QUESTION;
    }
}

/**
 * Декодувати один UTF-8 codepoint з буфера.
 * @param s    вказівник на початок послідовності
 * @param len  доступних байтів
 * @param out  [out] codepoint
 * @return     кількість спожитих байтів (>=1; 1 для невалідного — як U+FFFD-сурогат)
 */
inline size_t utf8_decode(const char* s, size_t len, uint32_t& out) {
    if (len == 0) { out = 0; return 0; }
    const uint8_t b0 = static_cast<uint8_t>(s[0]);

    if (b0 < 0x80) { out = b0; return 1; }

    size_t n;
    uint32_t cp;
    if      ((b0 & 0xE0) == 0xC0) { n = 2; cp = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { n = 3; cp = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { n = 4; cp = b0 & 0x07; }
    else { out = 0xFFFD; return 1; }   // невалідний lead-байт

    if (n > len) { out = 0xFFFD; return 1; }  // обрізана послідовність
    for (size_t i = 1; i < n; ++i) {
        const uint8_t bi = static_cast<uint8_t>(s[i]);
        if ((bi & 0xC0) != 0x80) { out = 0xFFFD; return 1; }  // битий continuation
        cp = (cp << 6) | (bi & 0x3F);
    }
    out = cp;
    return n;
}

/**
 * Перетворити UTF-8 рядок у масив індексів гліфів.
 * @param s        вхідний UTF-8 рядок (null-terminated)
 * @param out      [out] буфер індексів
 * @param out_cap  місткість буфера
 * @return         кількість записаних гліфів (<= out_cap)
 */
inline size_t osd_map_utf8(const char* s, uint8_t* out, size_t out_cap) {
    size_t w = 0;
    size_t i = 0;
    while (s[i] && w < out_cap) {
        // визначаємо довжину до null без strlen — рахуємо доступний хвіст
        size_t avail = 0;
        while (s[i + avail]) ++avail;
        uint32_t cp;
        const size_t used = utf8_decode(s + i, avail, cp);
        if (used == 0) break;
        out[w++] = osd_codepoint_to_glyph(cp);
        i += used;
    }
    return w;
}

} // namespace modesp::osd
