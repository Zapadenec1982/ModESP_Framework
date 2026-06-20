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
#include <cstring>

static const char* TAG = "LD2410b";

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
    presence_hold_ms_ = (uint32_t)b.setting_or("presence_hold_ms", (float)presence_hold_ms_);
    timeout_ms_       = (uint32_t)b.setting_or("timeout_ms", (float)timeout_ms_);
}

bool Ld2410bDriver::init() {
    if (!port_) {
        ESP_LOGE(TAG, "[%s] No UART port bound", role_.c_str());
        return false;
    }
    ESP_LOGI(TAG, "[%s] Initialized (channel=%d, %s, hold=%lu ms, timeout=%lu ms)",
             role_.c_str(), (int)channel_, owner_ ? "owner" : "view",
             (unsigned long)presence_hold_ms_, (unsigned long)timeout_ms_);
    return true;
}

void Ld2410bDriver::update(uint32_t dt_ms) {
    if (!port_) return;

    // Only the owner instance drains the UART; views read the shared frame.
    if (owner_) port_->pump(dt_ms);

    // Presence hold timer — maintained for every instance (cheap, per-channel).
    bool raw_present = port_->fresh(timeout_ms_) && (port_->latest().state != 0);
    if (raw_present) {
        since_present_ms_ = 0;
        present_held_ = true;
    } else {
        if (since_present_ms_ < presence_hold_ms_) {
            since_present_ms_ += dt_ms;
        }
        if (since_present_ms_ >= presence_hold_ms_) {
            present_held_ = false;
        }
    }
}

bool Ld2410bDriver::read(float& value) {
    if (!port_ || !port_->fresh(timeout_ms_)) return false;
    const Ld2410Frame& f = port_->latest();
    switch (channel_) {
        case Channel::PRESENCE:      value = present_held_ ? 1.0f : 0.0f; break;
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
