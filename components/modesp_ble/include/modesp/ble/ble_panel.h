/**
 * @file ble_panel.h
 * @brief BlePanel — NimBLE central-CONNECT link to one iPixel/LED_BLE panel.
 *
 * Unlike BleCentral (passive observer for broadcast sensors), the LED panel is a
 * CONNECT device: scan for its adv name → ble_gap_connect → discover fa02(write)/
 * fa03(notify) → enable notify → write commands. The single NimBLE host is shared
 * with the sensor observer + peripheral advertising; connecting pauses the observer
 * scan and resumes it once connected (scan + connection coexist).
 *
 * The drivers/ble_led_panel actuator registers the panel's adv-name prefix here and
 * sends control/text frames via write_cmd(). Protocol: docs/ble/panel_protocol.md.
 *
 * NimBLE GATT-client funcs live in host/ble_gatt.h (NOT ble_gattc.h).
 */
#pragma once

#include "sdkconfig.h"   // CONFIG_MODESP_BLE_* must be defined before the guard

#if defined(CONFIG_MODESP_BLE_ENABLE) && defined(CONFIG_MODESP_BLE_CENTRAL)

#include <stdint.h>
#include <stddef.h>

struct ble_gap_event;
struct ble_gatt_error;
struct ble_gatt_svc;
struct ble_gatt_chr;
struct ble_gatt_dsc;

namespace modesp {

class BlePanel {
public:
    static BlePanel& instance();

    /// Driver-side: register the target adv-name prefix (e.g. "LED_BLE_E6C5EBE2").
    /// Empty/unset → the connect logic stays dormant.
    void set_target(const char* name_prefix);
    bool target_set() const { return name_len_ > 0; }
    bool is_connected() const { return state_ == State::READY; }

    /// Write a command to the panel's fa02 char. with_response=false for control
    /// (fire-and-forget); true for image/frame (ACT1025-reliable). Returns false
    /// if not connected. Safe to call from any task (NimBLE locks internally).
    bool write_cmd(const uint8_t* data, uint16_t len, bool with_response);

    /// Render a short ASCII string as a native iPixel text frame in fg colour (r,g,b,
    /// default white) with an optional panel-native effect: anim (0..7 — 0 static, others
    /// scroll/blink/breathe/…), speed (0..100), rainbow (0..9 colour-cycle). The panel runs
    /// the effect autonomously. NON-BLOCKING: enqueues to a background render task (latest-
    /// wins), so the caller never stalls on BLE I/O. No-op if null; dropped if disconnected.
    void show_text(const char* s, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
                   uint8_t anim = 0, uint8_t speed = 0x32, uint8_t rainbow = 0);

    /// Display a full-frame PNG image (form C). The PNG must be 64×16 RGB; the bytes are
    /// caller-owned and MUST outlive the send (static/.rodata — only a pointer is queued).
    /// save_slot ≥ 1 persists to a gallery slot, which is then shown. NON-BLOCKING (enqueues to
    /// the render task). UNVERIFIED on HW — validate a solid-colour frame first.
    void show_image(const uint8_t* png, size_t len, uint8_t save_slot = 1);

    /// Encode a 64×16 RGB888 framebuffer (3072 bytes, row-major, top-left pixel first) to PNG
    /// on-device (ROM miniz, zero flash) and display it — the practical path for rich/dynamic
    /// frames (draw in RAM, then one compressed upload). NON-BLOCKING. No-op if null / encode fails.
    void show_rgb888(const uint8_t* rgb, uint8_t save_slot = 1);

    // ── called from modesp_ble scan/host internals (NimBLE host task) ──
    bool name_matches(const uint8_t* adv_name, uint8_t adv_name_len) const;
    void on_scan_hit(const void* addr);            // ble_addr_t* — begin connect
    static int gap_event(struct ble_gap_event* event, void* arg);

private:
    enum class State : uint8_t { IDLE, CONNECTING, DISCOVERING, READY };

    void reset_link();                             // back to IDLE, resume observer scan
    void render_text_blocking(const char* s, uint8_t r, uint8_t g, uint8_t b,
                              uint8_t anim, uint8_t speed, uint8_t rainbow);  // build frame + chunked send
    void render_image_blocking(const uint8_t* png, uint32_t len, uint8_t slot);  // build PNG frame + send + show_slot
    bool ensure_render_task();                     // lazy queue + task creation (first show_text/show_image)
    static void render_task_fn(void* arg);         // background render task (drains the latest-frame queue)
    // GATT-client discovery callbacks (static members → access privates via instance()).
    static int on_chr(uint16_t conn, const struct ble_gatt_error* err,
                      const struct ble_gatt_chr* chr, void* arg);   // disc-all-chrs: log + match fa02/fa03
    static int on_dsc(uint16_t conn, const struct ble_gatt_error* err,
                      uint16_t chr_val_handle, const struct ble_gatt_dsc* dsc, void* arg);

    char     name_[24]      = {0};
    uint8_t  name_len_      = 0;
    State    state_         = State::IDLE;
    uint16_t conn_handle_   = 0xFFFF;   // BLE_HS_CONN_HANDLE_NONE
    uint16_t svc_start_     = 0;
    uint16_t svc_end_       = 0;
    uint16_t write_handle_  = 0;        // fa02
    uint8_t  write_props_   = 0;        // fa02 props (WRITE 0x08 / WRITE_NO_RSP 0x04) → picks write type
    uint16_t notify_handle_ = 0;        // fa03
    int64_t  cooldown_until_us_ = 0;    // after a failed discovery, suppress reconnect until this time
};

} // namespace modesp

#endif // CONFIG_MODESP_BLE_ENABLE && CONFIG_MODESP_BLE_CENTRAL
