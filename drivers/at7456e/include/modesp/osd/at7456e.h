/**
 * @file at7456e.h
 * @brief AT7456E (MAX7456-сумісний OSD) — переносний bit-bang драйвер.
 *
 * Накладає символьну сітку (PAL 16×30 / NTSC 13×30) на composite-відео.
 * Spільний для ModESP_v4_Framework і ModESP_Sacaner: піни задаються через
 * конфіг, протокол — канонічний MAX7456 bit-bang (mode0, MSB-first, семпл
 * на rising CLK), перевірений на залізі KOZHAN.
 *
 * Шрифт character-NVM ми заливаємо власний (osd_charmap.h визначає
 * розкладку гліфів). `write_glyphs()` приймає СИРІ індекси гліфів —
 * UTF-8→index робить виклик (рендерер) через osd_charmap.
 *
 * ⚠ На спільній шині (як у сканера, DATA/CLK з тюнерами) серіалізацію
 *   викликів забезпечує сторона виклику. Драйвер шину не мютексить.
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace modesp::osd {

/// GPIO-піни bit-bang шини (номери ESP32 GPIO).
struct At7456ePins {
    int cs;
    int data;   // MOSI
    int clk;
    int miso;
};

/// Геометрія / відеостандарт.
enum class VideoStd : uint8_t { PAL, NTSC };

class At7456e {
public:
    explicit At7456e(const At7456ePins& pins, VideoStd std = VideoStd::PAL)
        : pins_(pins), std_(std) {}

    /// Сконфігурувати GPIO, soft-reset, дефолти. OSD лишається вимкненим.
    void init();

    /// true якщо чіп відповідає по SPI (readback DMAL).
    bool present();

    uint8_t read_stat() const;   ///< STAT: bit0=PAL bit1=NTSC bit2=LOS bit5=NVM-busy
    uint8_t read_vm0()  const;
    void    write_vm0(uint8_t v);

    /// synchSelect: 0=auto, 1=internal (власний sync), 2=external.
    void set_sync(uint8_t mode);

    void enable_osd(bool on);            ///< VM0[3]
    void clear();                        ///< очистити дисплейну пам'ять
    void offset(uint8_t h, uint8_t v);   ///< зсув видимої зони (overscan)

    uint8_t rows() const { return std_ == VideoStd::PAL ? 16 : 13; }
    uint8_t cols() const { return 30; }

    /// Вивести сирі індекси гліфів у рядок (autoincrement DM-стрім).
    void write_glyphs(uint8_t col, uint8_t row, const uint8_t* idx, size_t n);

    // ── Character-NVM (заливка власного шрифту) ──

    static constexpr size_t CHAR_BYTES = 54;   ///< 12×18 px, 2 bpp
    static constexpr size_t CHAR_COUNT = 256;

    /// Записати один гліф у NVM (addr 0..255, data[54]). OSD має бути off.
    /// Блокує до зняття STAT[5] (~12 мс). true якщо busy знявся вчасно.
    bool upload_char(uint8_t addr, const uint8_t data[CHAR_BYTES]);

    /// Прочитати один гліф із NVM у out[54].
    bool read_char(uint8_t addr, uint8_t out[CHAR_BYTES]) const;

    /**
     * Залити повний шрифт (масив char_count×54). Вимикає OSD на час запису,
     * відновлює попередній стан. Повертає к-сть успішно записаних гліфів.
     * @param sentinel_addr,sentinel_byte0  якщо перший байт цього гліфа вже
     *   збігається — шрифт вважається залитим, повертає char_count без запису.
     */
    size_t upload_font(const uint8_t* data, size_t char_count,
                       int sentinel_addr = -1, uint8_t sentinel_byte0 = 0);

private:
    void     pin_set(int pin, int level) const;
    int      pin_get(int pin) const;
    void     shift_out(uint8_t b) const;
    uint8_t  shift_in() const;
    void     wr(uint8_t addr, uint8_t data) const;
    uint8_t  rd(uint8_t addr) const;
    void     dm_stream(uint16_t pos, const uint8_t* data, size_t n) const;

    At7456ePins pins_;
    VideoStd    std_;
};

} // namespace modesp::osd
