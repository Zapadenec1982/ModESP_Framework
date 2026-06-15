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
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port                     = -1;   // авто-вибір вільного порту
    bus_cfg.sda_io_num                   = static_cast<gpio_num_t>(pins_.sda);
    bus_cfg.scl_io_num                   = static_cast<gpio_num_t>(pins_.scl);
    bus_cfg.clk_source                   = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt            = 7;
    bus_cfg.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus_cfg, &bus_) != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed");
        return false;
    }

    auto add = [&](uint8_t addr, i2c_master_dev_handle_t& h) {
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address  = addr;
        dev_cfg.scl_speed_hz    = pins_.freq_hz ? pins_.freq_hz : 100000;
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
    amt_w(amt_bank::OSD, 0x10, attr);                                      // колір/фон
    for (size_t i = 0; i < n; ++i) {
        amt_w(amt_bank::OSD, 0x0E, static_cast<uint8_t>((tiles[i] >> 8) & 0x03));  // data msb (біти 8-9)
        amt_w(amt_bank::OSD, 0x01, static_cast<uint8_t>(tiles[i] & 0xFF));         // data lsb → VRAM, addr++
        ++addr;
        // amt-2: перенос lsb→msb апаратно НЕ робиться — після обертання lsb перевиставити addr.
        if ((addr & 0xFF) == 0x00) {
            amt_w(amt_bank::OSD, 0x0D, static_cast<uint8_t>((addr >> 8) & 0x01));
            amt_w(amt_bank::OSD, 0x00, 0x00);
        }
    }
}

void Amt630a::upload_font(const uint16_t* glyphs, uint16_t first_tile, uint8_t count,
                          uint8_t xsiz, uint8_t ysiz, uint8_t restore_mask) {
    set_char_size(xsiz, ysiz);
    window_enable(0x00);                       // вимкнути вікна (інакше glitch)
    vTaskDelay(pdMS_TO_TICKS(25));             // діє з наступного кадру
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
    amt_w(amt_bank::GLOBAL, 0x20, static_cast<uint8_t>(total & 0xFF));
    amt_w(amt_bank::GLOBAL, 0x21, static_cast<uint8_t>(total >> 8));
    amt_w(amt_bank::GLOBAL, 0x28, static_cast<uint8_t>(high & 0xFF));
    amt_w(amt_bank::GLOBAL, 0x29, static_cast<uint8_t>(high >> 8));
    amt_w(amt_bank::GLOBAL, 0x1F, 0x01);   // PWM0 enable
}

void Amt630a::set_video_brightness(uint8_t pct) { amt_w(amt_bank::VIDEO, 0xD4, map_pct(pct, 0x66, 0xB6)); }
void Amt630a::set_contrast(uint8_t pct)         { amt_w(amt_bank::VIDEO, 0xD3, map_pct(pct, 0x56, 0xA6)); }
void Amt630a::set_saturation(uint8_t pct)       { amt_w(amt_bank::VIDEO, 0xD6, map_pct(pct, 0x10, 0x60)); }

void Amt630a::select_input(uint8_t n) {
    // Послідовність FED7→FED8→FED7→FEDC (design doc §3). ⚠ bit-значення — bench.
    const bool av3 = (n != 0);
    amt_w(amt_bank::AV, 0xD7, 0xFC & ~0x18);                 // скинути bit3,4
    sh_fed8_ = static_cast<uint8_t>((sh_fed8_ & 0x3F) | (av3 ? (0 << 6) : (2 << 6)));
    amt_w(amt_bank::AV, 0xD8, sh_fed8_);
    amt_w(amt_bank::AV, 0xD7, 0xFC);                         // bit3,4 = 3
    amt_w(amt_bank::AV, 0xDC, static_cast<uint8_t>(av3 ? 0x20 : 0x00));
}

bool Amt630a::have_signal() {
    uint8_t v = 0;
    if (!amt_r(amt_bank::AV, 0x26, v)) return false;
    return (v & 0x02) != 0;   // FE26 bit1
}

} // namespace modesp::osd
