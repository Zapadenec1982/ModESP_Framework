/**
 * @file at7456e.cpp
 * @brief AT7456E bit-bang драйвер — реалізація (ESP-IDF target).
 *
 * Протокол портовано зі сканера (cyclop++/MAX7456 datasheet), узагальнено
 * під конфігуровані піни + додано character-NVM upload.
 */

#include "modesp/osd/at7456e.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

namespace modesp::osd {

static const char* TAG = "at7456e";

// ── Регістри (MAX7456) ──
static constexpr uint8_t VM0   = 0x00;
static constexpr uint8_t HOS   = 0x02;
static constexpr uint8_t VOS   = 0x03;
static constexpr uint8_t DMM   = 0x04;
static constexpr uint8_t DMAH  = 0x05;
static constexpr uint8_t DMAL  = 0x06;
static constexpr uint8_t DMDI  = 0x07;
static constexpr uint8_t CMM   = 0x08;
static constexpr uint8_t CMAH  = 0x09;
static constexpr uint8_t CMAL  = 0x0A;
static constexpr uint8_t CMDI  = 0x0B;
static constexpr uint8_t OSDM  = 0x0C;
static constexpr uint8_t RB0   = 0x10;
static constexpr uint8_t OSDBL = 0x6C;
static constexpr uint8_t STAT  = 0xA0;   // read-only
static constexpr uint8_t CMDO  = 0xC0;   // read-only (char memory data out)

static constexpr uint8_t VM0_RESET   = 0x02;
static constexpr uint8_t VM0_OSD_EN  = 0x08;
static constexpr uint8_t STAT_NVM_BUSY = 0x20;  // bit5

static inline void d1() { esp_rom_delay_us(1); }

// ── GPIO ──────────────────────────────────────────────────────

void At7456e::pin_set(int pin, int level) const {
    gpio_set_level(static_cast<gpio_num_t>(pin), level);
}
int At7456e::pin_get(int pin) const {
    return gpio_get_level(static_cast<gpio_num_t>(pin));
}

void At7456e::shift_out(uint8_t b) const {
    for (int i = 7; i >= 0; --i) {
        pin_set(pins_.data, (b >> i) & 1);
        d1();
        pin_set(pins_.clk, 1);   // семпл на rising
        d1();
        pin_set(pins_.clk, 0);
    }
}

uint8_t At7456e::shift_in() const {
    uint8_t v = 0;
    for (int i = 7; i >= 0; --i) {
        pin_set(pins_.clk, 1);
        d1();
        v = static_cast<uint8_t>((v << 1) | (pin_get(pins_.miso) & 1));
        pin_set(pins_.clk, 0);
        d1();
    }
    return v;
}

void At7456e::wr(uint8_t addr, uint8_t data) const {
    pin_set(pins_.cs, 0); d1();
    shift_out(addr);          // write: D7=0
    shift_out(data);
    pin_set(pins_.cs, 1); d1();
}

uint8_t At7456e::rd(uint8_t addr) const {
    pin_set(pins_.cs, 0); d1();
    shift_out(addr | 0x80);   // read: D7=1 (ідемпотентно для 0xA0/0xC0)
    uint8_t v = shift_in();
    pin_set(pins_.cs, 1); d1();
    return v;
}

// ── Init / стан ───────────────────────────────────────────────

void At7456e::init() {
    gpio_config_t out = {};
    out.pin_bit_mask = (1ULL << pins_.cs) | (1ULL << pins_.data) | (1ULL << pins_.clk);
    out.mode = GPIO_MODE_OUTPUT;
    gpio_config(&out);
    gpio_config_t in = {};
    in.pin_bit_mask = (1ULL << pins_.miso);
    in.mode = GPIO_MODE_INPUT;
    gpio_config(&in);

    pin_set(pins_.cs, 1);
    pin_set(pins_.clk, 0);

    wr(VM0, VM0_RESET);                       // soft-reset
    vTaskDelay(pdMS_TO_TICKS(100));

    const uint8_t vstd = (std_ == VideoStd::PAL) ? 0x40 : 0x00;  // VM0[6]=PAL
    wr(VM0, vstd | 0x04);                     // vsync; sync=auto; OSD off
    wr(OSDM, 0x12);                           // sharpness
    for (uint8_t x = 0; x < 16; ++x) wr(RB0 + x, 0x00);  // white 120% / black 0%
    uint8_t bl = rd(OSDBL);
    wr(OSDBL, bl & ~0x10);                    // авто OSD black level
    offset(0x20, 0x10);                       // центр (overscan можна підкрутити)

    ESP_LOGI(TAG, "init CS=%d %s VM0=0x%02X STAT=0x%02X %s",
             pins_.cs, std_ == VideoStd::PAL ? "PAL" : "NTSC",
             rd(VM0), rd(STAT), present() ? "OK" : "NO-RESP");
}

bool At7456e::present() {
    wr(DMAL, 0x55); uint8_t a = rd(DMAL);
    wr(DMAL, 0xAA); uint8_t b = rd(DMAL);
    return a == 0x55 && b == 0xAA;
}

uint8_t At7456e::read_stat() const { return rd(STAT); }
uint8_t At7456e::read_vm0()  const { return rd(VM0); }
void    At7456e::write_vm0(uint8_t v) { wr(VM0, v); }

void At7456e::set_sync(uint8_t mode) {
    uint8_t vm0 = rd(VM0) & ~0x30;
    if (mode == 1)      vm0 |= 0x30;   // internal
    else if (mode == 2) vm0 |= 0x20;   // external
    wr(VM0, vm0);
}

void At7456e::enable_osd(bool on) {
    uint8_t vm0 = rd(VM0);
    if (on) vm0 |= VM0_OSD_EN; else vm0 &= ~VM0_OSD_EN;
    wr(VM0, vm0);
}

void At7456e::clear() {
    pin_set(pins_.cs, 0); d1();
    shift_out(DMM); shift_out(0x04);   // clearDisplayMemory (bit2)
    pin_set(pins_.cs, 1); d1();
    vTaskDelay(pdMS_TO_TICKS(1));
}

void At7456e::offset(uint8_t h, uint8_t v) {
    wr(HOS, h & 0x3F);
    wr(VOS, v & 0x1F);
}

// ── Дисплейний вивід (autoincrement, CS тримається LOW) ──────

void At7456e::dm_stream(uint16_t pos, const uint8_t* data, size_t n) const {
    pin_set(pins_.cs, 0); d1();
    shift_out(DMM);  shift_out(0x01);             // autoincrement enable
    shift_out(DMAH); shift_out((pos >> 8) & 0x01);
    shift_out(DMAL); shift_out(pos & 0xFF);
    for (size_t i = 0; i < n; ++i) { shift_out(DMDI); shift_out(data[i]); }
    shift_out(DMDI); shift_out(0xFF);             // escape autoincrement
    pin_set(pins_.cs, 1); d1();
}

void At7456e::write_glyphs(uint8_t col, uint8_t row, const uint8_t* idx, size_t n) {
    if (row >= rows()) return;
    const uint8_t max = static_cast<uint8_t>(cols() - col);
    if (n > max) n = max;
    if (n == 0) return;
    dm_stream(static_cast<uint16_t>(cols() * row + col), idx, n);
}

// ── Character-NVM ─────────────────────────────────────────────

bool At7456e::upload_char(uint8_t addr, const uint8_t data[CHAR_BYTES]) {
    wr(CMAH, addr);
    for (uint8_t i = 0; i < CHAR_BYTES; ++i) {
        wr(CMAL, i);
        wr(CMDI, data[i]);
    }
    wr(CMM, 0xA0);                       // write shadow → NVM
    // NVM-запис ~12 мс; чекаємо зняття busy (з запасом)
    for (int t = 0; t < 50; ++t) {
        if ((rd(STAT) & STAT_NVM_BUSY) == 0) return true;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return false;
}

bool At7456e::read_char(uint8_t addr, uint8_t out[CHAR_BYTES]) const {
    wr(CMAH, addr);
    wr(CMM, 0x50);                       // read NVM → shadow
    for (int t = 0; t < 50; ++t) {
        if ((rd(STAT) & STAT_NVM_BUSY) == 0) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    for (uint8_t i = 0; i < CHAR_BYTES; ++i) {
        wr(CMAL, i);
        out[i] = rd(CMDO);
    }
    return true;
}

size_t At7456e::upload_font(const uint8_t* data, size_t char_count,
                            int sentinel_addr, uint8_t sentinel_byte0) {
    if (char_count > CHAR_COUNT) char_count = CHAR_COUNT;

    // Sentinel: якщо шрифт уже залитий — пропускаємо (NVM-запис зношує чіп).
    if (sentinel_addr >= 0 && sentinel_addr < static_cast<int>(char_count)) {
        uint8_t cur[CHAR_BYTES];
        read_char(static_cast<uint8_t>(sentinel_addr), cur);
        if (cur[0] == sentinel_byte0) {
            ESP_LOGI(TAG, "font already present (sentinel match) — skip flash");
            return char_count;
        }
    }

    const uint8_t vm0 = rd(VM0);
    enable_osd(false);                   // OSD off на час NVM-операцій
    vTaskDelay(pdMS_TO_TICKS(10));

    size_t ok = 0;
    for (size_t a = 0; a < char_count; ++a) {
        if (upload_char(static_cast<uint8_t>(a), data + a * CHAR_BYTES)) ++ok;
    }

    wr(VM0, vm0);                        // відновити попередній стан
    ESP_LOGI(TAG, "font flashed: %u/%u chars", (unsigned)ok, (unsigned)char_count);
    return ok;
}

} // namespace modesp::osd
