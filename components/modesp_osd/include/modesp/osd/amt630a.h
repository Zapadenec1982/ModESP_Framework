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
    /// Чіп НЕ володіє шиною — bus_handle надає HAL (board.json i2c_buses).
    /// Драйвер лише додає свої device-handle-и банків (0x58–0x5F) на цю шину.
    Amt630a(i2c_master_bus_handle_t bus, uint32_t freq_hz)
        : bus_(bus), freq_hz_(freq_hz ? freq_hz : 100000) {}

    /// Додати device-handle-и банків на надану шину; probe present.
    bool init();
    /// ACK-probe на OSD-банку (0x5B).
    bool present();

    /// Безпечний runtime-запис (guard блокує DANGER-регістри). false якщо заблоковано/помилка I²C.
    bool amt_w(uint8_t dev7, uint8_t reg, uint8_t val);
    bool amt_r(uint8_t dev7, uint8_t reg, uint8_t& out);

    /// Доведена init-послідовність Fizik (initDisplay+onDisplay). Поза guard.
    void apply_init_table();
    /// МІНІМАЛЬНИЙ OSD-init (Шлях A): vendor-unlock (банк 0x5F) + увімкнення OSD-вікон (FB05).
    /// БЕЗ відеобанків (0x58/0x59/0x5A) — щоб не зруйнувати робоче OEM-відео.
    void apply_osd_init();

    // ── OSD (dev 0x5B) ──
    void set_char_size(uint8_t xsiz, uint8_t ysiz);          // FB76/FB77 (16,22)
    void window0_setup(uint8_t cols, uint8_t rows, uint8_t x, uint8_t y);
    /// Налаштувати будь-яке вікно W0–W4: cols/rows (символи), x/y (ПІКСЕЛІ), vramaddr —
    /// ділянка BGMAP, з якої вікно читає (для W0 фіксовано 000h, ігнорується). Для різних
    /// розмірів одночасно: кожне вікно — свій scale + своя ділянка BGMAP.
    void window_setup(uint8_t win, uint8_t cols, uint8_t rows, uint8_t x, uint8_t y, uint16_t vramaddr);
    /// Апаратний масштаб вікна ×1..×4 (FB32=W0; FB33=W1/W2; FB34=W3/W4). OSD-банк, безпечно.
    void set_window_scale(uint8_t win, uint8_t scale_x, uint8_t scale_y);
    void window_enable(uint8_t mask);                        // FB05
    void set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);  // idx 1..6, RGB444 0..10
    /// Вивести n тайлів (10-біт індекси) у BGMAP з addr, з атрибутом кольору.
    void osd_print(uint16_t bgmap_addr, const uint16_t* tiles, size_t n, uint8_t attr);
    /// BRING-UP self-test (FIZIK-faithful): друкує ASCII (' ', 0-9, A-Z) вбудованим
    /// low-code шрифтом чіпа (0x00..0x24), char 16x22, W0. Обходить RAM-шрифт 0x1C0+.
    void selftest_print(uint8_t x, uint8_t y, const char* text);
    /// BRING-UP: дамп вбудованого шрифту — сітка cols×rows, клітинка(r,c)=код start+r*cols+c.
    /// Дозволяє зчитати з фото, які гліфи має ROM чіпа і на яких кодах (для мапи кирилиці).
    void selftest_font_dump(uint16_t start_code, uint8_t cols, uint8_t rows);
    /// BRING-UP: заливає 2 рукотворні гліфи у FONT RAM (tile0=рамка, tile1=суцільний блок)
    /// за ENGELS-протоколом, 1bpp, і друкує 8 клітинок (коди 0x1C0/0x1C1 по черзі).
    /// Перевіряє: запис FONT RAM + мапу код↔RAM-tile + режим 1bpp за один прохід.
    void selftest_one_glyph();
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
    /// Логічні CVBS-входи. Лише AV1 і AV3 фізично розведені й дають чисте відео
    /// (proven: ENGELS apply_av_input_r7 @9CD6h — findings 02-video-input-select.md §2).
    /// AV2 — вироджена OEM-гілка (FED8=2,FEDC=3): лише «привид» CVBS3, НЕ чистий вхід.
    enum class AvInput : uint8_t {
        AV1_CVBS1 = 0,   ///< FED8 bits6,7=2; FEDC bits4,5=0; lock=FE2A bit4 (HaveCVBS1)
        AV3_CVBS3 = 1,   ///< FED8 bits6,7=0; FEDC bits4,5=2; lock=FE2A bit6 (HaveCVBS3)
        AV2_JUNK  = 2,   ///< ⚠ OEM-сміття (FED8=2,FEDC=3): ghost CVBS3 — не використовувати
    };
    /// Перемкнути CVBS-вхід живим RMW (dev 0x59). n: 0=AV1(CVBS1), 1=AV3(CVBS3), 2=AV2(junk).
    /// За замовчуванням БЕЗ FED7 off→on брекету (відео лишається ON — безпечно); порядок
    /// FED8→FED7(ensure-on)→FEDC. Повертає lock-статус (re-detect FE2A). Ревізія-залежний (§1b).
    bool select_input(uint8_t n);
    /// Останній запитаний вхід (софт-тінь; апаратного mux-readout немає — findings §5).
    AvInput selected_input() const { return sh_input_; }

    // ── Ревізія прошивки + безпека перемикання (control reference §0, §1b) ──
    // OLD (<09.2017) і NEW (≥09.2017) міняють місцями мукс-значення AV1↔AV3. Невідому ревізію
    // визначити через probe_inputs(). Дефолт OLD (ENGELS-гілка з опкодами).
    enum class AvFwRev : uint8_t { OLD, NEW };
    void    set_av_fw_revision(AvFwRev r) { av_rev_ = r; }
    AvFwRev av_fw_revision() const { return av_rev_; }
    // FED7 video-off→on брекет форсує релок декодера, АЛЕ на цій платі глушив відео безповоротно.
    // Дефолт ВИМКНЕНО: міняємо лише мукс-біти (відео лишається ON). Увімкнути, якщо вхід не перемикається.
    void    set_av_video_bracket(bool on) { av_bracket_ = on; }
    bool    av_video_bracket() const { return av_bracket_; }
    // BENCH: чи залочений сигнал на фізичному вході n (FE2A: AV1→bit4 CVBS1, AV3→bit6 CVBS3).
    bool    input_has_signal(uint8_t n);
    // BENCH-діагностика розводки: перебрати входи 0 і 1, повернути бітмаску залочених (bit0=вх0, bit1=вх1).
    // Лог на кожен вхід: FE26(lock)+FE2A(CVBS1/CVBS3) → зіставити роз'єм ↔ логічний вхід ↔ ревізію.
    uint8_t probe_inputs();

    // Фон за ВІДСУТНОСТІ CVBS-сигналу (control reference §1a): малює банк VIDEO (0x5A).
    // SNOW/BLUE: FFB0(сніг bit7)+FFCE/CF/D0(YCbCr)+FFDA(рівень), FFD2=0x4F — живе відео НЕ глушиться,
    // фон видно лише коли немає сигналу. BLACK: FFD2=0x54 — форсований чорний blank (лише-OSD),
    // сумісно з поточним UX. FED5=0xB1 глушить автономний апаратний сірий сніг чіпа.
    enum class Backdrop : uint8_t { SNOW, BLUE, BLACK };
    void set_backdrop(Backdrop mode);
    /// Явний show/hide живого відео (FFD2): on→0x4F (відео+OSD), off→0x54 (чорний, лише OSD).
    void display_on(bool on);

    bool have_signal();                     // dev 0x59 FE26 bit1

private:
    bool raw_w(uint8_t dev7, uint8_t reg, uint8_t val);   // без guard (init-таблиця)
    static bool is_danger(uint8_t dev7, uint8_t reg);
    i2c_master_dev_handle_t handle_for(uint8_t dev7) const;

    i2c_master_bus_handle_t bus_ = nullptr;   // власність HAL — НЕ видаляти
    uint32_t freq_hz_ = 100000;               // per-device scl_speed_hz
    i2c_master_dev_handle_t h58_ = nullptr, h59_ = nullptr, h5A_ = nullptr,
                            h5B_ = nullptr, h5C_ = nullptr, h5F_ = nullptr;
    AvInput  sh_input_ = AvInput::AV1_CVBS1;  // софт-тінь ЛИШЕ запитаного mux (findings §5: hw без mux-readout)
    uint8_t  sh_fb33_ = 0x00, sh_fb34_ = 0x00; // тіні per-window scale (FB33=W1/W2, FB34=W3/W4) — OEM-баг 12.6
    AvFwRev  av_rev_ = AvFwRev::OLD;          // ревізія прошивки для мукс-біт AV1↔AV3 (дефолт OLD/ENGELS)
    bool     av_bracket_ = false;             // FED7 off→on брекет при перемиканні (дефолт OFF — безпечно)
    bool     ok_ = false;
};

} // namespace modesp::osd
