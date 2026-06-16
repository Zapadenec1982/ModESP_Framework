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
    /// cal_x/cal_y — overscan-зсув OSD (per-panel, з board.json i2c_displays).
    Amt630aPort(i2c_master_bus_handle_t bus, uint32_t freq_hz, uint8_t cols, uint8_t rows,
                int8_t cal_x = 0, int8_t cal_y = 0);

    bool init() override;
    DisplayCaps caps() const override;
    void begin_reinit() override;                 // запустити неблокуюче відновлення OSD
    bool reinit_busy() const override;            // true поки відновлення триває
    bool reinit_tick(uint32_t dt_ms) override;    // крок відновлення (з on_update)

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
    // Конфігурація OSD поверх OEM-прошивки (unlock+вікна+палітра+шрифт+підсвітка) — спільне
    // для init() і reinit() (відновлення після холодного старту заліза).
    void configure_chip_();
    // Розмістити вікно зі зсувом калібровки (cal_x_/cal_y_), з клемпом у [0,255].
    void win0_(uint8_t cols, uint8_t rows, int x, int y);
    void win_(uint8_t win, uint8_t cols, uint8_t rows, int x, int y, uint16_t vram);

    osd::Amt630a dev_;
    uint8_t      cols_;
    uint8_t      rows_;
    int          cal_x_ = 0;   // overscan-калібровка: піксельний зсув усього OSD
    int          cal_y_ = 0;
    uint8_t      backlight_pct_ = 70;  // остання яскравість — відновити після cold boot

    // Неблокуюче відновлення після cold boot (chunked state-machine).
    enum class Recov : uint8_t { IDLE, WAIT, SETUP, FONT };
    Recov    recov_        = Recov::IDLE;
    uint32_t recov_wait_   = 0;        // лічильник затримки/таймауту
    uint16_t recov_cursor_ = 0;        // індекс наступного гліфа для заливки
};

} // namespace modesp::display
