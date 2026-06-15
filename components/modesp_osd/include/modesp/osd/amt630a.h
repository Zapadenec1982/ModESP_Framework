/**
 * @file amt630a.h
 * @brief AMT630A — переносний драйвер OSD/TFT video-SoC по I²C (ESP-IDF i2c_master).
 *
 * Керування ЗЗОВНІ поверх заводської прошивки (AMT630A_driver_design.md, Шлях A):
 * XRAM-порти FBxx–FFxx доступні через I²C-банки 0x58–0x5C + vendor-init 0x5F.
 * Транзакція = 2 байти payload [reg, val] (3 на шині, з адресою банку).
 *
 * Двотерміновий guard: apply_init_table() (точні Fizik-значення, вкл. DANGER
 * FD11/FD12) — БЕЗ guard; runtime amt_w() блокує DANGER-регістри (SPI-flash тощо).
 *
 * ⚠ Багато значень — bench-pending (полярність PWM, bit-и CVBS-входів, bit-order
 *   FONT-data). Див. AMT630A_driver_design.md §9 + ADR-002.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include "driver/i2c_master.h"

namespace modesp::osd {

struct Amt630aPins {
    int      sda;
    int      scl;
    uint32_t freq_hz;
};

// I²C 7-біт банки (Rosetta-карта, design doc §2.1)
namespace amt_bank {
    constexpr uint8_t GLOBAL = 0x58;  // FDxx: PWM-підсвітка / PLL
    constexpr uint8_t AV     = 0x59;  // FExx: вибір CVBS + детект
    constexpr uint8_t VIDEO  = 0x5A;  // FFxx: brightness/contrast/saturation/gamma
    constexpr uint8_t OSD    = 0x5B;  // FBxx: OSD-рушій (BGMAP/FONT/палітра)
    constexpr uint8_t LCD    = 0x5C;  // FCxx: Tcon (НЕ чіпати)
    constexpr uint8_t INIT   = 0x5F;  // vendor init/unlock
}

class Amt630a {
public:
    explicit Amt630a(const Amt630aPins& pins) : pins_(pins) {}

    /// Створити I²C-шину + device-handle-и банків; probe present.
    bool init();
    /// ACK-probe на OSD-банку (0x5B).
    bool present();

    /// Безпечний runtime-запис (guard блокує DANGER-регістри). false якщо заблоковано/помилка I²C.
    bool amt_w(uint8_t dev7, uint8_t reg, uint8_t val);
    bool amt_r(uint8_t dev7, uint8_t reg, uint8_t& out);

    /// Доведена init-послідовність Fizik (initDisplay+onDisplay). Поза guard.
    void apply_init_table();

    // ── OSD (dev 0x5B) ──
    void set_char_size(uint8_t xsiz, uint8_t ysiz);          // FB76/FB77 (16,22)
    void window0_setup(uint8_t cols, uint8_t rows, uint8_t x, uint8_t y);
    void window_enable(uint8_t mask);                        // FB05
    void set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);  // idx 1..6, RGB444 0..10
    /// Вивести n тайлів (10-біт індекси) у BGMAP з addr, з атрибутом кольору.
    void osd_print(uint16_t bgmap_addr, const uint16_t* tiles, size_t n, uint8_t attr);
    /// Залити власний шрифт у FONT RAM (1bpp, ysiz слів/гліф). Вимикає вікна на час.
    /// @param first_tile RAM word-tile index (0-based; це НЕ BGMAP-код 0x1C0+). amt-3.
    ///        Інваріант: ysiz*(first_tile+count) <= 0xFFF (FONT RAM = 0x1000 слів).
    void upload_font(const uint16_t* glyphs, uint16_t first_tile, uint8_t count,
                     uint8_t xsiz, uint8_t ysiz, uint8_t restore_mask);

    // ── Параметри екрана (0..100%) ──
    void set_backlight(uint8_t pct);        // dev 0x58 PWM0 duty
    void set_video_brightness(uint8_t pct); // dev 0x5A FFD4
    void set_contrast(uint8_t pct);         // dev 0x5A FFD3
    void set_saturation(uint8_t pct);       // dev 0x5A FFD6
    void select_input(uint8_t n);           // dev 0x59 FED7→D8→D7→DC

    bool have_signal();                     // dev 0x59 FE26 bit1

private:
    bool raw_w(uint8_t dev7, uint8_t reg, uint8_t val);   // без guard (init-таблиця)
    static bool is_danger(uint8_t dev7, uint8_t reg);
    i2c_master_dev_handle_t handle_for(uint8_t dev7) const;

    Amt630aPins pins_;
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t h58_ = nullptr, h59_ = nullptr, h5A_ = nullptr,
                            h5B_ = nullptr, h5C_ = nullptr, h5F_ = nullptr;
    uint8_t  sh_fed8_ = 0xA3;  // тінь FED8 (amt-6: засів OEM-init значенням → зберігає config-біти 0x23)
    bool     ok_ = false;
};

} // namespace modesp::osd
