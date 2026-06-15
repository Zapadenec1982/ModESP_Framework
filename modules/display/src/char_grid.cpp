/**
 * @file char_grid.cpp
 * @brief Реалізація універсального layout-движка (ADR-002, Крок 2).
 *
 * Чисто, без прив'язки до заліза: семантичний View + (cols, rows) → CharGrid
 * з ролями. Ширина рахується в ГЛІФАХ (кодпойнтах), обрізання UTF-8-aware.
 */

#include "display/char_grid.h"

namespace modesp::display {

// Довжина UTF-8 послідовності за провідним байтом.
static inline size_t seq_len(uint8_t lead) {
    if ((lead & 0x80) == 0x00) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;  // невалідний lead — рахуємо як 1 байт
}

size_t CharGridLayout::glyph_len(const char* s) {
    size_t n = 0;
    while (s && *s) {
        s += seq_len(static_cast<uint8_t>(*s));
        ++n;
    }
    return n;
}

void CharGridLayout::append_clamped(etl::istring& dst, const char* src, size_t max_glyphs) {
    size_t added = 0;
    while (src && *src && added < max_glyphs) {
        const size_t len = seq_len(static_cast<uint8_t>(*src));
        if (dst.size() + len > dst.capacity()) break;
        for (size_t i = 0; i < len && *src; ++i) dst.push_back(*src++);
        ++added;
    }
}

// ── приватні хелпери ──

static void append_spaces(etl::istring& dst, size_t n) {
    while (n-- > 0 && dst.size() < dst.capacity()) dst.push_back(' ');
}

/// Рядок «label …пробіли… value» (value вирівняно праворуч) у межах cols гліфів.
static void build_kv(GridRow& row, const char* label, const char* value, uint8_t cols) {
    const size_t valW = CharGridLayout::glyph_len(value);
    if (valW == 0) {
        CharGridLayout::append_clamped(row.text, label, cols);
        return;
    }
    if (valW + 1 >= cols) {
        // значення майже заповнює рядок — лишаємо лише його
        CharGridLayout::append_clamped(row.text, value, cols);
        return;
    }
    const size_t labelMax = static_cast<size_t>(cols) - valW - 1;  // ≥1 пробіл-розрив
    const size_t labelW   = CharGridLayout::glyph_len(label);
    const size_t labelShown = (labelW < labelMax) ? labelW : labelMax;
    CharGridLayout::append_clamped(row.text, label, labelShown);
    const size_t pad = static_cast<size_t>(cols) - labelShown - valW;  // ≥1
    append_spaces(row.text, pad);
    CharGridLayout::append_clamped(row.text, value, valW);
}

// ── layout ──

void CharGridLayout::layout_main(const MainView& view, uint8_t cols, uint8_t rows, CharGrid& out) {
    out.reset(cols, rows);
    const uint8_t C = out.cols;
    const uint8_t R = out.rows;

    size_t r = 0;
    for (size_t i = 0; i < view.items.size() && r < R; ++i, ++r) {
        const MainItem& it = view.items[i];

        etl::string<48> val;
        append_clamped(val, it.value.c_str(), 48);
        if (!it.unit.empty()) {
            if (val.size() + 1 < val.capacity()) val.push_back(' ');
            append_clamped(val, it.unit.c_str(), 48);
        }

        GridRow row;
        build_kv(row, it.label.c_str(), val.c_str(), C);
        row.role = RowRole::NORMAL;
        out.lines.push_back(row);
    }

    if (out.lines.empty()) {
        GridRow row;
        append_clamped(row.text, "ModESP", C);
        row.role = RowRole::TITLE;
        out.lines.push_back(row);
    }
}

void CharGridLayout::layout_menu(const MenuView& view, uint8_t cols, uint8_t rows, CharGrid& out) {
    out.reset(cols, rows);
    const uint8_t C = out.cols;
    const uint8_t R = out.rows;

    // Рядок 0 — заголовок.
    {
        GridRow row;
        row.role = RowRole::TITLE;
        append_clamped(row.text, view.title.c_str(), C);
        out.lines.push_back(row);
    }
    if (R <= 1) return;

    const size_t total   = view.items.size();
    const size_t visible = static_cast<size_t>(R - 1);

    // Stateless-скрол: тримати selected у видимому вікні (selected притискається до низу).
    size_t first = 0;
    if (total > visible) {
        first = (view.selected < visible)
              ? 0
              : (static_cast<size_t>(view.selected) - visible + 1);
        const size_t max_first = total - visible;
        if (first > max_first) first = max_first;
    }

    for (size_t i = first; i < total && out.lines.size() < R; ++i) {
        const MenuItem& it = view.items[i];
        GridRow row;
        row.role = (i == view.selected) ? RowRole::SELECTED : RowRole::NORMAL;

        if (it.is_submenu || it.is_back || it.value.empty()) {
            append_clamped(row.text, it.label.c_str(), C);
        } else {
            build_kv(row, it.label.c_str(), it.value.c_str(), C);
        }
        out.lines.push_back(row);
    }
}

void CharGridLayout::layout_edit(const EditView& view, uint8_t cols, uint8_t rows, CharGrid& out) {
    out.reset(cols, rows);
    const uint8_t C = out.cols;
    const uint8_t R = out.rows;

    if (R >= 1) {
        GridRow row; row.role = RowRole::TITLE;
        append_clamped(row.text, view.title.c_str(), C);
        out.lines.push_back(row);
    }
    if (R >= 2) {
        GridRow row; row.role = RowRole::SELECTED;
        append_clamped(row.text, view.value.c_str(), C);
        out.lines.push_back(row);
    }
    if (R >= 3 && !view.range.empty()) {
        GridRow row; row.role = RowRole::HINT;
        append_clamped(row.text, view.range.c_str(), C);
        out.lines.push_back(row);
    }
}

} // namespace modesp::display
