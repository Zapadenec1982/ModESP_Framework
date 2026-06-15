/**
 * @file at7456e_port.h
 * @brief IDisplayPort на AT7456E (символьний OSD composite-оверлей) — ADR-002.
 *
 * Адаптер: семантичний View → CharGridLayout(cols×rows) → індекси гліфів
 * (osd_charmap) → write_glyphs. Використовує ПОВНУ сітку чипа (PAL 30×16 /
 * NTSC 30×13), без 4-рядкового центрування. Роль SELECTED → маркер '>' (mono,
 * без per-char інверсії). Піни/відеостандарт — з Kconfig.
 *
 * Компілюється лише при CONFIG_MODESP_DISPLAY_AT7456E (залізо).
 */

#pragma once

#include "display/display_port.h"
#include "display/char_grid.h"
#include "modesp/osd/at7456e.h"

namespace modesp::display {

class At7456ePort : public IDisplayPort {
public:
    At7456ePort();

    bool init() override;
    DisplayCaps caps() const override { return {}; }   // mono OSD overlay — без кольору/підсвітки/входів

    void present_main(const MainView& view) override;
    void present_menu(const MenuView& view) override;
    void present_edit(const EditView& view) override;
    void present_notice(const Notice& notice) override;
    void clear_notice() override;

private:
    void render(const CharGrid& g);   ///< CharGrid → write_glyphs (маркер ролі у col 0)

    osd::At7456e   dev_;
    const uint8_t* font_              = nullptr;
    size_t         font_count_        = 0;
    int            font_sentinel_addr_ = -1;
    uint8_t        font_sentinel_b0_   = 0;
};

} // namespace modesp::display
