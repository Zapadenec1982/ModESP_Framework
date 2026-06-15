/**
 * @file char_grid.h
 * @brief Універсальний layout-движок для символьних дисплеїв (ADR-002, Крок 2).
 *
 * Чиста, апаратно-незалежна утиліта: бере СЕМАНТИЧНИЙ View (MainView/MenuView/
 * EditView) + цільові розміри сітки (cols × rows) і повертає CharGrid — рядки з
 * семантичними РОЛЯМИ (TITLE/SELECTED/HINT/NORMAL). Як роль ВИГЛЯДАЄ (колір,
 * інверсія, маркер курсора, масштаб) — вирішує драйвер-адаптер, НЕ ця утиліта.
 *
 * НЕ прив'язана до жодного чипа і НЕ відтворює legacy AT7456E: використовує
 * ПОВНУ задану сітку (без 4-рядкового центрування). Ширина — в ГЛІФАХ
 * (кодпойнтах), UTF-8-aware. Host-тестована, zero-heap (etl фіксованих розмірів).
 *
 * Використовують лише СИМВОЛЬНІ адаптери (At7456ePort, Amt630aPort-text).
 * Піксельний (SSD1306) і текстовий (LogPort) — НЕ використовують (опційна утиліта).
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include "etl/string.h"
#include "etl/vector.h"
#include "display/display_port.h"

namespace modesp::display {

/// Семантична роль рядка. Драйвер мапить у вигляд (колір/інверсія/маркер/масштаб).
enum class RowRole : uint8_t {
    NORMAL,    ///< звичайний пункт / значення
    TITLE,     ///< заголовок екрана
    SELECTED,  ///< вибраний пункт (курсор)
    HINT,      ///< підказка ([OK]…, діапазон редагування)
};

// Максимуми сітки — найбільший дисплей парку (AMT630A ~30×21 з запасом).
static constexpr uint8_t MAX_GRID_COLS = 40;
static constexpr uint8_t MAX_GRID_ROWS = 24;
// UTF-8 worst-case: до 3 байтів/гліф (кирилиця 2, рідкісні 3) + NUL.
static constexpr size_t  MAX_ROW_BYTES = static_cast<size_t>(MAX_GRID_COLS) * 3 + 1;

struct GridRow {
    etl::string<MAX_ROW_BYTES> text;                ///< UTF-8, ≤ cols гліфів (обрізано без розриву послідовності)
    RowRole                    role = RowRole::NORMAL;
};

struct CharGrid {
    uint8_t                             cols = 0;
    uint8_t                             rows = 0;
    etl::vector<GridRow, MAX_GRID_ROWS> lines;       ///< lines.size() ≤ rows

    void reset(uint8_t c, uint8_t r) {
        cols = (c > MAX_GRID_COLS) ? MAX_GRID_COLS : c;
        rows = (r > MAX_GRID_ROWS) ? MAX_GRID_ROWS : r;
        lines.clear();
    }
};

/// Чистий універсальний layout. Усі методи статичні, без стану.
class CharGridLayout {
public:
    /// Головний (idle) екран: список «label   value» на повну сітку.
    static void layout_main(const MainView& view, uint8_t cols, uint8_t rows, CharGrid& out);

    /// Меню: заголовок (TITLE) + скрольований список. Рядок view.selected → RowRole::SELECTED.
    /// Скрол-вікно рахується навколо selected під (rows-1) видимих рядків.
    static void layout_menu(const MenuView& view, uint8_t cols, uint8_t rows, CharGrid& out);

    /// Редагування: заголовок (TITLE) + значення (SELECTED) + діапазон (HINT).
    static void layout_edit(const EditView& view, uint8_t cols, uint8_t rows, CharGrid& out);

    /// К-сть гліфів (кодпойнтів) у UTF-8 рядку.
    static size_t glyph_len(const char* s);

    /// Додати до dst не більше max_glyphs гліфів з src (UTF-8-aware, без розриву).
    static void append_clamped(etl::istring& dst, const char* src, size_t max_glyphs);
};

} // namespace modesp::display
