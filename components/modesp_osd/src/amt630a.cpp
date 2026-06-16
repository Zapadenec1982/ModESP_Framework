/**
 * @file amt630a.cpp
 * @brief AMT630A I²C-драйвер — реалізація (ESP-IDF i2c_master).
 */

#include "modesp/osd/amt630a.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

namespace modesp::osd {

static const char* TAG = "amt630a";
static constexpr int I2C_TIMEOUT_MS = 50;

// ── I²C bring-up ───────────────────────────────────────────────

bool Amt630a::init() {
    if (!bus_) {
        ESP_LOGE(TAG, "no I2C bus handle (HAL did not provide one)");
        return false;
    }

    auto add = [&](uint8_t addr, i2c_master_dev_handle_t& h) {
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address  = addr;
        dev_cfg.scl_speed_hz    = freq_hz_;
        return i2c_master_bus_add_device(bus_, &dev_cfg, &h) == ESP_OK;
    };
    bool okd = add(amt_bank::GLOBAL, h58_) && add(amt_bank::AV, h59_)
            && add(amt_bank::VIDEO, h5A_) && add(amt_bank::OSD, h5B_)
            && add(amt_bank::LCD, h5C_)  && add(amt_bank::INIT, h5F_);
    if (!okd) { ESP_LOGE(TAG, "add_device failed"); return false; }

    ok_ = present();
    if (!ok_) ESP_LOGW(TAG, "AMT630A not ACKing on 0x5B");
    return ok_;
}

bool Amt630a::present() {
    return bus_ && i2c_master_probe(bus_, amt_bank::OSD, I2C_TIMEOUT_MS) == ESP_OK;
}

i2c_master_dev_handle_t Amt630a::handle_for(uint8_t dev7) const {
    switch (dev7) {
    case amt_bank::GLOBAL: return h58_;
    case amt_bank::AV:     return h59_;
    case amt_bank::VIDEO:  return h5A_;
    case amt_bank::OSD:    return h5B_;
    case amt_bank::LCD:    return h5C_;
    case amt_bank::INIT:   return h5F_;
    default:               return nullptr;
    }
}

bool Amt630a::raw_w(uint8_t dev7, uint8_t reg, uint8_t val) {
    auto h = handle_for(dev7);
    if (!h) return false;
    const uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(h, buf, sizeof(buf), I2C_TIMEOUT_MS) == ESP_OK;
}

// DANGER-регістри (design doc §10): SPI-flash / hang. НЕ писати в runtime.
bool Amt630a::is_danger(uint8_t dev7, uint8_t reg) {
    if (dev7 == amt_bank::GLOBAL) {           // FDxx
        if (reg == 0xD0 || reg == 0xDE || reg == 0xE0) return true;  // SPI transfer/hang
        if (reg >= 0xE5 && reg <= 0xE7) return true;
        if (reg == 0x32 || reg == 0x33) return true;                // SPI-flash піни
        if (reg == 0x01 || reg == 0x11 || reg == 0x12 || reg == 0x17) return true; // PLL
        if (reg == 0x40 || reg == 0x41) return true;                // KillTft/StopDotClk
    }
    if (dev7 == amt_bank::LCD) return true;   // FCxx Tcon — не чіпати
    return false;
}

bool Amt630a::amt_w(uint8_t dev7, uint8_t reg, uint8_t val) {
    if (is_danger(dev7, reg)) {
        ESP_LOGW(TAG, "blocked DANGER write 0x%02X:0x%02X", dev7, reg);
        return false;
    }
    return raw_w(dev7, reg, val);
}

bool Amt630a::amt_r(uint8_t dev7, uint8_t reg, uint8_t& out) {
    auto h = handle_for(dev7);
    if (!h) return false;
    return i2c_master_transmit_receive(h, &reg, 1, &out, 1, I2C_TIMEOUT_MS) == ESP_OK;
}

// ── Доведена init-послідовність (Fizik AMT630.h) — поза guard ──

void Amt630a::apply_init_table() {
    struct RW { uint8_t dev, reg, val; };
    // initDisplay() — unlock + standby
    static const RW kInit[] = {
        {0x5F,0xAF,0x00},{0x5F,0xA1,0x55},{0x5F,0xA2,0xAA},{0x5F,0xA3,0x03},{0x5F,0xA4,0x50},
        {0x5F,0xA5,0x00},{0x5F,0xA6,0x53},{0x5F,0xAF,0x11},{0x5F,0xC6,0x42},{0x5F,0xC6,0x00},
        {0x58,0x42,0x03},{0x58,0x1F,0x03},{0x58,0x28,0x00},{0x58,0x29,0x00},{0x58,0x11,0x1F},
        {0x58,0x12,0x38},{0x58,0x13,0x00},{0x59,0xDC,0x00},{0x5A,0xD2,0x54},{0x59,0xD7,0xFC},
        {0x5A,0xB0,0x00},{0x5B,0x05,0x1F},{0x5F,0xBE,0x55},{0x5F,0xBA,0x00},{0x5F,0xBE,0xAA},
    };
    // onDisplay() — увімкнути відео + OSD
    static const RW kOn[] = {
        {0x58,0x11,0xFF},{0x58,0x12,0xFF},{0x58,0x13,0xFF},{0x59,0x07,0x01},{0x59,0x11,0x01},
        {0x59,0xDC,0x20},{0x5A,0xD2,0x4F},{0x59,0xCD,0x00},{0x59,0x01,0x06},{0x59,0x04,0x80},
        {0x59,0x05,0x30},{0x59,0x54,0x40},{0x59,0x8A,0x0E},{0x59,0x8B,0x08},{0x59,0xA4,0x78},
        {0x59,0xA7,0x08},{0x59,0xA8,0xD9},{0x59,0xA9,0x57},{0x59,0xAB,0xCE},{0x59,0xAC,0xFF},
        {0x59,0xAD,0x02},{0x59,0xAF,0x88},{0x59,0xB0,0xFE},{0x59,0xB1,0x06},{0x59,0xB4,0xDE},
        {0x59,0xCB,0x10},{0x59,0xD7,0xE7},{0x59,0xD8,0xA3},{0x59,0xDD,0x4D},{0x59,0xDE,0x5A},
        {0x59,0xE0,0x51},{0x59,0xE1,0x67},{0x59,0xE3,0x01},{0x5A,0xB0,0xA3},{0x5A,0xB2,0x1C},
        {0x5A,0xB3,0x1C},{0x5A,0xB4,0x1C},{0x5A,0xD3,0x80},{0x5A,0xD4,0x80},{0x5A,0xD6,0x56},
        {0x5A,0xDA,0x6C},{0x5A,0xF0,0x02},{0x5A,0xF1,0xF1},{0x5A,0xF2,0x13},{0x5A,0xF3,0xDB},
        {0x5A,0xF4,0xCD},{0x5A,0xF5,0x19},{0x5A,0xF6,0x1B},{0x5A,0xF7,0xEA},{0x5A,0xF8,0x0F},
        {0x5A,0xF9,0x31},{0x5A,0xFA,0x19},{0x5B,0x05,0x1F},{0x58,0x19,0x08},{0x58,0x42,0x03},
        {0x58,0x1F,0x03},
    };
    for (const auto& w : kInit) raw_w(w.dev, w.reg, w.val);
    vTaskDelay(pdMS_TO_TICKS(200));
    for (const auto& w : kOn) raw_w(w.dev, w.reg, w.val);
}

void Amt630a::apply_osd_init() {
    // Лише vendor-unlock (банк 0x5F) + увімкнення OSD-вікон (FB05). БЕЗ відеобанків
    // (0x58/0x59/0x5A) — щоб не робити off→on відео й не псувати робоче OEM-зображення.
    struct RW { uint8_t dev, reg, val; };
    static const RW seq[] = {
        {0x5F,0xAF,0x00},{0x5F,0xA1,0x55},{0x5F,0xA2,0xAA},{0x5F,0xA3,0x03},{0x5F,0xA4,0x50},
        {0x5F,0xA5,0x00},{0x5F,0xA6,0x53},{0x5F,0xAF,0x11},{0x5F,0xC6,0x42},{0x5F,0xC6,0x00},
        {0x5B,0x05,0x1F},                                   // OSD вікна ON
        {0x5F,0xBE,0x55},{0x5F,0xBA,0x00},{0x5F,0xBE,0xAA},  // commit
    };
    for (const auto& w : seq) raw_w(w.dev, w.reg, w.val);
    vTaskDelay(pdMS_TO_TICKS(50));
}

// ── OSD ────────────────────────────────────────────────────────

void Amt630a::set_char_size(uint8_t xsiz, uint8_t ysiz) {
    amt_w(amt_bank::OSD, 0x76, xsiz);
    amt_w(amt_bank::OSD, 0x77, ysiz);
}

void Amt630a::window0_setup(uint8_t cols, uint8_t rows, uint8_t x, uint8_t y) {
    amt_w(amt_bank::OSD, 0x07, cols);   // size_x
    amt_w(amt_bank::OSD, 0x08, rows);   // size_y
    amt_w(amt_bank::OSD, 0x09, 0x00);   // xyloc msb
    amt_w(amt_bank::OSD, 0x0A, x);      // xloc lsb
    amt_w(amt_bank::OSD, 0x0B, y);      // yloc lsb
}

void Amt630a::window_enable(uint8_t mask) {
    amt_w(amt_bank::OSD, 0x05, mask);
}

void Amt630a::window_setup(uint8_t win, uint8_t cols, uint8_t rows, uint8_t x, uint8_t y, uint16_t vramaddr) {
    // Реєстри per-window (design doc §3.4): {size_x, size_y, xyloc_msb, xloc, yloc, vramaddr_lsb}.
    static const uint8_t R[5][6] = {
        {0x07,0x08,0x09,0x0A,0x0B,0xFF},   // W0 — vramaddr фікс 000h (0xFF = немає регістра)
        {0x12,0x13,0x14,0x15,0x16,0x17},   // W1
        {0x18,0x19,0x1A,0x1B,0x1C,0x1D},   // W2
        {0x1E,0x1F,0x20,0x21,0x22,0x23},   // W3
        {0x24,0x25,0x26,0x27,0x28,0x29},   // W4
    };
    if (win > 4) return;
    const uint8_t* r = R[win];
    amt_w(amt_bank::OSD, r[0], cols);
    amt_w(amt_bank::OSD, r[1], rows);
    // xyloc_msb: старший біт vramaddr → bit7 (⚠ повна розкладка xloc/yloc msb — bench 12.7;
    // для x,y<256 лишаємо 0).
    amt_w(amt_bank::OSD, r[2], static_cast<uint8_t>(((vramaddr >> 8) & 0x01) << 7));
    amt_w(amt_bank::OSD, r[3], x);
    amt_w(amt_bank::OSD, r[4], y);
    if (r[5] != 0xFF) amt_w(amt_bank::OSD, r[5], static_cast<uint8_t>(vramaddr & 0xFF));
}

void Amt630a::set_window_scale(uint8_t win, uint8_t scale_x, uint8_t scale_y) {
    if (win == 0) {
        // Вікно 0: FB32 = (scale-1), просте 0..3 = ×1..×4 (демо ENGELS 8588h; УНІФОРМНИЙ множник).
        // Для scale≥2 ще заповнити per-line маски FB2B-2D (vscale) + FB2F-31 (hscale) = 0xFF.
        uint8_t s = (scale_x > scale_y) ? scale_x : scale_y;   // вікно 0 масштабує однаково по X/Y
        if (s < 1) s = 1;
        if (s > 4) s = 4;
        const uint8_t mask = (s >= 2) ? 0xFF : 0x00;
        amt_w(amt_bank::OSD, 0x2B, mask); amt_w(amt_bank::OSD, 0x2C, mask); amt_w(amt_bank::OSD, 0x2D, mask);
        amt_w(amt_bank::OSD, 0x2F, mask); amt_w(amt_bank::OSD, 0x30, mask); amt_w(amt_bank::OSD, 0x31, mask);
        amt_w(amt_bank::OSD, 0x32, static_cast<uint8_t>(s - 1));   // 0=×1,1=×2,2=×3,3=×4
        return;
    }
    if (win > 4) return;
    // Вікна 1-4: 2×2-біт на нібл (ScaleX bit0-1, ScaleY bit2-3). ×2×2 = 0x05.
    const uint8_t sx = (scale_x ? scale_x - 1 : 0) & 0x03;
    const uint8_t sy = (scale_y ? scale_y - 1 : 0) & 0x03;
    const uint8_t s  = static_cast<uint8_t>(sx | (sy << 2));
    // W1/W2 → FB33, W3/W4 → FB34; парне вікно (W2,W4) у старшому ніблі. Тінь проти OEM-бага 12.6.
    const uint8_t reg = (win <= 2) ? 0x33 : 0x34;
    const bool    hi  = (win == 2 || win == 4);
    uint8_t& sh = (reg == 0x33) ? sh_fb33_ : sh_fb34_;
    sh = hi ? static_cast<uint8_t>((sh & 0x0F) | (s << 4))
            : static_cast<uint8_t>((sh & 0xF0) | s);
    amt_w(amt_bank::OSD, reg, sh);
}

void Amt630a::set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    if (idx < 1 || idx > 6) return;
    const uint8_t base = 0x56 + (idx - 1) * 2;
    // amt-1: спека §2.3 — парний(base)=MSB=Blue, непарний(base+1)=LSB=(G<<4)|R. ⚠ звірити на залізі.
    amt_w(amt_bank::OSD, base,     static_cast<uint8_t>(b & 0x0F));                         // MSB: Blue
    amt_w(amt_bank::OSD, base + 1, static_cast<uint8_t>((r & 0x0F) | ((g & 0x0F) << 4)));   // LSB: G|R
}

void Amt630a::osd_print(uint16_t bgmap_addr, const uint16_t* tiles, size_t n, uint8_t attr) {
    uint16_t addr = bgmap_addr;
    amt_w(amt_bank::OSD, 0x0D, static_cast<uint8_t>((addr >> 8) & 0x01));  // addr msb (9-біт BGMAP)
    amt_w(amt_bank::OSD, 0x00, static_cast<uint8_t>(addr & 0xFF));         // addr lsb (autoinc на data)
    for (size_t i = 0; i < n; ++i) {
        amt_w(amt_bank::OSD, 0x0E, static_cast<uint8_t>((tiles[i] >> 8) & 0x03));  // data msb (біти 8-9)
        amt_w(amt_bank::OSD, 0x01, static_cast<uint8_t>(tiles[i] & 0xFF));         // data lsb → VRAM, addr++
        amt_w(amt_bank::OSD, 0x10, attr);  // атрибут НА КОЖНУ клітинку (порядок FIZIK):
                                           // attr-once фарбував col0 старим атрибутом (1-cell lag)
        ++addr;
        // amt-2: перенос lsb→msb апаратно НЕ робиться — після обертання lsb перевиставити addr.
        if ((addr & 0xFF) == 0x00) {
            amt_w(amt_bank::OSD, 0x0D, static_cast<uint8_t>((addr >> 8) & 0x01));
            amt_w(amt_bank::OSD, 0x00, 0x00);
        }
    }
}

// ── BRING-UP self-test (точна копія FIZIK osdPrint) ───────────────
// Друкує ASCII вбудованим шрифтом чіпа на low-кодах (0x00..0x24), без RAM-шрифту.
// Підтверджує, що OSD/W0/позиція/рендер працюють незалежно від 0x1C0+.
void Amt630a::selftest_print(uint8_t x, uint8_t y, const char* text) {
    size_t len = 0;
    while (text[len] && len < 64) ++len;

    raw_w(amt_bank::OSD, 0x05, 0x01);                       // лише W0
    raw_w(amt_bank::OSD, 0x07, static_cast<uint8_t>(len));  // width (символів)
    raw_w(amt_bank::OSD, 0x08, 0x01);                       // height = 1
    raw_w(amt_bank::OSD, 0x76, 16);                         // char_xsiz = 16px
    raw_w(amt_bank::OSD, 0x77, 22);                         // char_ysiz = 22px
    raw_w(amt_bank::OSD, 0x09, 0x00);                       // xyloc msb
    raw_w(amt_bank::OSD, 0x0A, x);                          // xloc
    raw_w(amt_bank::OSD, 0x0B, y);                          // yloc
    raw_w(amt_bank::OSD, 0x0D, 0x00);                       // bgmap addr msb
    raw_w(amt_bank::OSD, 0x00, 0x00);                       // bgmap addr lsb
    for (size_t i = 0; i < len; ++i) {
        const char c = text[i];
        uint8_t code;
        if (c >= '0' && c <= '9')      code = static_cast<uint8_t>(c - '0' + 0x01);  // 0x01..0x0A
        else if (c >= 'A' && c <= 'Z') code = static_cast<uint8_t>(c - 'A' + 0x0B);  // 0x0B..0x24
        else                           code = 0x00;                                  // пробіл/інше
        raw_w(amt_bank::OSD, 0x0E, 0x00);   // char msb
        raw_w(amt_bank::OSD, 0x01, code);   // char lsb → VRAM, addr++
        raw_w(amt_bank::OSD, 0x10, 0x09);   // attr (FG=колір, BG=прозорий)
    }
}

void Amt630a::selftest_font_dump(uint16_t start_code, uint8_t cols, uint8_t rows) {
    raw_w(amt_bank::OSD, 0x05, 0x01);     // W0
    raw_w(amt_bank::OSD, 0x07, cols);     // width
    raw_w(amt_bank::OSD, 0x08, rows);     // height
    raw_w(amt_bank::OSD, 0x76, 16);       // char_xsiz
    raw_w(amt_bank::OSD, 0x77, 22);       // char_ysiz
    raw_w(amt_bank::OSD, 0x09, 0x00);     // xyloc msb
    raw_w(amt_bank::OSD, 0x0A, 10);       // xloc (10 = крайня ліва видима, design §3.4)
    raw_w(amt_bank::OSD, 0x0B, 12);       // yloc (12 = крайня верхня)
    raw_w(amt_bank::OSD, 0x0D, 0x00);     // bgmap addr msb
    raw_w(amt_bank::OSD, 0x00, 0x00);     // bgmap addr lsb
    const uint16_t total = static_cast<uint16_t>(cols) * rows;
    uint16_t addr = 0;
    for (uint16_t i = 0; i < total; ++i) {
        const uint16_t code = static_cast<uint16_t>(start_code + i);
        raw_w(amt_bank::OSD, 0x0E, static_cast<uint8_t>((code >> 8) & 0x03));  // char msb
        raw_w(amt_bank::OSD, 0x01, static_cast<uint8_t>(code & 0xFF));         // char lsb, addr++
        raw_w(amt_bank::OSD, 0x10, 0x76);                                      // FG=color6(біл), BG=7(чорний)
        ++addr;
        if ((addr & 0xFF) == 0) {  // ручний перенос lsb→msb
            raw_w(amt_bank::OSD, 0x0D, static_cast<uint8_t>((addr >> 8) & 0x01));
            raw_w(amt_bank::OSD, 0x00, 0x00);
        }
    }
}

void Amt630a::selftest_one_glyph() {
    // Рукотворні гліфи 16×22 (22 слова; msb-байт = ліві пікселі, bit7 = x0).
    uint16_t box[22], solid[22];
    for (int r = 0; r < 22; ++r) {
        box[r]   = (r == 0 || r == 21) ? 0xFFFF : 0x8001;  // контур: верх/низ + крайні стовпці
        solid[r] = 0xFFFF;                                  // суцільна заливка
    }

    raw_w(amt_bank::OSD, 0x76, 16);    // char_xsiz
    raw_w(amt_bank::OSD, 0x77, 22);    // char_ysiz → INNER.LEN = 22 слова/гліф
    raw_w(amt_bank::OSD, 0x05, 0x00);  // усі вікна OFF на час запису FONT RAM
    vTaskDelay(pdMS_TO_TICKS(25));     // вимкнення діє з наступного кадру

    raw_w(amt_bank::OSD, 0x11, 0xC1);  // bitmap_start (релятивний): 0x1C0..0x280 = 1bpp
    raw_w(amt_bank::OSD, 0x70, 0x00);

    auto upload = [&](uint16_t tile, const uint16_t* g) {
        const uint16_t base = static_cast<uint16_t>(22 * tile);  // адреса слова = INNER.LEN*tile
        for (uint16_t row = 0; row < 22; ++row) {
            const uint16_t a = static_cast<uint16_t>(base + row);
            raw_w(amt_bank::OSD, 0x0F, static_cast<uint8_t>((a >> 8) & 0x0F));     // font addr msb
            raw_w(amt_bank::OSD, 0x02, static_cast<uint8_t>(a & 0xFF));            // font addr lsb
            raw_w(amt_bank::OSD, 0x04, static_cast<uint8_t>((g[row] >> 8) & 0xFF));// data msb ПЕРШИМ
            raw_w(amt_bank::OSD, 0x03, static_cast<uint8_t>(g[row] & 0xFF));       // data lsb ЛАТЧ
        }
    };
    upload(0, box);     // RAM tile 0 → BGMAP код 0x1C0
    upload(1, solid);   // RAM tile 1 → BGMAP код 0x1C1

    const uint8_t W = 12, H = 6;       // велика область, добре видно на фоні снігу
    raw_w(amt_bank::OSD, 0x07, W);     // W0 width
    raw_w(amt_bank::OSD, 0x08, H);     // height
    raw_w(amt_bank::OSD, 0x09, 0x00);
    raw_w(amt_bank::OSD, 0x0A, 10);    // xloc
    raw_w(amt_bank::OSD, 0x0B, 12);    // yloc
    raw_w(amt_bank::OSD, 0x0D, 0x00);  // bgmap addr msb
    raw_w(amt_bank::OSD, 0x00, 0x00);  // bgmap addr lsb
    raw_w(amt_bank::OSD, 0x05, 0x01);  // увімкнути W0 назад

    const uint16_t total = static_cast<uint16_t>(W) * H;   // 72 < 256 → без переносу addr
    for (uint16_t i = 0; i < total; ++i) {
        const uint16_t row  = i / W;
        const uint16_t code = (row < H / 2) ? 0x1C0 : 0x1C1;  // верх=рамка, низ=блок
        raw_w(amt_bank::OSD, 0x0E, static_cast<uint8_t>((code >> 8) & 0x03));  // char msb (=0x01)
        raw_w(amt_bank::OSD, 0x01, static_cast<uint8_t>(code & 0xFF));         // char lsb (0xC0/0xC1)
        raw_w(amt_bank::OSD, 0x10, 0x76);                                      // FG=біл, BG=чорн
    }
    ESP_LOGI(TAG, "selftest_one_glyph: box@0x1C0 + solid@0x1C1, %dx%d fill, FB11=0xC1", W, H);
}

void Amt630a::upload_font(const uint16_t* glyphs, uint16_t first_tile, uint8_t count,
                          uint8_t xsiz, uint8_t ysiz, uint8_t restore_mask) {
    set_char_size(xsiz, ysiz);                 // FB76/FB77
    window_enable(0x00);                        // вікна OFF на час запису (proven-порядок)
    vTaskDelay(pdMS_TO_TICKS(25));              // діє з наступного кадру
    // bitmap_start (FB11/FB70) — РЕЛЯТИВНИЙ до 0x1C0 (bench-proven): 1bpp-зона =
    // [0x1C0, 0x1C0+bitmap_start). first_tile+count покриває всі гліфи → 1bpp.
    // (Раніше хибно ставили абсолютний 0x281 → кольорові квадрати.) Ставимо ПІСЛЯ вікон-OFF.
    const uint16_t bitmap_start = static_cast<uint16_t>(first_tile + count);
    amt_w(amt_bank::OSD, 0x11, static_cast<uint8_t>(bitmap_start & 0xFF));         // lsb
    amt_w(amt_bank::OSD, 0x70, static_cast<uint8_t>((bitmap_start >> 8) & 0xFF));  // msb
    const uint16_t W = ysiz;                   // слів/гліф (xsiz<=16 → 1 слово/ряд)
    for (uint8_t c = 0; c < count; ++c) {
        const uint16_t addr = static_cast<uint16_t>(W * (first_tile + c));
        for (uint16_t row = 0; row < W; ++row) {
            const uint16_t a = static_cast<uint16_t>(addr + row);
            amt_w(amt_bank::OSD, 0x0F, static_cast<uint8_t>((a >> 8) & 0x0F));  // font addr msb
            amt_w(amt_bank::OSD, 0x02, static_cast<uint8_t>(a & 0xFF));         // font addr lsb
            const uint16_t w = glyphs[c * W + row];
            amt_w(amt_bank::OSD, 0x04, static_cast<uint8_t>((w >> 8) & 0xFF));  // data msb ПЕРШИМ
            amt_w(amt_bank::OSD, 0x03, static_cast<uint8_t>(w & 0xFF));         // data lsb латч
        }
    }
    window_enable(restore_mask);
}

// ── Параметри екрана ───────────────────────────────────────────

static uint8_t map_pct(uint8_t pct, uint8_t lo, uint8_t hi) {
    if (pct > 100) pct = 100;
    return static_cast<uint8_t>(lo + (hi - lo) * static_cast<int>(pct) / 100);
}

void Amt630a::set_backlight(uint8_t pct) {
    // PWM0 duty (design doc §11). ⚠ полярність — bench. total=0x1000.
    const uint16_t total = 0x1000;
    const uint16_t high  = static_cast<uint16_t>(total * static_cast<int>(pct > 100 ? 100 : pct) / 100);
    amt_w(amt_bank::GLOBAL, 0x42, 0x03);   // FD42: пін P35/P36 → PWM-режим (FIZIK initDisplay) — без цього duty не діє
    amt_w(amt_bank::GLOBAL, 0x20, static_cast<uint8_t>(total & 0xFF));
    amt_w(amt_bank::GLOBAL, 0x21, static_cast<uint8_t>(total >> 8));
    amt_w(amt_bank::GLOBAL, 0x28, static_cast<uint8_t>(high & 0xFF));
    amt_w(amt_bank::GLOBAL, 0x29, static_cast<uint8_t>(high >> 8));
    amt_w(amt_bank::GLOBAL, 0x1F, 0x03);   // PWM enable (FIZIK: 0x03)
}

void Amt630a::set_video_brightness(uint8_t pct) { amt_w(amt_bank::VIDEO, 0xD4, map_pct(pct, 0x66, 0xB6)); }
void Amt630a::set_contrast(uint8_t pct)         { amt_w(amt_bank::VIDEO, 0xD3, map_pct(pct, 0x56, 0xA6)); }
void Amt630a::set_saturation(uint8_t pct)       { amt_w(amt_bank::VIDEO, 0xD6, map_pct(pct, 0x10, 0x60)); }

bool Amt630a::select_input(uint8_t n) {
    // Живий RMW мукса CVBS (ENGELS apply_av_input_r7 @9CD6h — findings 02-video-input-select.md §2,§3,§6).
    //   AV1=CVBS1 → FED8 bits6,7=2 (bit7=1,bit6=0), FEDC bits4,5=0              ; lock=FE2A bit4
    //   AV3=CVBS3 → FED8 bits6,7=0,                  FEDC bits4,5=2 (bit5=1,bit4=0); lock=FE2A bit6
    //   AV2       → FED8 bits6,7=2,                  FEDC bits4,5=3              — junk (ghost CVBS3)
    // Усе RMW (НЕ абсолютні записи): FED7 bits0,1,6,7=snow/freeze, FEDC bit6=backdrop → зберігаємо.
    // Ревізія: NEW-прошивка міняє місцями AV1↔AV3 (control reference §1b) → ефективний індекс.
    if (n > 2) {
        ESP_LOGW(TAG, "select_input(%u): немає такого входу (0=AV1, 1=AV3, 2=AV2-junk)", n);
        return false;
    }
    sh_input_ = static_cast<AvInput>(n);   // софт-тінь ЛИШЕ запиту (findings §5: hw без mux-readout)

    // Для NEW-прошивки логічні AV1(0)↔AV3(1) переставлені (мукс + lock-біт слідують за фізикою).
    const uint8_t eff = (av_rev_ == AvFwRev::NEW && n < 2) ? static_cast<uint8_t>(1 - n) : n;

    struct Mux { bool b7, b6, b5, b4; uint8_t lock; const char* name; };
    Mux m;
    switch (eff) {
    case 0:  m = {true,  false, false, false, 0x10, "AV1/CVBS1"}; break;  // FED8=2, FEDC=0, FE2A bit4
    case 1:  m = {false, false, true,  false, 0x40, "AV3/CVBS3"}; break;  // FED8=0, FEDC=2, FE2A bit6
    default: m = {true,  false, true,  true,  0x40, "AV2/junk"};  break;  // FED8=2, FEDC=3, ghost CVBS3
    }

    // одно-бітний RMW у банку AV (читаємо живе значення, чіпаємо рівно один біт)
    auto rmw_bit = [&](uint8_t reg, uint8_t mask, bool set) -> bool {
        uint8_t t = 0;
        if (!amt_r(amt_bank::AV, reg, t)) return false;
        const uint8_t nv = set ? static_cast<uint8_t>(t | mask)
                               : static_cast<uint8_t>(t & ~mask);
        return amt_w(amt_bank::AV, reg, nv);
    };

    uint8_t t = 0;
    bool okw = true;
    // 1. (опц.) video OFF: FED7 &= ~0x18. Брекет форсує релок, АЛЕ на цій платі глушив відео
    //    безповоротно → дефолт ВИМКНЕНО (міняємо лише мукс, відео лишається ON). Див. set_av_video_bracket().
    if (av_bracket_)
        okw = amt_r(amt_bank::AV, 0xD7, t) && amt_w(amt_bank::AV, 0xD7, static_cast<uint8_t>(t & ~0x18));
    // 2. FED8 bits6,7 — два RMW-кроки: bit7, потім bit6 (точно як OEM)
    okw = okw && rmw_bit(0xD8, 0x80, m.b7);
    okw = okw && rmw_bit(0xD8, 0x40, m.b6);
    // 3. ГАРАНТОВАНО video ON: FED7 |= 0x18 (НІКОЛИ не лишаємо відео вимкненим — навіть без брекету)
    okw = okw && amt_r(amt_bank::AV, 0xD7, t) && amt_w(amt_bank::AV, 0xD7, static_cast<uint8_t>(t | 0x18));
    // 4. FEDC bits4,5 — два RMW-кроки: bit5, потім bit4
    okw = okw && rmw_bit(0xDC, 0x20, m.b5);
    okw = okw && rmw_bit(0xDC, 0x10, m.b4);
    if (!okw) {
        ESP_LOGW(TAG, "select_input(%u=%s): I²C RMW помилка", n, m.name);
        return false;
    }

    // 5. settle + re-detect (findings §4): ~10 мс до першого читання, до ~200 мс на лок FE2A.
    bool locked = false;
    for (int elapsed = 0; elapsed < 200; elapsed += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
        uint8_t s = 0;
        if (amt_r(amt_bank::AV, 0x2A, s) && (s & m.lock)) { locked = true; break; }
    }
    ESP_LOGI(TAG, "select_input(%u=%s, fw=%s, bracket=%d) -> lock(FE2A&0x%02X)=%d",
             n, m.name, av_rev_ == AvFwRev::OLD ? "OLD" : "NEW", av_bracket_, m.lock, locked);
    return locked;
}

void Amt630a::set_backdrop(Backdrop mode) {
    // No-signal backdrop (control reference §1a; OEM apply_backdrop CLEAN:3044, ENGELS:5642-5686).
    // Малює банк VIDEO (0x5A). FED5=0xB1 глушить АВТОномний апаратний сірий сніг чіпа, щоб ним
    // керував FFB0 (без цього чіп сам малює сніг при втраті sync, ігноруючи FFB0).
    amt_w(amt_bank::AV, 0xD5, 0xB1);

    // Колір фону YCbCr (FFCE/CF/D0): BLUE=13,DD,72 ; BLACK/SNOW=00,80,80.
    const bool blue = (mode == Backdrop::BLUE);
    amt_w(amt_bank::VIDEO, 0xCE, blue ? 0x13 : 0x00);   // Y
    amt_w(amt_bank::VIDEO, 0xCF, blue ? 0xDD : 0x80);   // Cb
    amt_w(amt_bank::VIDEO, 0xD0, blue ? 0x72 : 0x80);   // Cr

    // FFB0: bit7=сніг on/off, bit5 завжди 1, інші біти зберегти (RMW). FFDA=рівень снігу (0x6C).
    uint8_t b0 = 0x22;                       // дефолт ініціалізації, якщо read не вдався
    amt_r(amt_bank::VIDEO, 0xB0, b0);
    if (mode == Backdrop::SNOW) {
        amt_w(amt_bank::VIDEO, 0xDA, 0x6C);                                      // рівень снігу
        amt_w(amt_bank::VIDEO, 0xB0, static_cast<uint8_t>(b0 | 0xA0));           // сніг ON (bit7,5)
    } else {
        amt_w(amt_bank::VIDEO, 0xB0, static_cast<uint8_t>((b0 & 0x7F) | 0x20));  // сніг off, bit5=1
    }

    // FFD2: BLACK → 0x54 форсований чорний blank (лише-OSD, сумісно з поточним UX «меню на чорному»);
    // SNOW/BLUE → 0x4F show-path (живе відео+OSD; фон видно лише коли НЕМАЄ сигналу).
    const uint8_t ffd2 = (mode == Backdrop::BLACK) ? 0x54 : 0x4F;
    amt_w(amt_bank::VIDEO, 0xD2, ffd2);
    ESP_LOGI(TAG, "set_backdrop(%s): FFB0 snow=%d color=%s FFD2=0x%02X",
             mode == Backdrop::SNOW ? "SNOW" : (blue ? "BLUE" : "BLACK"),
             mode == Backdrop::SNOW ? 1 : 0, blue ? "BLUE" : "BLACK", ffd2);
}

void Amt630a::display_on(bool on) {
    // FFD2 (forced_blank_color): 0x4F = показ AV-відео+OSD, 0x54 = чорний blank (лише OSD/меню).
    amt_w(amt_bank::VIDEO, 0xD2, on ? 0x4F : 0x54);
    ESP_LOGI(TAG, "display_on(%d) -> FFD2=0x%02X", on ? 1 : 0, on ? 0x4F : 0x54);
}

bool Amt630a::have_signal() {
    uint8_t v = 0;
    if (!amt_r(amt_bank::AV, 0x26, v)) return false;
    return (v & 0x02) != 0;   // FE26 bit1
}

bool Amt630a::input_has_signal(uint8_t n) {
    uint8_t v = 0;
    if (!amt_r(amt_bank::AV, 0x2A, v)) return false;
    // n=0→AV1→bit4 (CVBS1); n=1→AV3→bit6 (CVBS3). Під NEW-прошивку фізика переставлена.
    const uint8_t eff = (av_rev_ == AvFwRev::NEW && n < 2) ? static_cast<uint8_t>(1 - n) : n;
    const uint8_t bit = (eff == 0) ? 0x10 : 0x40;
    return (v & bit) != 0;
}

uint8_t Amt630a::probe_inputs() {
    // BENCH-визначення розводки: підключи камеру до ОДНОГО входу й виклич це. У логу вхід із lock=1
    // — це вхід камери. Якщо жоден не залочився — спробуй set_av_fw_revision(AvFwRev::NEW) і/або
    // set_av_video_bracket(true), тоді повтори. FE2A показує, який фізичний CVBS-пін має сигнал.
    uint8_t mask = 0;
    for (uint8_t n = 0; n < 2; ++n) {
        const bool lock = select_input(n);
        uint8_t fe26 = 0, fe2a = 0;
        amt_r(amt_bank::AV, 0x26, fe26);
        amt_r(amt_bank::AV, 0x2A, fe2a);
        ESP_LOGI(TAG, "probe input %u: lock=%d FE26=0x%02X(bit1=%d) FE2A=0x%02X (CVBS1=%d CVBS3=%d)",
                 n, lock ? 1 : 0, fe26, (fe26 & 0x02) != 0 ? 1 : 0,
                 fe2a, (fe2a & 0x10) != 0 ? 1 : 0, (fe2a & 0x40) != 0 ? 1 : 0);
        if (lock) mask |= static_cast<uint8_t>(1u << n);
    }
    ESP_LOGI(TAG, "probe_inputs: locked mask=0x%02X (fw=%s)",
             mask, av_rev_ == AvFwRev::OLD ? "OLD" : "NEW");
    return mask;
}

} // namespace modesp::osd
