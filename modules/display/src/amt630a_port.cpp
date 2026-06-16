/**
 * @file amt630a_port.cpp
 * @brief Amt630aPort — реалізація (компілюється лише з Kconfig-опцією).
 */

#include "sdkconfig.h"

#ifdef CONFIG_MODESP_DISPLAY_AMT630A

#include "display/amt630a_port.h"
#include "display/display_backend_registry.h"
#include "modesp/hal/hal.h"                 // HAL + Binding (board.json/bindings)
#include "modesp/osd/amt630a_font_data.h"
#include "modesp/osd/amt630a_charmap.h"
#include "driver/gpio.h"                   // power-gate load-switch
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"                 // vTaskDelay (cold-boot wait у init)
#include "esp_log.h"
#include <cstdio>

namespace modesp::display {

static const char* TAG = "amt630a_port";

namespace {
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

// Атрибут кольору за роллю: attr = (FG&7) | ((BG&7)<<4). BG=0 = ПРОЗОРИЙ — текст
// накладається на те, що позаду: backdrop=Чорний → чистий чорний; backdrop=Сніг +
// камера → відео під текстом (режим «відео+OSD»). Фон керується backdrop, не OSD.
constexpr uint8_t ATTR_NORMAL = 0x06;  // FG=color6 (білий), BG=0 (прозорий)
constexpr uint8_t ATTR_SELECT = 0x02;  // FG=color2 (хайлайт), BG=0 (прозорий)

inline uint8_t clamp_u8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }

// Неблокуюче відновлення після cold boot (power-gate)
constexpr uint32_t kBootDelayMs   = 300;    // чекати холодний старт чіпа перед першим I²C
constexpr uint32_t kBootTimeoutMs = 4000;   // не з'явився за цей час → здатися
constexpr uint16_t kFontChunk     = 4;      // гліфів за один on_update-цикл (менше = плавніше)
} // namespace

Amt630aPort::Amt630aPort(i2c_master_bus_handle_t bus, uint32_t freq_hz,
                         uint8_t cols, uint8_t rows, int8_t cal_x, int8_t cal_y, int power_gpio)
    : dev_(bus, freq_hz)
    , cols_(cols)
    , rows_(rows)
    , cal_x_(cal_x)
    , cal_y_(cal_y)
    , power_gpio_(power_gpio)
{}

bool Amt630aPort::init() {
    if (power_gpio_ >= 0) {                                     // power-gate: живлення чіпа ON на старті
        gpio_set_direction(static_cast<gpio_num_t>(power_gpio_), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(power_gpio_), 1);
        vTaskDelay(pdMS_TO_TICKS(kBootDelayMs));               // дочекатись cold boot чіпа
    }
    if (!dev_.init()) {
        ESP_LOGW(TAG, "AMT630A not responding — port disabled");
        return false;
    }
    configure_chip_();                                          // блокуюча конфіг — ок на старті
    ESP_LOGI(TAG, "ready: %ux%u, font %u glyphs%s", cols_, rows_,
             static_cast<unsigned>(osd::AMT630A_FONT_COUNT),
             power_gpio_ >= 0 ? " (power-gated)" : "");
    return true;
}

// ── Power-gate (load-switch на GPIO) + неблокуюче відновлення OSD ──
// set_rail(true): живлення ON → chunked-reconfig (OEM піднімає відео, ESP-side OSD втрачено при
// cold boot). set_rail(false): живлення OFF (0мА). Реконфіг chunked: жоден service()-крок не блокує
// надовго (на відміну від монолітного configure_chip_ ~5-7с, що трипить TWDT). I²C-handle-и ESP чинні.
void Amt630aPort::set_rail(bool on) {
    if (power_gpio_ < 0) { ESP_LOGW(TAG, "set_rail(%d): no power_gpio in bindings", on ? 1 : 0); return; }
    gpio_set_level(static_cast<gpio_num_t>(power_gpio_), on ? 1 : 0);
    ESP_LOGI(TAG, "rail %s (GPIO%d)", on ? "ON" : "OFF", power_gpio_);
    if (on) start_recovery_();
    else    recov_ = Recov::IDLE;     // живлення знято — відновлення не має сенсу
}

void Amt630aPort::start_recovery_() {
    if (recov_ != Recov::IDLE) return;   // вже триває
    recov_ = Recov::WAIT;
    recov_wait_ = 0;
    recov_cursor_ = 0;
    ESP_LOGI(TAG, "recovery scheduled (await cold boot)");
}

bool Amt630aPort::busy() const { return recov_ != Recov::IDLE; }

// Generic heartbeat: просуває внутрішнє chunked-відновлення на один крок. Модуль кличе щотіку.
// На завершенні recov_→IDLE → busy() стає false → модуль (на спаді) повторно подасть кадр+параметри.
void Amt630aPort::service(uint32_t dt_ms) {
    switch (recov_) {
    case Recov::IDLE:
        return;

    case Recov::WAIT: {
        recov_wait_ += dt_ms;
        if (recov_wait_ < kBootDelayMs) return;                // чекаємо cold boot ~300мс
        uint8_t v;                                             // ACK-чек звичайним read (НЕ probe)
        if (!dev_.amt_r(osd::amt_bank::OSD, 0x05, v)) {
            if (recov_wait_ >= kBootTimeoutMs) {
                recov_ = Recov::IDLE;
                ESP_LOGW(TAG, "recovery: chip absent after %ums — giving up", (unsigned)recov_wait_);
            }
            return;                                            // ще не на шині — чекаємо далі
        }
        recov_ = Recov::SETUP;
        return;
    }

    case Recov::SETUP:                                         // конфіг OSD (без шрифту), один крок
        dev_.apply_osd_init();                                // unlock + OSD-вікна (вкл. 50мс delay)
        dev_.set_palette(1, 10, 0, 0);
        dev_.set_palette(2, 10, 10, 0);
        win0_(cols_, rows_, 80, 36);                          // з калібровкою
        dev_.begin_font_upload(osd::AMT630A_FONT_XSIZ, osd::AMT630A_FONT_YSIZ,
                               static_cast<uint16_t>(osd::AMT630A_FONT_COUNT));  // вікна OFF + bitmap_start
        recov_cursor_ = 0;
        recov_ = Recov::FONT;                                  // гліфи з НАСТУПНОГО кадру (вікна вже OFF)
        return;

    case Recov::FONT: {                                        // заливка шрифту порціями
        const uint16_t total = static_cast<uint16_t>(osd::AMT630A_FONT_COUNT);
        const uint16_t left  = static_cast<uint16_t>(total - recov_cursor_);
        const uint16_t n     = (left < kFontChunk) ? left : kFontChunk;
        dev_.upload_font_chunk(osd::AMT630A_FONT, /*first_tile=*/0,
                               recov_cursor_, n, osd::AMT630A_FONT_YSIZ);
        recov_cursor_ = static_cast<uint16_t>(recov_cursor_ + n);
        if (recov_cursor_ >= total) {
            dev_.end_font_upload(0x00);                        // вікна OFF — present увімкне після BGMAP
            dev_.set_backlight(backlight_pct_);
            recov_ = Recov::IDLE;
            ESP_LOGI(TAG, "recovery done (%u glyphs)", (unsigned)total);
        }
        return;
    }
    }
}

// Конфігурація OSD поверх працюючої OEM-прошивки (спільне для init() і reinit()).
void Amt630aPort::configure_chip_() {
    // МІНІМАЛЬНИЙ OSD-init: лише unlock (0x5F) + OSD-вікна. БЕЗ відеобанків — відео OEM ціле.
    dev_.apply_osd_init();
    dev_.set_palette(1, 10, 0, 0);           // color1 = червоний (alarm) — OSD-банк, безпечно
    dev_.set_palette(2, 10, 10, 0);          // color2 = жовтий — OSD-банк
    // xloc/yloc — у ПІКСЕЛЯХ. Центруємо вікно на панелі 480×272 (overscan); win0_ додає калібровку.
    win0_(cols_, rows_, 80, 36);
    // Кириличний RAM-шрифт (16×20 1bpp) у FONT RAM; bitmap_start вмикає 1bpp.
    // restore_mask=0x00: вікна лишаємо ВИМКНЕНИМИ — перший present увімкне їх ПІСЛЯ запису BGMAP
    // (інакше на старті блимає сміття: кольорові квадрати / випадкові гліфи поверх порожнього BGMAP).
    dev_.upload_font(osd::AMT630A_FONT, /*first_tile=*/0,
                     static_cast<uint8_t>(osd::AMT630A_FONT_COUNT),
                     osd::AMT630A_FONT_XSIZ, osd::AMT630A_FONT_YSIZ, /*restore_mask=*/0x00);
    dev_.set_backlight(backlight_pct_);      // відновити поточну яскравість (не hardcode)
}

DisplayCaps Amt630aPort::caps() const {
    DisplayCaps c;
    c.has_color        = true;
    c.has_backlight    = true;
    c.has_video_params = true;
    c.has_inputs       = true;
    c.has_backdrop     = true;
    c.has_power        = (power_gpio_ >= 0);   // power-gate лише якщо binding дав power_gpio
    c.input_count      = 2;   // CVBS1 / CVBS3 (AV1/AV3)
    return c;
}

// Розмістити вікно зі зсувом overscan-калібровки (cal_x_/cal_y_), клемп у видимий діапазон.
void Amt630aPort::win0_(uint8_t cols, uint8_t rows, int x, int y) {
    dev_.window0_setup(cols, rows, clamp_u8(x + cal_x_), clamp_u8(y + cal_y_));
}
void Amt630aPort::win_(uint8_t win, uint8_t cols, uint8_t rows, int x, int y, uint16_t vram) {
    dev_.window_setup(win, cols, rows, clamp_u8(x + cal_x_), clamp_u8(y + cal_y_), vram);
}

void Amt630aPort::render(const CharGrid& g) {
    // present_main міг переналаштувати W0 (геометрія+масштаб ×2) і ввімкнути W1 —
    // повертаємо повноекранний W0 ×1 і вимикаємо решту вікон, інакше меню малюється
    // у крихітному масштабованому вікні головного екрана.
    win0_(cols_, rows_, 80, 36);
    dev_.set_window_scale(0, 1, 1);
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
        while (n < cols) tiles[n++] = 0x1C0; // добити RAM-пробілом (відомо порожній; ROM 0x00 — невідомий)
        dev_.osd_print(static_cast<uint16_t>(r * cols), tiles, cols, attr);
    }
    dev_.window_enable(0x01);   // W0 ON ПІСЛЯ запису BGMAP — кадр з'являється атомарно, без блимання
}

void Amt630aPort::present_main(const MainView& view) {
    // Головний (idle) екран: перший пункт — ВЕЛИКИМ у вікні W0 ×2, решта — дрібними
    // рядками у вікні W1 ×1. Кожне вікно читає свою ділянку BGMAP (W0=0x000, W1=0x040),
    // тому розміри незалежні. Кожен рядок добиваємо RAM-пробілом (0x1C0) на всю ширину
    // вікна — інакше старі тайли BGMAP лишаються «хвостом» (напр. "26.5C"→"26.5Cенц").
    constexpr uint8_t BIG_COLS   = 8;    // W0: 8 симв. × 16px × ×2 = 256px
    constexpr uint8_t SMALL_COLS = 20;   // W1: 20 симв. × 16px = 320px
    constexpr uint8_t SMALL_ROWS = 6;
    constexpr uint8_t LABEL_COL  = 13;   // де починається value у дрібному рядку

    // Записати рядок у BGMAP, доповнивши пробілами до width символів (рахуємо ДЕКОДОВАНІ).
    auto put = [&](uint16_t bgaddr, uint8_t width, const char* s) {
        uint16_t tiles[40];
        uint8_t n = 0;
        const char* p = s;
        while (*p && n < width && n < 40) tiles[n++] = osd::amt630a_cp_to_tile(decode_utf8(p));
        while (n < width && n < 40) tiles[n++] = 0x1C0;
        dev_.osd_print(bgaddr, tiles, n, ATTR_NORMAL);
    };
    // Дрібний рядок: label зліва, value+unit вирівняні на LABEL_COL.
    auto put_row = [&](uint16_t bgaddr, const char* label, const char* value) {
        uint16_t tiles[40];
        uint8_t n = 0;
        const char* p = label;
        while (*p && n < (LABEL_COL - 1) && n < SMALL_COLS) tiles[n++] = osd::amt630a_cp_to_tile(decode_utf8(p));
        while (n < LABEL_COL && n < SMALL_COLS) tiles[n++] = 0x1C0;
        p = value;
        while (*p && n < SMALL_COLS && n < 40) tiles[n++] = osd::amt630a_cp_to_tile(decode_utf8(p));
        while (n < SMALL_COLS && n < 40) tiles[n++] = 0x1C0;
        dev_.osd_print(bgaddr, tiles, n, ATTR_NORMAL);
    };

    // ── W0 ×2: головне значення (перший пункт) "<value><unit>" ──
    char big[24] = {0};
    if (!view.items.empty())
        snprintf(big, sizeof(big), "%s%s", view.items[0].value.c_str(), view.items[0].unit.c_str());
    win_(0, BIG_COLS, 1, /*x*/80, /*y*/12, /*vram*/0x000);
    dev_.set_window_scale(0, 2, 2);
    put(0x000, BIG_COLS, big);

    // ── W1 ×1: решта пунктів дрібними рядками ──
    win_(1, SMALL_COLS, SMALL_ROWS, /*x*/80, /*y*/64, /*vram*/0x040);
    dev_.set_window_scale(1, 1, 1);
    uint16_t addr = 0x040;
    uint8_t  row  = 0;
    const size_t first = view.items.empty() ? 0 : 1;   // пункт 0 вже показано великим
    for (size_t i = first; i < view.items.size() && row < SMALL_ROWS; ++i, ++row) {
        char val[24];
        snprintf(val, sizeof(val), "%s%s", view.items[i].value.c_str(), view.items[i].unit.c_str());
        put_row(addr, view.items[i].label.c_str(), val);
        addr += SMALL_COLS;
    }
    for (; row < SMALL_ROWS; ++row) { put(addr, SMALL_COLS, ""); addr += SMALL_COLS; }

    dev_.window_enable(0x03);   // W0 + W1
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

void Amt630aPort::set_backlight(uint8_t pct)  { backlight_pct_ = pct; dev_.set_backlight(pct); }
void Amt630aPort::set_contrast(uint8_t pct)   { dev_.set_contrast(pct); }
void Amt630aPort::set_brightness(uint8_t pct) { dev_.set_video_brightness(pct); }
void Amt630aPort::set_saturation(uint8_t pct) { dev_.set_saturation(pct); }

void Amt630aPort::set_backdrop(uint8_t mode) {
    dev_.set_backdrop(static_cast<osd::Amt630a::Backdrop>(mode));
}

void    Amt630aPort::select_input(uint8_t n)   { dev_.select_input(n); }
uint8_t Amt630aPort::input_count() const       { return 2; }

// ── Backend factory + реєстрація (board.json/bindings → IDisplayPort) ──
//
// bindings.json: {"hardware":"disp_0","driver":"amt630a","role":"display_main",
//                 "module":"display"} → DisplayModule зве цю фабрику.
// Піни/швидкість/геометрія — з board.json (i2c_buses + i2c_displays) через HAL.
namespace {
IDisplayPort* amt630a_factory(const modesp::Binding& b, modesp::HAL& hal) {
    auto* dcfg = hal.find_i2c_display(b.hardware_id);
    if (!dcfg) {
        ESP_LOGE(TAG, "no i2c_display '%s' in board.json", b.hardware_id.c_str());
        return nullptr;
    }
    auto* bus = hal.find_i2c_bus(dcfg->bus_id);
    if (!bus || !bus->bus_handle) {
        ESP_LOGE(TAG, "i2c bus '%s' not found for display '%s'",
                 dcfg->bus_id.c_str(), b.hardware_id.c_str());
        return nullptr;
    }
    // Дисплей — singleton: один статичний порт (zero heap), будується на місці.
    const int power_gpio = static_cast<int>(b.setting_or("power_gpio", -1.0f));  // load-switch (опц.)
    static Amt630aPort port(bus->bus_handle, bus->freq_hz, dcfg->cols, dcfg->rows,
                            dcfg->cal_x, dcfg->cal_y, power_gpio);
    return &port;
}
} // namespace

MODESP_REGISTER_DISPLAY(amt630a, &amt630a_factory)

} // namespace modesp::display

#endif // CONFIG_MODESP_DISPLAY_AMT630A
