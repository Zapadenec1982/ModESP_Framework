/**
 * @file panel_module.cpp
 * @brief Rotate live sensor values (temperature / humidity / presence) on the
 *        iPixel BLE LED panel, with a threshold-based colour per value.
 */
#include "panel_module.h"
#include "panel_text.h"                  // this module's text-output slots API (panel.slotN)
#include "modesp/hal/panel_port.h"       // IPanelPort — the panel backend a driver publishes
#include "modesp/hal/driver_registry.h"  // DriverRegistry::panel_port()
#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>

static const char* TAG = "Panel";

// Text animation effect is WEB-CONTROLLED (panel.anim, 0..7 — see manifest). Speed + rainbow fixed.
//   anim: 0 static · 1 scroll→← · 2 scroll←→ · 3 ↑ · 4 ↓ · 5 blink · 6 breathe · 7 drop-in
static constexpr uint8_t kSpeed   = 80;  // 0..100 — for scroll/blink/breathe/drop
static constexpr uint8_t kRainbow = 0;   // 0 off · 1..9 colour-cycle (overrides threshold colour)

// Panel-font private-use icon bytes (hand-authored pictographs in tools/gen_osd_font.py PANEL_ICONS;
// show_text renders them inline as ordinary glyphs). Emitted as a leading char in each readout.
static constexpr char ICON_THERMO = static_cast<char>(0x80);
static constexpr char ICON_DROP   = static_cast<char>(0x81);
static constexpr char ICON_PERSON = static_cast<char>(0x82);
static constexpr char ICON_CLOCK  = static_cast<char>(0x85);

// Parse "#RRGGBB" → out[3]. Returns false (out untouched) on malformed input.
static bool parse_hex_color(const char* s, uint8_t out[3]) {
    if (!s || s[0] != '#') return false;
    auto hx = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[6];
    for (int i = 0; i < 6; i++) { v[i] = hx(s[1 + i]); if (v[i] < 0) return false; }
    out[0] = static_cast<uint8_t>(v[0] * 16 + v[1]);
    out[1] = static_cast<uint8_t>(v[2] * 16 + v[3]);
    out[2] = static_cast<uint8_t>(v[4] * 16 + v[5]);
    return true;
}

PanelModule::PanelModule()
    : BaseModule("panel", modesp::ModulePriority::LOW)
{}

void PanelModule::on_bind(modesp::DriverManager& /*drivers*/,
                          const modesp::BindingTable& /*bindings*/,
                          modesp::HAL& /*hal*/) {
    // The panel driver (if bound) published its IPanelPort at factory time. No panel
    // driver → null → the module runs but produces no output.
    port_ = modesp::DriverRegistry::panel_port();
    ESP_LOGI(TAG, "panel backend %s", port_ ? "resolved" : "absent (no output)");
}

bool PanelModule::on_init() {
    state_set("panel.text", "");
    state_set("panel.connected", false);
    for (int i = 0; i < modesp::panel_text::SLOTS; i++) state_set(modesp::panel_text::slot(i), "");  // text-output slots
    ESP_LOGI(TAG, "Panel content module — clock/temp/humidity rotation + web control + %d text slots",
             modesp::panel_text::SLOTS);
    return true;
}

void PanelModule::on_update(uint32_t dt_ms) {
    if (!port_) { (void)dt_ms; return; }   // no panel backend bound → nothing to drive

    // ── Web/MQTT control: connection status + power + brightness (applied on change) ──
    bool conn = port_->connected();
    if (conn != last_connected_) { last_connected_ = conn; state_set("panel.connected", conn); }
    if (!conn) { shown_[0] = '\0'; last_power_ = -1; last_bright_ = -1; return; }

    int power = read_bool("panel.power", true) ? 1 : 0;
    if (power != last_power_) {
        last_power_ = power;
        port_->set_power(power != 0);
        ESP_LOGI(TAG, "panel power %s (web)", power ? "ON" : "OFF");
    }
    int bright = read_int("panel.brightness", 80);
    if (bright < 5) bright = 5; else if (bright > 100) bright = 100;
    if (bright != last_bright_) {
        last_bright_ = bright;
        port_->set_brightness(bright);
        ESP_LOGI(TAG, "panel brightness %d%% (web)", bright);
    }
    if (power == 0) { shown_[0] = '\0'; return; }                              // panel off → nothing
    const uint8_t web_anim = static_cast<uint8_t>(read_int("panel.anim", 0));  // web-selected text effect

    // ── Web message override ── a non-empty panel.message (iPixel tab text field) shows that
    // text in panel.color (default white), overriding the rotation (and the rotate pause).
    // Empty message → resume rotation.
    char msg[32];
    if (read_string("panel.message", msg, sizeof(msg)) && msg[0] != '\0') {
        uint8_t mc[3] = {255, 255, 255};                 // default white
        char colbuf[12];
        if (read_string("panel.color", colbuf, sizeof(colbuf))) parse_hex_color(colbuf, mc);
        if (strncmp(msg, shown_, sizeof(shown_)) != 0 ||
            shown_rgb_[0] != mc[0] || shown_rgb_[1] != mc[1] || shown_rgb_[2] != mc[2] ||
            web_anim != shown_anim_) {
            strncpy(shown_, msg, sizeof(shown_) - 1); shown_[sizeof(shown_) - 1] = '\0';
            shown_rgb_[0] = mc[0]; shown_rgb_[1] = mc[1]; shown_rgb_[2] = mc[2]; shown_anim_ = web_anim;
            port_->show_text(msg, mc[0], mc[1], mc[2], web_anim, kSpeed, kRainbow);
            state_set("panel.text", msg);
        }
        return;
    }

    if (!read_bool("panel.rotate", true)) { shown_[0] = '\0'; return; }        // paused (no message) → hold

    // ── DIAGNOSTIC anim sweep ─────────────────────────────────────────────────────────────
    // Flip to true, rebuild (recompiles ONLY this file — no menuconfig, no sdkconfig.h churn),
    // flash: the panel cycles the native animation byte 0..7 (~6 s each), labelled
    // "ANIM0".."ANIM7" in green (rainbow off) so each effect is identifiable. Set back to false
    // and rebuild for normal sensor rotation.
    static constexpr bool kAnimSweep = false;
    if (kAnimSweep) {
        eval_ms_ += dt_ms;
        if (eval_ms_ < 6000) return;
        eval_ms_ = 0;
        uint8_t anim = static_cast<uint8_t>(rot_ % 8);
        rot_++;
        char tbuf[12];
        snprintf(tbuf, sizeof(tbuf), "ANIM%u", static_cast<unsigned>(anim));
        port_->show_text(tbuf, 0, 255, 0, anim, /*speed=*/80, /*rainbow=*/0);
        ESP_LOGI(TAG, "ANIM TEST: animation=%u speed=80 rainbow=0 -> '%s'",
                 static_cast<unsigned>(anim), tbuf);
        return;
    }

    // Re-evaluate at ~4 Hz (cheap — show_text() is now non-blocking and we de-dupe);
    // advance the rotation slot every 4 s.
    eval_ms_ += dt_ms;
    if (eval_ms_ < 250) return;
    rotate_ms_ += eval_ms_;
    eval_ms_ = 0;
    if (rotate_ms_ >= 4000) { rotate_ms_ = 0; rot_++; }

    // Collect the fields that currently have data, each with its own colour.
    struct Entry { char buf[32]; uint8_t r, g, b; uint8_t anim; };
    Entry e[4 + modesp::panel_text::SLOTS];   // clock + temp + humidity + presence + text slots
    int n = 0;

    // ── clock ── HH:MM once SNTP has set the wall clock (skip the 1970 pre-sync time)
    time_t now = time(nullptr);
    if (now > 1600000000) {   // ~2020-09 → time is synced
        struct tm tmv;
        localtime_r(&now, &tmv);
        Entry& x = e[n++];
        snprintf(x.buf, sizeof(x.buf), "%c%02d:%02d", ICON_CLOCK, tmv.tm_hour, tmv.tm_min);
        x.r = 180; x.g = 200; x.b = 255;   // soft blue-white
        x.anim = web_anim;
    }

    // ── temperature ── gate on health (drop stale/dead sensor) AND skip the 0.00 voltage-only
    //                   frame until a real reading lands (room_temp_ok can be true for it).
    float t = read_float("equipment.room_temp", -1000.0f);
    if (t > -100.0f && read_bool("equipment.room_temp_ok", false) && (seen_temp_ || std::fabs(t) >= 0.05f)) {
        seen_temp_ = true;
        Entry& x = e[n++];
        snprintf(x.buf, sizeof(x.buf), "%c%.1fC", ICON_THERMO, t);   // [thermo]29.7C
        if      (t < 18.0f) { x.r = 80;  x.g = 140; x.b = 255; }   // cold    → blue
        else if (t > 27.0f) { x.r = 255; x.g = 70;  x.b = 40;  }   // warm    → red
        else                { x.r = 60;  x.g = 220; x.b = 80;  }   // comfort → green
        x.anim = web_anim;
    }

    // ── humidity ──
    float h = read_float("equipment.room_humid", -1000.0f);
    if (h > -100.0f && read_bool("equipment.room_humid_ok", false) && h > 0.05f) {
        Entry& x = e[n++];
        snprintf(x.buf, sizeof(x.buf), "%c%.0f%%", ICON_DROP, h);   // [drop]65%
        if      (h < 30.0f) { x.r = 255; x.g = 150; x.b = 40;  }   // dry   → orange
        else if (h > 60.0f) { x.r = 80;  x.g = 140; x.b = 255; }   // humid → blue
        else                { x.r = 60;  x.g = 220; x.b = 80;  }   // ok    → green
        x.anim = web_anim;
    }

    // ── presence ── use the GATED occupancy presence.detected (honours
    // presence.max_distance + hold_sec), NOT raw equipment.presence (full range).
    // presence.sensor_ok gates the slot so a never-seen/dead radar drops it (no fake "AWAY").
    if (read_bool("presence.sensor_ok", false)) {
        Entry& x = e[n++];
        if (read_bool("presence.detected", false)) { snprintf(x.buf, sizeof(x.buf), "%cHERE", ICON_PERSON); x.r = 60; x.g = 220; x.b = 80; }
        else { snprintf(x.buf, sizeof(x.buf), "%cAWAY", ICON_PERSON); x.r = 90; x.g = 90;  x.b = 90; }
        x.anim = web_anim;
    }

    // ── text slots ── any module posts here with state_set(modesp::panel_text::slot(i), "msg");
    // non-empty slots rotate in (neutral white). See modules/panel/include/panel_text.h.
    for (int i = 0; i < modesp::panel_text::SLOTS && n < static_cast<int>(sizeof(e) / sizeof(e[0])); i++) {
        Entry& x = e[n];
        if (read_string(modesp::panel_text::slot(i), x.buf, sizeof(x.buf)) && x.buf[0] != '\0') {
            x.r = x.g = x.b = 255;   // module messages: neutral white
            x.anim = web_anim;
            n++;
        }
    }

    const char* buf; uint8_t r, g, b, anim;
    if (n == 0) { buf = "ModESP"; r = g = b = 255; anim = 0; }     // nothing yet → white splash
    else { const Entry& x = e[rot_ % n]; buf = x.buf; r = x.r; g = x.g; b = x.b; anim = x.anim; }

    // Re-push on a text, colour OR effect change (threshold flip / web anim change).
    if (strncmp(buf, shown_, sizeof(shown_)) != 0 ||
        r != shown_rgb_[0] || g != shown_rgb_[1] || b != shown_rgb_[2] || anim != shown_anim_) {
        strncpy(shown_, buf, sizeof(shown_) - 1);
        shown_[sizeof(shown_) - 1] = '\0';
        shown_rgb_[0] = r; shown_rgb_[1] = g; shown_rgb_[2] = b;
        shown_anim_ = anim;
        port_->show_text(buf, r, g, b, anim, kSpeed, kRainbow);
        // panel.text state + log: drop the leading icon byte so the value stays valid ASCII/UTF-8
        const char* clean = (static_cast<uint8_t>(buf[0]) >= 0x80) ? buf + 1 : buf;
        state_set("panel.text", clean);
        ESP_LOGI(TAG, "panel <- '%s' (rgb=%02x%02x%02x)", clean, r, g, b);
    }
}
