/**
 * @file at7456e_renderer.cpp
 * @brief AT7456E renderer — реалізація (компілюється лише з Kconfig-опцією).
 */

#include "sdkconfig.h"

#ifdef CONFIG_MODESP_DISPLAY_AT7456E

#include "display/at7456e_renderer.h"
#include "modesp/osd/osd_charmap.h"
#include "modesp/osd/osd_font_data.h"
#include "esp_log.h"

namespace modesp::display {

static const char* TAG = "at7456e_rndr";

namespace {
osd::At7456ePins kconfig_pins() {
    return osd::At7456ePins{
        CONFIG_MODESP_DISPLAY_AT7456E_CS,
        CONFIG_MODESP_DISPLAY_AT7456E_MOSI,
        CONFIG_MODESP_DISPLAY_AT7456E_CLK,
        CONFIG_MODESP_DISPLAY_AT7456E_MISO,
    };
}
osd::VideoStd kconfig_std() {
#ifdef CONFIG_MODESP_DISPLAY_AT7456E_NTSC
    return osd::VideoStd::NTSC;
#else
    return osd::VideoStd::PAL;
#endif
}
uint8_t kconfig_sync() {
#if   defined(CONFIG_MODESP_DISPLAY_AT7456E_SYNC_INTERNAL)
    return 1;
#elif defined(CONFIG_MODESP_DISPLAY_AT7456E_SYNC_EXTERNAL)
    return 2;
#else
    return 0;  // auto
#endif
}
} // namespace

AT7456ERenderer::AT7456ERenderer()
    : dev_(kconfig_pins(), kconfig_std()) {
    // Власний кириличний шрифт (gen_osd_font.py) — заливається в NVM при
    // init(), якщо sentinel не збігається (інакше пропуск, NVM не зношується).
    set_font(osd::OSD_FONT, osd::OSD_FONT_CHARS,
             osd::OSD_FONT_SENTINEL_ADDR, osd::OSD_FONT_SENTINEL_BYTE0);
}

void AT7456ERenderer::set_font(const uint8_t* data, size_t char_count,
                               int sentinel_addr, uint8_t sentinel_byte0) {
    font_ = data;
    font_count_ = char_count;
    font_sentinel_addr_ = sentinel_addr;
    font_sentinel_b0_ = sentinel_byte0;
}

bool AT7456ERenderer::init() {
    dev_.init();
    if (!dev_.present()) {
        ESP_LOGW(TAG, "AT7456E not responding — renderer disabled");
        return false;
    }

    if (font_ && font_count_) {
        dev_.upload_font(font_, font_count_, font_sentinel_addr_, font_sentinel_b0_);
    }

    dev_.set_sync(kconfig_sync());

    // Вертикальне центрування 4-рядкового кадру на сітці OSD (16/13 рядків).
    const uint8_t r = dev_.rows();
    top_row_ = (r > DisplayFrame::MAX_ROWS)
             ? static_cast<uint8_t>((r - DisplayFrame::MAX_ROWS) / 2)
             : 0;

    dev_.clear();
    dev_.enable_osd(true);
    ESP_LOGI(TAG, "ready: %ux%u, top_row=%u", dev_.cols(), dev_.rows(), top_row_);
    return true;
}

void AT7456ERenderer::render(const DisplayFrame& frame) {
    dev_.clear();
    for (uint8_t r = 0; r < DisplayFrame::MAX_ROWS; ++r) {
        const auto& row = frame.rows[r];
        if (row.empty()) continue;
        uint8_t idx[30];
        const size_t n = osd::osd_map_utf8(row.c_str(), idx, sizeof(idx));
        dev_.write_glyphs(/*col*/1, static_cast<uint8_t>(top_row_ + r), idx, n);
    }
}

} // namespace modesp::display

#endif // CONFIG_MODESP_DISPLAY_AT7456E
