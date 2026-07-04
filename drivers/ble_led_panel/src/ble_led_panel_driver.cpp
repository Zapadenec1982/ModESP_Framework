/**
 * @file ble_led_panel_driver.cpp
 * @brief iPixel/LED_BLE 16x64 panel — connect profile + control + text encoder.
 *
 * All iPixel wire knowledge lives here: the GATT write/notify UUIDs handed to
 * modesp_ble's generic central link, the control byte-commands, the native text
 * frame encoder (glyphs + colour + effect) and its glyph font, plus a background
 * render task so the caller never blocks on BLE. modesp_ble owns only the radio
 * and the connect state machine (central_link.h). Protocol: docs/ble/panel_protocol.md.
 */
#include "ble_led_panel_driver.h"
#include "modesp/hal/hal_types.h"        // complete modesp::Binding for apply_settings()
#include "modesp/hal/hal.h"
#include "modesp/hal/driver_registry.h"
#include "etl/string_view.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "host/ble_uuid.h"              // BLE_UUID128_INIT + ble_uuid128_t
#include "panel_font_data.h"           // generated: PANEL_FONT + panel_font_index() (namespace modesp::panel)
#include <cstdio>
#include <cstring>

static const char* TAG = "ble_led_panel";

// ── iPixel GATT UUIDs ─────────────────────────────────────────────────────────
// The panel exposes its chars as FULL 128-bit base-derived UUIDs
// (0000faXX-0000-1000-8000-00805f9b34fb); its firmware does NOT expand a 16-bit
// discovery request to match them. BLE_UUID128_INIT takes the 16 bytes little-endian
// (LSB first); byte[12] carries the 16-bit low byte (0x02/0x03), byte[13]=0xfa.
#define PANEL_UUID128(lo) BLE_UUID128_INIT( \
    0xfb,0x34,0x9b,0x5f, 0x80,0x00, 0x00,0x80, 0x00,0x10, 0x00,0x00, (lo),0xfa,0x00,0x00)
static const ble_uuid128_t PANEL_WR_UUID = PANEL_UUID128(0x02);   // 0000fa02-... (write)
static const ble_uuid128_t PANEL_NT_UUID = PANEL_UUID128(0x03);   // 0000fa03-... (notify)

// ── Background render task ────────────────────────────────────────────────────
// show_text() enqueues (latest-wins, non-blocking) so the module's update loop never
// stalls on BLE; this task does the blocking chunked with-response sends.
namespace {
struct PanelMsg { char text[32]; uint8_t rgb[3]; uint8_t anim; uint8_t speed; uint8_t rainbow; };
QueueHandle_t s_queue       = nullptr;   // length 1 → xQueueOverwrite (newest wins)
TaskHandle_t  s_render_task = nullptr;
// Producer-side build buffer — show_text runs only on the module loop, and xQueueOverwrite
// copies it by value, so the render task reads its own snapshot (no shared-buffer race).
PanelMsg s_build;

// write_frame body: chunk the whole frame (244 B) with-response while the transport holds
// the write mutex, so a control write can't interleave between chunks and corrupt reassembly.
struct FrameCtx { const uint8_t* frame; size_t len; };
bool chunk_body(modesp::ble::ICentralLink* link, void* arg) {
    auto* c = static_cast<FrameCtx*>(arg);
    bool ok = true;
    for (size_t off = 0; off < c->len && ok; off += 244) {
        size_t clen = (c->len - off < 244) ? (c->len - off) : 244;
        ok = link->write(c->frame + off, static_cast<uint16_t>(clen), /*with_response=*/true);
    }
    return ok;
}

void render_task_fn(void* arg) {
    auto* self = static_cast<BleLedPanelDriver*>(arg);
    static PanelMsg m;   // static (single consumer)
    for (;;) {
        if (xQueueReceive(s_queue, &m, portMAX_DELAY) == pdTRUE)
            self->render_text_frame(m.text, m.rgb[0], m.rgb[1], m.rgb[2], m.anim, m.speed, m.rainbow);
    }
}

// Lazy one-time creation of the render queue + task (first show_text, always from the
// module loop → no creation race). False if allocation failed.
bool ensure_render_task(BleLedPanelDriver* self) {
    if (s_queue) return true;
    s_queue = xQueueCreate(1, sizeof(PanelMsg));
    if (!s_queue) return false;
    if (xTaskCreate(&render_task_fn, "panel_render", 4096, self, 4, &s_render_task) != pdPASS) {
        vQueueDelete(s_queue); s_queue = nullptr;
        ESP_LOGE(TAG, "panel render task create failed");
        return false;
    }
    return true;
}
} // namespace

// ── configuration ─────────────────────────────────────────────────────────────
void BleLedPanelDriver::configure(const char* role, modesp::ble::ICentralLink* link) {
    role_ = role;
    link_ = link;
}

void BleLedPanelDriver::apply_settings(const modesp::Binding& b) {
    brightness_ = static_cast<int>(b.setting_or("brightness", static_cast<float>(brightness_)));
}

bool BleLedPanelDriver::init() {
    ESP_LOGI(TAG, "[%s] init (brightness=%d%%) — connecting on scan match", role_.c_str(), brightness_);
    return true;
}

// ── control-plane byte commands (Confirmed-by-working-code, docs/ble/panel_protocol.md §1.1) ──
bool BleLedPanelDriver::push_brightness(int pct) {
    if (!link_) return false;
    if (pct < 5)   pct = 5;
    if (pct > 100) pct = 100;
    const uint8_t cmd[5] = {0x05, 0x00, 0x04, 0x80, static_cast<uint8_t>(pct)};
    return link_->write(cmd, sizeof(cmd), /*with_response=*/true);
}

// ── IActuatorDriver ─────────────────────────────────────────────────────────────
void BleLedPanelDriver::update(uint32_t /*dt_ms*/) {
    // The driver owns only the BLE link + wire format. Power, brightness, effect and text
    // are decided by the panel MODULE, which drives them through this driver's IPanelPort.
    bool ready = connected();
    if (ready && !was_ready_)
        ESP_LOGI(TAG, "[%s] panel connected (content driven by the panel module)", role_.c_str());
    was_ready_ = ready;
}

bool BleLedPanelDriver::set(bool /*state*/) {
    // No-op: power is owned by the panel module (panel.power). EquipmentBase drives every
    // bound actuator to its default request (false) each tick; honoring that here would
    // force the panel OFF and flood the link.
    return is_healthy();
}

bool BleLedPanelDriver::set_value(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    brightness_ = static_cast<int>(v * 100.0f + 0.5f);
    if (connected() && push_brightness(brightness_)) {
        sent_brightness_ = brightness_;
        return true;
    }
    return false;   // not connected → write dropped
}

bool BleLedPanelDriver::is_healthy() const {
    return link_ && link_->connected();
}

// ── IPanelPort (the panel module drives content through this) ──────────────────
bool BleLedPanelDriver::connected() const {
    return link_ && link_->connected();
}

void BleLedPanelDriver::set_power(bool on) {
    if (!link_) return;
    const uint8_t cmd[5] = {0x05, 0x00, 0x07, 0x01, static_cast<uint8_t>(on ? 1 : 0)};
    link_->write(cmd, sizeof(cmd), /*with_response=*/true);
}

void BleLedPanelDriver::set_brightness(int pct) {
    if (push_brightness(pct)) { brightness_ = (pct < 5) ? 5 : (pct > 100 ? 100 : pct); sent_brightness_ = brightness_; }
}

void BleLedPanelDriver::show_text(const char* s, uint8_t r, uint8_t g, uint8_t b,
                                  uint8_t anim, uint8_t speed, uint8_t rainbow) {
    if (s == nullptr) return;
    if (!ensure_render_task(this)) return;
    size_t i = 0;
    for (; s[i] && i < sizeof(s_build.text) - 1; i++) s_build.text[i] = s[i];
    s_build.text[i] = '\0';
    s_build.rgb[0] = r; s_build.rgb[1] = g; s_build.rgb[2] = b;
    s_build.anim = anim; s_build.speed = speed; s_build.rainbow = rainbow;
    xQueueOverwrite(s_queue, &s_build);      // latest-wins, non-blocking
}

// ── native iPixel text-frame encoder (render task only) ────────────────────────
void BleLedPanelDriver::render_text_frame(const char* s, uint8_t r, uint8_t g, uint8_t b,
                                          uint8_t anim, uint8_t speed, uint8_t rainbow) {
    if (!connected() || s == nullptr) return;
    using namespace modesp::panel;

    int n = 0;
    while (s[n] && n < 31) n++;                     // up to 31 chars (the panel scrolls them)
    const uint8_t R = r, G = g, B = b;

    // payload: [num_chars][3 rsv][anim/speed/rainbow 3][fg RGB 3][bg_en][bg RGB 3] + glyph blocks
    // static: render runs only on the single panel_render task, so these large buffers live off
    // the 4 KB task stack and are not re-created per call.
    static uint8_t payload[700];   // 14-byte header + 31*20 glyph blocks = 634
    size_t pl = 0;
    payload[pl++] = static_cast<uint8_t>(n);
    payload[pl++] = 0; payload[pl++] = 0; payload[pl++] = 0;       // reserved
    payload[pl++] = anim; payload[pl++] = speed; payload[pl++] = rainbow;  // native effect/speed/rainbow
    payload[pl++] = R; payload[pl++] = G; payload[pl++] = B;       // fg RGB
    payload[pl++] = 0;                                             // bg_enable=0
    payload[pl++] = 0; payload[pl++] = 0; payload[pl++] = 0;       // bg RGB
    for (int i = 0; i < n; i++) {
        uint8_t idx = panel_font_index(static_cast<uint8_t>(s[i]));
        payload[pl++] = 0x00;                                     // glyph block type: char 16x8
        payload[pl++] = R; payload[pl++] = G; payload[pl++] = B;  // per-char color
        for (int row = 0; row < PANEL_FONT_H; row++)
            payload[pl++] = PANEL_FONT[idx * PANEL_FONT_H + row];
    }

    uint32_t crc = esp_rom_crc32_le(0, payload, pl);

    // frame: [total_len u16][00 01][has_next 00][payload_size u32][crc u32][00][slot 0x65] + payload
    static uint8_t frame[720];   // payload + 15-byte header (<=649 for 31 chars); static, same single-task reason
    size_t fl = 0;
    uint16_t total = static_cast<uint16_t>(pl + 15);
    frame[fl++] = total & 0xFF;          frame[fl++] = (total >> 8) & 0xFF;
    frame[fl++] = 0x00;                  frame[fl++] = 0x01;       // text type
    frame[fl++] = 0x00;                                           // has_next (single frame)
    frame[fl++] = pl & 0xFF;  frame[fl++] = (pl >> 8) & 0xFF;  frame[fl++] = (pl >> 16) & 0xFF;  frame[fl++] = (pl >> 24) & 0xFF;
    frame[fl++] = crc & 0xFF; frame[fl++] = (crc >> 8) & 0xFF; frame[fl++] = (crc >> 16) & 0xFF; frame[fl++] = (crc >> 24) & 0xFF;
    frame[fl++] = 0x00;                  frame[fl++] = 0x65;       // 0x00, slot
    memcpy(frame + fl, payload, pl);     fl += pl;

    // send the whole frame atomically (write_frame holds the transport write mutex across
    // all chunks) so a control write can't interleave and corrupt the panel's reassembly.
    FrameCtx ctx{frame, fl};
    if (!link_->write_frame(&chunk_body, &ctx)) return;   // link dropped mid-send — next show_text retries
    ESP_LOGI(TAG, "panel show_text '%s' (%d chars, rgb=%02x%02x%02x, %u-byte frame)",
             s, n, R, G, B, (unsigned)fl);
}

// ═══════════════════════════════════════════════════════════════
// Factory + registration (optional via CONFIG_MODESP_DRIVER_BLE_LED_PANEL).
// A normal actuator: DriverManager creates it from the `panel` role binding and
// indexes it by role. The factory registers the panel's connect profile (adv-name
// + fa02/fa03 UUIDs) with modesp_ble's generic central link and returns the driver.
// The panel module resolves this SAME object by role (find_actuator + as_panel).
// ═══════════════════════════════════════════════════════════════

namespace {
BleLedPanelDriver s_panel;   // single panel instance (multiple_per_bus: false)

modesp::IActuatorDriver* ble_led_panel_factory(const modesp::Binding& b, modesp::HAL& hal) {
    auto* dev = hal.find_ble_device(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!dev) {
        ESP_LOGE(TAG, "BLE device '%s' not in board.json ble_devices", b.hardware_id.c_str());
        return nullptr;
    }
    const char* name = dev->name.c_str();
    if (name[0] == '\0') {
        ESP_LOGE(TAG, "ble_devices '%s' needs a 'name' (panel adv-name prefix) to connect",
                 b.hardware_id.c_str());
        return nullptr;
    }
    modesp::ble::ConnectProfile profile{};
    profile.name_prefix = name;
    profile.write_uuid  = &PANEL_WR_UUID.u;
    profile.notify_uuid = &PANEL_NT_UUID.u;
    profile.on_notify   = nullptr;
    profile.ctx         = nullptr;
    modesp::ble::ICentralLink* link = modesp::ble::register_connect_profile(profile);
    if (!link) {
        ESP_LOGE(TAG, "central-link connect-profile registration failed");
        return nullptr;
    }
    s_panel.configure(b.role.c_str(), link);
    s_panel.apply_settings(b);
    return &s_panel;   // panel module resolves this by role via find_actuator("panel")->as_panel()
}
} // namespace

// Register hook (called at boot by the generated register-all). Registers the factory AND
// a connect-name-prefix matcher — the latter at BOOT so an UNBOUND panel is visible in the
// unified GET /api/ble/scan before any binding exists (you subscribe it, then bind). The
// full advertised name of the subscribed panel becomes its connect target.
extern "C" void modesp_register_driver_ble_led_panel(void) {
    modesp::DriverRegistry::register_actuator("ble_led_panel", &ble_led_panel_factory);
    modesp::ble::register_connect_matcher("LED_BLE", "ble_led_panel");
}
