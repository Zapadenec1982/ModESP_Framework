/**
 * @file ld2410b_driver.cpp
 * @brief HLK-LD2410B mmWave presence radar — UART frame parser + ISensorDriver
 *
 * Protocol (target data report, streamed continuously, no host command):
 *   header  : F4 F3 F2 F1
 *   length  : 2 bytes (LE) — payload length
 *   payload : [0]=type(02 basic/01 eng) [1]=AA [2]=state
 *             [3..4]=moving cm (LE)  [5]=moving energy
 *             [6..7]=static cm (LE)  [8]=static energy
 *             [9..10]=detect cm (LE) ... (engineering adds per-gate data)
 *   footer  : F8 F7 F6 F5
 * The first 11 payload bytes are identical in basic and engineering modes, so we
 * decode only those and ignore the rest.
 *
 * The HAL owns the UART port (installed from board.json uart_buses); this driver
 * is a pure reader. One Ld2410Port per physical sensor parses the byte stream;
 * each Ld2410bDriver instance is a view onto one decoded channel.
 */

#include "ld2410b_driver.h"
#include "modesp/hal/hal_types.h"   // complete modesp::Binding for apply_settings()
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "LD2410b";

// ═══════════════════════════════════════════════════════════════
// LD2410 command protocol (config frames FD FC FB FA … 04 03 02 01).
// Used once at init to set absence duration, max distance gate and sensitivities
// — needs MCU TX wired to the sensor RX (board.json uart tx pin).
// ═══════════════════════════════════════════════════════════════
static void ld2410_put_u32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF;        p[1] = (v >> 8)  & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

/// Send one command frame and block until the bytes are on the wire.
static void ld2410_send_cmd(uart_port_t port, uint16_t cmd,
                            const uint8_t* data, uint16_t dlen) {
    uint8_t f[40];
    uint16_t n = 0;
    f[n++] = 0xFD; f[n++] = 0xFC; f[n++] = 0xFB; f[n++] = 0xFA;  // header
    uint16_t len = 2 + dlen;                                     // cmd word + data
    f[n++] = len & 0xFF; f[n++] = (len >> 8) & 0xFF;
    f[n++] = cmd & 0xFF; f[n++] = (cmd >> 8) & 0xFF;
    for (uint16_t i = 0; i < dlen; i++) f[n++] = data[i];
    f[n++] = 0x04; f[n++] = 0x03; f[n++] = 0x02; f[n++] = 0x01;  // footer
    uart_write_bytes(port, f, n);
    uart_wait_tx_done(port, pdMS_TO_TICKS(100));
}

// ═══════════════════════════════════════════════════════════════
// Decoded frame (common fields shared by basic + engineering modes)
// ═══════════════════════════════════════════════════════════════
struct Ld2410Frame {
    uint8_t  state     = 0;   // 0 none, 1 moving, 2 stationary, 3 both
    uint16_t moving_cm = 0;
    uint8_t  moving_e  = 0;   // 0..100
    uint16_t static_cm = 0;
    uint8_t  static_e  = 0;   // 0..100
    uint16_t detect_cm = 0;
};

// ═══════════════════════════════════════════════════════════════
// Ld2410Port — shared per-UART-port frame parser
// ═══════════════════════════════════════════════════════════════
class Ld2410Port {
public:
    void attach(uart_port_t port) {
        port_ = port;
        len_ = 0;
        has_frame_ = false;
        uptime_ms_ = 0;
        last_frame_ms_ = 0;
    }

    uart_port_t port_num() const { return port_; }

    /// Drain the UART RX buffer and extract the most recent complete frame.
    void pump(uint32_t dt_ms) {
        uptime_ms_ += dt_ms;
        if (len_ < BUF_SIZE) {
            int n = uart_read_bytes(port_, buf_ + len_, BUF_SIZE - len_, 0);
            if (n > 0) len_ += (size_t)n;
        }
        scan();
    }

    /// True if a frame was decoded within the last `timeout_ms`.
    bool fresh(uint32_t timeout_ms) const {
        return has_frame_ && (uptime_ms_ - last_frame_ms_) <= timeout_ms;
    }

    const Ld2410Frame& latest() const { return latest_; }

    /// Push config to the radar once (owner instance, at init). Needs MCU TX wired
    /// to the sensor RX. All config commands must sit between enable(0xFF) and
    /// end(0xFE). Best-effort: ACK frames are flushed, not verified.
    void configure_sensor(uint16_t max_gate, uint32_t absence_s,
                          uint8_t move_sens, uint8_t still_sens) {
        uart_flush_input(port_);

        const uint8_t enable[2] = {0x01, 0x00};
        ld2410_send_cmd(port_, 0x00FF, enable, 2);      // enter config mode
        vTaskDelay(pdMS_TO_TICKS(100));

        // 0x0060: max moving gate, max static gate, no-one (absence) duration.
        // Each parameter is a 2-byte word id followed by a 4-byte LE value.
        uint8_t v[18] = {0};
        v[0]  = 0x00; v[1]  = 0x00; ld2410_put_u32(&v[2],  max_gate);
        v[6]  = 0x01; v[7]  = 0x00; ld2410_put_u32(&v[8],  max_gate);
        v[12] = 0x02; v[13] = 0x00; ld2410_put_u32(&v[14], absence_s);
        ld2410_send_cmd(port_, 0x0060, v, 18);
        vTaskDelay(pdMS_TO_TICKS(100));

        // 0x0064: per-gate sensitivity (gate word 0xFFFF = all gates). Skip if both 0.
        if (move_sens > 0 || still_sens > 0) {
            uint8_t s[18] = {0};
            s[0]  = 0x00; s[1]  = 0x00; s[2] = 0xFF; s[3] = 0xFF;   // gate = all
            s[6]  = 0x01; s[7]  = 0x00; ld2410_put_u32(&s[8],  move_sens  ? move_sens  : 50);
            s[12] = 0x02; s[13] = 0x00; ld2410_put_u32(&s[14], still_sens ? still_sens : 50);
            ld2410_send_cmd(port_, 0x0064, s, 18);
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        ld2410_send_cmd(port_, 0x00FE, nullptr, 0);     // exit config mode
        vTaskDelay(pdMS_TO_TICKS(100));

        uart_flush_input(port_);
        len_ = 0;   // drop any ACK bytes left in the parse buffer
    }

private:
    void scan() {
        size_t i = 0;
        size_t consumed_to = 0;   // end of last fully-parsed frame
        bool   pending = false;   // header found but frame not yet complete

        while (i + HDR_LEN + LEN_LEN <= len_) {
            if (!(buf_[i] == 0xF4 && buf_[i + 1] == 0xF3 &&
                  buf_[i + 2] == 0xF2 && buf_[i + 3] == 0xF1)) {
                i++;
                continue;
            }
            uint16_t plen = (uint16_t)(buf_[i + 4] | (buf_[i + 5] << 8));
            if (plen > MAX_PAYLOAD) { i++; continue; }   // bogus length → not a header
            size_t total = HDR_LEN + LEN_LEN + plen + FTR_LEN;
            if (i + total > len_) { pending = true; break; }  // need more bytes
            size_t foff = i + HDR_LEN + LEN_LEN + plen;
            if (buf_[foff] == 0xF8 && buf_[foff + 1] == 0xF7 &&
                buf_[foff + 2] == 0xF6 && buf_[foff + 3] == 0xF5) {
                parse_payload(buf_ + i + HDR_LEN + LEN_LEN, plen);
                i += total;
                consumed_to = i;
                continue;
            }
            i++;   // header magic but no footer → false positive, advance
        }

        // Compaction: keep the partial frame (pending), else drop parsed frames,
        // else retain a short tail in case a header is split across reads.
        size_t drop;
        if (pending)            drop = i;
        else if (consumed_to)   drop = consumed_to;
        else                    drop = (len_ > KEEP_TAIL) ? (len_ - KEEP_TAIL) : 0;

        if (drop > 0 && drop <= len_) {
            len_ -= drop;
            if (len_) memmove(buf_, buf_ + drop, len_);
        }
        if (len_ == BUF_SIZE) len_ = 0;   // safety: never deadlock on a full buffer
    }

    void parse_payload(const uint8_t* p, uint16_t plen) {
        if (plen < 11) return;       // too short for the common fields
        if (p[1] != 0xAA) return;    // intra-frame head marker
        latest_.state     = p[2];
        latest_.moving_cm = (uint16_t)(p[3] | (p[4] << 8));
        latest_.moving_e  = p[5];
        latest_.static_cm = (uint16_t)(p[6] | (p[7] << 8));
        latest_.static_e  = p[8];
        latest_.detect_cm = (uint16_t)(p[9] | (p[10] << 8));
        has_frame_ = true;
        last_frame_ms_ = uptime_ms_;
    }

    static constexpr size_t HDR_LEN     = 4;
    static constexpr size_t LEN_LEN     = 2;
    static constexpr size_t FTR_LEN     = 4;
    static constexpr size_t MAX_PAYLOAD = 64;    // basic=13, engineering ~35
    static constexpr size_t BUF_SIZE    = 128;
    static constexpr size_t KEEP_TAIL   = HDR_LEN + LEN_LEN - 1;  // possible split header

    uart_port_t  port_ = UART_NUM_1;
    uint8_t      buf_[BUF_SIZE];
    size_t       len_ = 0;
    Ld2410Frame  latest_{};
    bool         has_frame_ = false;
    uint32_t     uptime_ms_ = 0;
    uint32_t     last_frame_ms_ = 0;
};

// ═══════════════════════════════════════════════════════════════
// Ld2410bDriver — ISensorDriver view onto one channel
// ═══════════════════════════════════════════════════════════════

Ld2410bDriver::Channel Ld2410bDriver::channel_from_address(const char* address) {
    if (!address || address[0] == '\0')             return Channel::PRESENCE;
    if (strcmp(address, "presence") == 0)           return Channel::PRESENCE;
    if (strcmp(address, "moving") == 0)             return Channel::MOVING_DIST;
    if (strcmp(address, "static") == 0)             return Channel::STATIC_DIST;
    if (strcmp(address, "detect") == 0)             return Channel::DETECT_DIST;
    if (strcmp(address, "moving_energy") == 0)      return Channel::MOVING_ENERGY;
    if (strcmp(address, "static_energy") == 0)      return Channel::STATIC_ENERGY;
    ESP_LOGW(TAG, "Unknown address channel '%s' — defaulting to presence", address);
    return Channel::PRESENCE;
}

void Ld2410bDriver::configure(const char* role, Ld2410Port* port,
                              Channel channel, bool owner) {
    role_    = role;
    port_    = port;
    channel_ = channel;
    owner_   = owner;
}

void Ld2410bDriver::apply_settings(const modesp::Binding& b) {
    timeout_ms_ = (uint32_t)b.setting_or("timeout_ms", (float)timeout_ms_);
    absence_s_  = (uint32_t)b.setting_or("absence_s", (float)absence_s_);
    move_sens_  = (uint8_t)b.setting_or("move_sens", (float)move_sens_);
    still_sens_ = (uint8_t)b.setting_or("still_sens", (float)still_sens_);

    // max_cm → gate index. Each gate ≈ 0.75 m; gate 1 (~75 cm) is the closest the
    // radar's max-gate supports, gate 8 (~6 m) the farthest.
    uint32_t max_cm = (uint32_t)b.setting_or("max_cm", 600.0f);
    uint32_t gate = (max_cm + 37) / 75;     // round to nearest gate
    if (gate < 1) gate = 1;
    if (gate > 8) gate = 8;
    max_gate_ = (uint16_t)gate;
}

bool Ld2410bDriver::init() {
    if (!port_) {
        ESP_LOGE(TAG, "[%s] No UART port bound", role_.c_str());
        return false;
    }
    // The owner pushes config to the radar (needs MCU TX → sensor RX). If TX is not
    // wired the commands are simply ignored and the radar keeps its factory config —
    // reading still works either way.
    if (owner_) {
        ESP_LOGI(TAG, "[%s] Configuring radar: max_gate=%u (~%u cm), absence=%lu s, sens m/s=%u/%u",
                 role_.c_str(), max_gate_, (unsigned)(max_gate_ * 75),
                 (unsigned long)absence_s_, move_sens_, still_sens_);
        port_->configure_sensor(max_gate_, absence_s_, move_sens_, still_sens_);
    }
    ESP_LOGI(TAG, "[%s] Initialized (channel=%d, %s, timeout=%lu ms)",
             role_.c_str(), (int)channel_, owner_ ? "owner" : "view",
             (unsigned long)timeout_ms_);
    return true;
}

void Ld2410bDriver::update(uint32_t dt_ms) {
    if (!port_) return;
    // Only the owner instance drains the UART; views read the shared frame. The
    // radar itself holds presence for absence_s, so there's no driver-side hold.
    if (owner_) port_->pump(dt_ms);
}

bool Ld2410bDriver::read(float& value) {
    if (!port_ || !port_->fresh(timeout_ms_)) return false;
    const Ld2410Frame& f = port_->latest();
    switch (channel_) {
        case Channel::PRESENCE:      value = (f.state != 0) ? 1.0f : 0.0f; break;
        case Channel::MOVING_DIST:   value = (float)f.moving_cm; break;
        case Channel::STATIC_DIST:   value = (float)f.static_cm; break;
        case Channel::DETECT_DIST:   value = (float)f.detect_cm; break;
        case Channel::MOVING_ENERGY: value = (float)f.moving_e;  break;
        case Channel::STATIC_ENERGY: value = (float)f.static_e;  break;
        default:                     value = 0.0f; break;
    }
    return true;
}

bool Ld2410bDriver::is_healthy() const {
    return port_ && port_->fresh(timeout_ms_);
}

// ═══════════════════════════════════════════════════════════════
// Driver factory + registration (optional via CONFIG_MODESP_DRIVER_LD2410B)
//
// Several roles may bind the same UART hardware (multiple_per_bus): they share
// one Ld2410Port keyed by uart port. The first instance for a port is the owner
// (pumps the UART); the rest are passive views onto the decoded frame.
// ═══════════════════════════════════════════════════════════════

#include "modesp/hal/driver_registry.h"
#include "modesp/hal/hal.h"
#include "etl/string_view.h"

namespace {
Ld2410bDriver s_pool[modesp::MAX_SENSORS];
size_t        s_n = 0;
Ld2410Port    s_ports[modesp::MAX_UART_BUSES];
size_t        s_ports_n = 0;

Ld2410Port* get_or_create_port(uart_port_t port, bool& created) {
    for (size_t i = 0; i < s_ports_n; i++) {
        if (s_ports[i].port_num() == port) { created = false; return &s_ports[i]; }
    }
    if (s_ports_n >= modesp::MAX_UART_BUSES) { created = false; return nullptr; }
    Ld2410Port* p = &s_ports[s_ports_n++];
    p->attach(port);
    created = true;
    return p;
}

modesp::ISensorDriver* ld2410b_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_n >= modesp::MAX_SENSORS) {
        ESP_LOGE(TAG, "LD2410b pool exhausted");
        return nullptr;
    }
    auto* ub = hal.find_uart_bus(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!ub) {
        ESP_LOGE(TAG, "UART bus '%s' not found in HAL", b.hardware_id.c_str());
        return nullptr;
    }
    bool created = false;
    Ld2410Port* port = get_or_create_port((uart_port_t)ub->port, created);
    if (!port) {
        ESP_LOGE(TAG, "LD2410b port pool exhausted");
        return nullptr;
    }
    auto& drv = s_pool[s_n++];
    auto ch = Ld2410bDriver::channel_from_address(
        b.address.empty() ? "" : b.address.c_str());
    drv.configure(b.role.c_str(), port, ch, created);
    drv.apply_settings(b);
    return &drv;
}
} // namespace

MODESP_REGISTER_SENSOR(ld2410b, &ld2410b_factory)
