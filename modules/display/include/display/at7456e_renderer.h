/**
 * @file at7456e_renderer.h
 * @brief IDisplayRenderer на AT7456E (OSD composite-оверлей).
 *
 * Міст між апаратно-незалежним DisplayFrame і переносним драйвером
 * modesp_osd: кожен рядок кадру UTF-8 → індекси гліфів (osd_charmap) →
 * write_glyphs. Піни й відеостандарт — з Kconfig.
 *
 * Компілюється лише при CONFIG_MODESP_DISPLAY_AT7456E.
 */

#pragma once

#include "display/renderer.h"
#include "modesp/osd/at7456e.h"

namespace modesp::display {

class AT7456ERenderer : public IDisplayRenderer {
public:
    AT7456ERenderer();

    bool init() override;
    void render(const DisplayFrame& frame) override;

    /// Необов'язково: дані власного шрифту (256×54) для одноразової заливки
    /// у NVM при init(). Якщо не задано — використовується наявний шрифт NVM.
    void set_font(const uint8_t* data, size_t char_count,
                  int sentinel_addr = -1, uint8_t sentinel_byte0 = 0);

private:
    osd::At7456e dev_;
    const uint8_t* font_      = nullptr;
    size_t         font_count_ = 0;
    int            font_sentinel_addr_ = -1;
    uint8_t        font_sentinel_b0_   = 0;
    uint8_t        top_row_   = 0;   // вертикальне центрування на сітці OSD
};

} // namespace modesp::display
