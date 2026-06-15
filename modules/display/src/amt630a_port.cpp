/**
 * @file amt630a_port.cpp
 * @brief Amt630aPort — реалізація (компілюється лише з Kconfig-опцією).
 */

#include "sdkconfig.h"

#ifdef CONFIG_MODESP_DISPLAY_AMT630A

#include "display/amt630a_port.h"
#include "modesp/osd/amt630a_font_data.h"
#include "modesp/osd/amt630a_charmap.h"
#include "esp_log.h"

namespace modesp::display {

static const char* TAG = "amt630a_port";

namespace {
osd::Amt630aPins kconfig_pins() {
    return osd::Amt630aPins{
        CONFIG_MODESP_DISPLAY_AMT630A_SDA,
        CONFIG_MODESP_DISPLAY_AMT630A_SCL,
        static_cast<uint32_t>(CONFIG_MODESP_DISPLAY_AMT630A_FREQ),
    };
}

/// UTF-8 → кодпойнт (просуває p).
uint32_t decode_utf8(const char*& p) {
    const uint8_t c = static_cast<uint8_t>(*p++);
    uint32_t cp;
    int n;
    if      (c < 0x80)        { cp = c;        n = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
    else                      { return c; }
    for (int i = 1; i < n && (static_cast<uint8_t>(*p) & 0xC0) == 0x80; ++i)
        cp = (cp << 6) | (static_cast<uint8_t>(*p++) & 0x3F);
    return cp;
}

// Атрибут кольору за роллю: fg у bit0-2, bg у bit4-6 (0=прозорий).
constexpr uint8_t ATTR_NORMAL = 0x06;  // fg=color6 (білий, ROM-default)
constexpr uint8_t ATTR_SELECT = 0x02;  // fg=color2 (хайлайт)
} // namespace

Amt630aPort::Amt630aPort()
    : dev_(kconfig_pins())
    , cols_(static_cast<uint8_t>(CONFIG_MODESP_DISPLAY_AMT630A_COLS))
    , rows_(static_cast<uint8_t>(CONFIG_MODESP_DISPLAY_AMT630A_ROWS))
{}

bool Amt630aPort::init() {
    if (!dev_.init()) {
        ESP_LOGW(TAG, "AMT630A not responding — port disabled");
        return false;
    }
    dev_.apply_init_table();                 // bring-up поверх OEM (bench: пропустити, якщо OEM сама)
    dev_.set_palette(1, 10, 0, 0);           // color1 = червоний (alarm)
    dev_.set_palette(2, 10, 10, 0);          // color2 = жовтий (highlight/warn)
    dev_.window0_setup(cols_, rows_, 10, 12);
    // A1: залити кириличний RAM-шрифт (16×22 1bpp) у FONT RAM; restore_mask вмикає вікно 0.
    dev_.upload_font(osd::AMT630A_FONT, /*first_tile=*/0,
                     static_cast<uint8_t>(osd::AMT630A_FONT_COUNT),
                     osd::AMT630A_FONT_XSIZ, osd::AMT630A_FONT_YSIZ, /*restore_mask=*/0x01);
    dev_.set_backlight(70);                  // amt-8: підсвітка після bring-up (init ставить duty=0!) ⚠ полярність — bench
    ESP_LOGI(TAG, "ready: %ux%u, font %u glyphs", cols_, rows_,
             static_cast<unsigned>(osd::AMT630A_FONT_COUNT));
    return true;
}

DisplayCaps Amt630aPort::caps() const {
    DisplayCaps c;
    c.has_color        = true;
    c.has_backlight    = true;
    c.has_video_params = true;
    c.has_inputs       = true;
    c.input_count      = 2;   // CVBS1 / CVBS3 (AV1/AV3)
    return c;
}

void Amt630aPort::render(const CharGrid& g) {
    // mem-2: ітеруємо КЛЕМПЛЕНУ сітку (g.cols/g.rows), а не сирі Kconfig cols_/rows_.
    uint16_t tiles[64];
    const uint8_t cols = (g.cols > 64) ? 64 : g.cols;
    for (uint8_t r = 0; r < g.rows; ++r) {
        const GridRow* ln = (r < g.lines.size()) ? &g.lines[r] : nullptr;
        uint8_t attr = ATTR_NORMAL;
        size_t n = 0;
        if (ln) {
            if (ln->role == RowRole::SELECTED) attr = ATTR_SELECT;
            const char* p = ln->text.c_str();
            while (*p && n < cols) tiles[n++] = osd::amt630a_cp_to_tile(decode_utf8(p));
        }
        while (n < cols) tiles[n++] = 0x00;   // добити пробілами (очистити рядок)
        dev_.osd_print(static_cast<uint16_t>(r * cols), tiles, cols, attr);
    }
}

void Amt630aPort::present_main(const MainView& view) {
    CharGrid g; CharGridLayout::layout_main(view, cols_, rows_, g); render(g);
}
void Amt630aPort::present_menu(const MenuView& view) {
    CharGrid g; CharGridLayout::layout_menu(view, cols_, rows_, g); render(g);
}
void Amt630aPort::present_edit(const EditView& view) {
    CharGrid g; CharGridLayout::layout_edit(view, cols_, rows_, g); render(g);
}

void Amt630aPort::present_notice(const Notice& notice) {
    // Мінімальний банер у нижньому рядку, колір за рівнем (TODO bench: окреме вікно W1 поверх).
    uint16_t tiles[64];
    const uint8_t cols = (cols_ > 64) ? 64 : cols_;
    uint8_t attr = ATTR_NORMAL;
    if (notice.level == NoticeLevel::ALARM) attr = 0x01;       // color1 червоний
    else if (notice.level == NoticeLevel::WARN) attr = 0x02;   // color2 жовтий
    size_t n = 0;
    const char* p = notice.text.c_str();
    while (*p && n < cols) tiles[n++] = osd::amt630a_cp_to_tile(decode_utf8(p));
    while (n < cols) tiles[n++] = 0x00;
    dev_.osd_print(static_cast<uint16_t>((rows_ - 1) * cols), tiles, cols, attr);
}

void Amt630aPort::clear_notice() {
    uint16_t tiles[64];
    const uint8_t cols = (cols_ > 64) ? 64 : cols_;
    for (uint8_t i = 0; i < cols; ++i) tiles[i] = 0x00;
    dev_.osd_print(static_cast<uint16_t>((rows_ - 1) * cols), tiles, cols, ATTR_NORMAL);
}

void Amt630aPort::set_backlight(uint8_t pct)  { dev_.set_backlight(pct); }
void Amt630aPort::set_contrast(uint8_t pct)   { dev_.set_contrast(pct); }
void Amt630aPort::set_brightness(uint8_t pct) { dev_.set_video_brightness(pct); }
void Amt630aPort::set_saturation(uint8_t pct) { dev_.set_saturation(pct); }

void    Amt630aPort::select_input(uint8_t n)   { dev_.select_input(n); }
uint8_t Amt630aPort::input_count() const       { return 2; }

} // namespace modesp::display

#endif // CONFIG_MODESP_DISPLAY_AMT630A
