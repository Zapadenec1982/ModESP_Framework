/**
 * @file panel_module.cpp
 * @brief Rotate live sensor values (temperature / humidity / presence) on the
 *        iPixel BLE LED panel, with a threshold-based colour per value.
 */
#include "panel_module.h"
#include "sdkconfig.h"            // CONFIG_* must be defined before the guards below
#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>

#if defined(CONFIG_MODESP_BLE_CENTRAL)
#include "modesp/ble/ble_panel.h"
#endif

static const char* TAG = "Panel";

// ── Per-field display effect (HW-confirmed anim mapping 2026-06-21; edit + rebuild this file) ──
//   anim: 0 static · 1 scroll→← · 2 scroll←→ · 3 ↑ · 4 ↓ · 5 blink · 6 breathe · 7 drop-in
//   Default static: the readout is short and must stay glanceable. rainbow overrides colour.
static constexpr uint8_t kTempAnim     = 7;   // drop-in: temp "assembles" each time it rotates in
static constexpr uint8_t kHumidAnim    = 0;   // static — readable
static constexpr uint8_t kPresenceAnim = 0;   // static
static constexpr uint8_t kSpeed        = 80;  // 0..100 — for scroll/blink/breathe/drop
static constexpr uint8_t kRainbow      = 0;   // 0 off · 1..9 colour-cycle (overrides threshold colour)

// Panel-font private-use icon bytes (hand-authored pictographs in tools/gen_osd_font.py PANEL_ICONS;
// show_text renders them inline as ordinary glyphs). Emitted as a leading char in each readout.
static constexpr char ICON_THERMO = static_cast<char>(0x80);
static constexpr char ICON_DROP   = static_cast<char>(0x81);
static constexpr char ICON_PERSON = static_cast<char>(0x82);
static constexpr char ICON_CLOCK  = static_cast<char>(0x85);

PanelModule::PanelModule()
    : BaseModule("panel", modesp::ModulePriority::LOW)
{}

bool PanelModule::on_init() {
    state_set("panel.text", "");
    ESP_LOGI(TAG, "Panel content module — rotates clock/temperature/humidity on the LED panel");
    return true;
}

void PanelModule::on_update(uint32_t dt_ms) {
#if defined(CONFIG_MODESP_BLE_CENTRAL)
    auto& panel = modesp::BlePanel::instance();
    if (!panel.is_connected()) { shown_[0] = '\0'; return; }

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
        panel.show_text(tbuf, 0, 255, 0, anim, /*speed=*/80, /*rainbow=*/0);
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
    struct Entry { char buf[24]; uint8_t r, g, b; uint8_t anim; };
    Entry e[4];   // clock + temp + humidity + presence
    int n = 0;

    // ── clock ── HH:MM once SNTP has set the wall clock (skip the 1970 pre-sync time)
    time_t now = time(nullptr);
    if (now > 1600000000) {   // ~2020-09 → time is synced
        struct tm tmv;
        localtime_r(&now, &tmv);
        Entry& x = e[n++];
        snprintf(x.buf, sizeof(x.buf), "%c%02d:%02d", ICON_CLOCK, tmv.tm_hour, tmv.tm_min);
        x.r = 180; x.g = 200; x.b = 255;   // soft blue-white
        x.anim = 0;
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
        x.anim = kTempAnim;
    }

    // ── humidity ──
    float h = read_float("equipment.room_humid", -1000.0f);
    if (h > -100.0f && read_bool("equipment.room_humid_ok", false) && h > 0.05f) {
        Entry& x = e[n++];
        snprintf(x.buf, sizeof(x.buf), "%c%.0f%%", ICON_DROP, h);   // [drop]65%
        if      (h < 30.0f) { x.r = 255; x.g = 150; x.b = 40;  }   // dry   → orange
        else if (h > 60.0f) { x.r = 80;  x.g = 140; x.b = 255; }   // humid → blue
        else                { x.r = 60;  x.g = 220; x.b = 80;  }   // ok    → green
        x.anim = kHumidAnim;
    }

    // ── presence ── use the GATED occupancy presence.detected (honours
    // presence.max_distance + hold_sec), NOT raw equipment.presence (full range).
    // presence.sensor_ok gates the slot so a never-seen/dead radar drops it (no fake "AWAY").
    if (read_bool("presence.sensor_ok", false)) {
        Entry& x = e[n++];
        if (read_bool("presence.detected", false)) { snprintf(x.buf, sizeof(x.buf), "%cHERE", ICON_PERSON); x.r = 60; x.g = 220; x.b = 80; }
        else { snprintf(x.buf, sizeof(x.buf), "%cAWAY", ICON_PERSON); x.r = 90; x.g = 90;  x.b = 90; }
        x.anim = kPresenceAnim;
    }

    const char* buf; uint8_t r, g, b, anim;
    if (n == 0) { buf = "ModESP"; r = g = b = 255; anim = 0; }     // nothing yet → white splash
    else { const Entry& x = e[rot_ % n]; buf = x.buf; r = x.r; g = x.g; b = x.b; anim = x.anim; }

    // Re-push on a text OR colour change (a threshold flip can recolour the same string).
    if (strncmp(buf, shown_, sizeof(shown_)) != 0 ||
        r != shown_rgb_[0] || g != shown_rgb_[1] || b != shown_rgb_[2]) {
        strncpy(shown_, buf, sizeof(shown_) - 1);
        shown_[sizeof(shown_) - 1] = '\0';
        shown_rgb_[0] = r; shown_rgb_[1] = g; shown_rgb_[2] = b;
        panel.show_text(buf, r, g, b, anim, kSpeed, kRainbow);
        // panel.text state + log: drop the leading icon byte so the value stays valid ASCII/UTF-8
        const char* clean = (static_cast<uint8_t>(buf[0]) >= 0x80) ? buf + 1 : buf;
        state_set("panel.text", clean);
        ESP_LOGI(TAG, "panel <- '%s' (rgb=%02x%02x%02x)", clean, r, g, b);
    }
#else
    (void)dt_ms;
#endif
}
