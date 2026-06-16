/**
 * @file amt630a_port.h
 * @brief IDisplayPort на AMT630A (I²C OSD/TFT) — ADR-002 / AMT630A_driver_design.md.
 *
 * Адаптер: семантичний View → CharGridLayout(cols×rows) → BGMAP-тайли по I²C.
 * Колір — через атрибут+палітру; параметри екрана (підсвітка/контраст/вхід) —
 * capability-методи. Роль SELECTED → колір-хайлайт.
 *
 * ⚠ Шрифт: наразі ROM (цифри/латиниця). Кирилиця через FONT RAM (1C0h+) —
 *   потребує генератора (gen_osd_font.py --target amt630a) + bench-валідації
 *   bit-order (design doc §4). Компілюється лише при CONFIG_MODESP_DISPLAY_AMT630A.
 */

#pragma once

#include "display/display_port.h"
#include "display/char_grid.h"
#include "modesp/osd/amt630a.h"

namespace modesp::display {

class Amt630aPort : public IDisplayPort, public IVideoInputs {
public:
    /// Шину + швидкість надає HAL (board.json i2c_buses); cols/rows — board.json
    /// i2c_displays. Жодних Kconfig-пінів — конфіг приходить через board+bindings.
    Amt630aPort(i2c_master_bus_handle_t bus, uint32_t freq_hz, uint8_t cols, uint8_t rows);

    bool init() override;
    DisplayCaps caps() const override;

    void present_main(const MainView& view) override;
    void present_menu(const MenuView& view) override;
    void present_edit(const EditView& view) override;
    void present_notice(const Notice& notice) override;
    void clear_notice() override;

    void set_backlight(uint8_t pct) override;
    void set_contrast(uint8_t pct) override;
    void set_brightness(uint8_t pct) override;
    void set_saturation(uint8_t pct) override;
    void set_backdrop(uint8_t mode) override;

    IVideoInputs* as_video_inputs() override { return this; }

    // IVideoInputs
    void    select_input(uint8_t n) override;
    uint8_t input_count() const override;

private:
    void render(const CharGrid& g);

    osd::Amt630a dev_;
    uint8_t      cols_;
    uint8_t      rows_;
};

} // namespace modesp::display
