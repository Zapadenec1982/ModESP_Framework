/**
 * @file at7456e_port.cpp
 * @brief At7456ePort — реалізація (компілюється лише з Kconfig-опцією).
 */

#include "sdkconfig.h"

#ifdef CONFIG_MODESP_DISPLAY_AT7456E

#include "display/at7456e_port.h"
#include "display/display_backend_registry.h"
#include "modesp/osd/osd_charmap.h"
#include "modesp/osd/osd_font_data.h"
#include "esp_log.h"

namespace modesp::display {

static const char* TAG = "at7456e_port";

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

At7456ePort::At7456ePort()
    : dev_(kconfig_pins(), kconfig_std())
    , font_(osd::OSD_FONT)
    , font_count_(osd::OSD_FONT_CHARS)
    , font_sentinel_addr_(osd::OSD_FONT_SENTINEL_ADDR)
    , font_sentinel_b0_(osd::OSD_FONT_SENTINEL_BYTE0)
{}

bool At7456ePort::init() {
    dev_.init();
    if (!dev_.present()) {
        ESP_LOGW(TAG, "AT7456E not responding — port disabled");
        return false;
    }
    if (font_ && font_count_) {
        dev_.upload_font(font_, font_count_, font_sentinel_addr_, font_sentinel_b0_);
    }
    dev_.set_sync(kconfig_sync());
    dev_.clear();
    dev_.enable_osd(true);
    ESP_LOGI(TAG, "ready: %ux%u", dev_.cols(), dev_.rows());
    return true;
}

void At7456ePort::render(const CharGrid& g) {
    dev_.clear();
    for (size_t r = 0; r < g.lines.size(); ++r) {
        const GridRow& ln = g.lines[r];
        // col 0 — маркер ролі (mono: SELECTED → '>'), col 1+ — текст
        const char* marker = (ln.role == RowRole::SELECTED) ? ">" : " ";
        uint8_t midx[2];
        size_t mn = osd::osd_map_utf8(marker, midx, sizeof(midx));
        dev_.write_glyphs(0, static_cast<uint8_t>(r), midx, mn);

        uint8_t idx[30];
        size_t n = osd::osd_map_utf8(ln.text.c_str(), idx, sizeof(idx));
        dev_.write_glyphs(1, static_cast<uint8_t>(r), idx, n);
    }
}

void At7456ePort::present_main(const MainView& view) {
    CharGrid g;
    CharGridLayout::layout_main(view, static_cast<uint8_t>(dev_.cols() - 1), dev_.rows(), g);
    render(g);
}

void At7456ePort::present_menu(const MenuView& view) {
    CharGrid g;
    CharGridLayout::layout_menu(view, static_cast<uint8_t>(dev_.cols() - 1), dev_.rows(), g);
    render(g);
}

void At7456ePort::present_edit(const EditView& view) {
    CharGrid g;
    CharGridLayout::layout_edit(view, static_cast<uint8_t>(dev_.cols() - 1), dev_.rows(), g);
    render(g);
}

void At7456ePort::present_notice(const Notice& notice) {
    // Мінімальний банер: нижній рядок поверх меню (mono, без шарів вікон).
    // TODO(bench): зарезервувати рядок у layout, коли notice активний.
    const uint8_t row = static_cast<uint8_t>(dev_.rows() - 1);
    uint8_t idx[30];
    size_t n = osd::osd_map_utf8(notice.text.c_str(), idx, sizeof(idx));
    dev_.write_glyphs(0, row, idx, n);
}

void At7456ePort::clear_notice() {
    const uint8_t row = static_cast<uint8_t>(dev_.rows() - 1);
    char blanks[31];
    for (int i = 0; i < 30; ++i) blanks[i] = ' ';
    blanks[30] = '\0';
    uint8_t idx[30];
    size_t n = osd::osd_map_utf8(blanks, idx, sizeof(idx));
    dev_.write_glyphs(0, row, idx, n);
}

// ── Backend factory + реєстрація ──
// AT7456E — SPI; піни наразі з Kconfig (TODO: board.json spi_buses, окрема
// задача). Binding/HAL не використовуються — backend обирається полем "driver".
namespace {
IDisplayPort* at7456e_factory(const modesp::Binding&, modesp::HAL&) {
    static At7456ePort port;   // singleton, zero heap
    return &port;
}
} // namespace

MODESP_REGISTER_DISPLAY(at7456e, &at7456e_factory)

} // namespace modesp::display

#endif // CONFIG_MODESP_DISPLAY_AT7456E
