/**
 * @file renderer.h
 * @brief Апаратно-незалежний інтерфейс рендерера екранного меню
 *
 * MenuEngine формує текстовий кадр (DisplayFrame) — рядки UTF-8.
 * Конкретний драйвер дисплея (SSD1306, HD44780, TFT...) реалізує
 * IDisplayRenderer і малює кадр на залізі. Кількість видимих колонок
 * визначає рендерер (рядки кадру обмежені байтами, не гліфами).
 */

#pragma once

#include <cstddef>
#include "etl/string.h"

namespace modesp::display {

struct DisplayFrame {
    static constexpr size_t MAX_ROWS = 4;   // типовий OLED 128×64, шрифт 8px
    static constexpr size_t MAX_BYTES = 40; // UTF-8 байтів на рядок

    etl::string<MAX_BYTES> rows[MAX_ROWS];

    void clear() {
        for (auto& r : rows) r.clear();
    }

    bool operator==(const DisplayFrame& o) const {
        for (size_t i = 0; i < MAX_ROWS; ++i) {
            if (rows[i] != o.rows[i]) return false;
        }
        return true;
    }
    bool operator!=(const DisplayFrame& o) const { return !(*this == o); }
};

class IDisplayRenderer {
public:
    virtual ~IDisplayRenderer() = default;

    /// Ініціалізація заліза. true якщо дисплей готовий.
    virtual bool init() { return true; }

    /// Намалювати кадр. Викликається лише коли кадр змінився.
    virtual void render(const DisplayFrame& frame) = 0;
};

} // namespace modesp::display
