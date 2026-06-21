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

#if defined(CONFIG_MODESP_BLE_CENTRAL)
#include "modesp/ble/ble_panel.h"
#endif

static const char* TAG = "Panel";

PanelModule::PanelModule()
    : BaseModule("panel", modesp::ModulePriority::LOW)
{}

bool PanelModule::on_init() {
    state_set("panel.text", "");
    ESP_LOGI(TAG, "Panel content module — rotates temp/humidity/presence on the LED panel");
    return true;
}

void PanelModule::on_update(uint32_t dt_ms) {
#if defined(CONFIG_MODESP_BLE_CENTRAL)
    auto& panel = modesp::BlePanel::instance();
    if (!panel.is_connected()) { shown_[0] = '\0'; return; }

    // Re-evaluate at ~4 Hz (cheap — show_text() is now non-blocking and we de-dupe);
    // advance the rotation slot every 4 s.
    eval_ms_ += dt_ms;
    if (eval_ms_ < 250) return;
    rotate_ms_ += eval_ms_;
    eval_ms_ = 0;
    if (rotate_ms_ >= 4000) { rotate_ms_ = 0; rot_++; }

    // Collect the fields that currently have data, each with its own colour.
    struct Entry { char buf[24]; uint8_t r, g, b; };
    Entry e[3];
    int n = 0;

    // ── temperature ── gate on health (drop stale/dead sensor) AND skip the 0.00 voltage-only
    //                   frame until a real reading lands (room_temp_ok can be true for it).
    float t = read_float("equipment.room_temp", -1000.0f);
    if (t > -100.0f && read_bool("equipment.room_temp_ok", false) && (seen_temp_ || std::fabs(t) >= 0.05f)) {
        seen_temp_ = true;
        Entry& x = e[n++];
        snprintf(x.buf, sizeof(x.buf), "%.1fC", t);
        if      (t < 18.0f) { x.r = 80;  x.g = 140; x.b = 255; }   // cold    → blue
        else if (t > 27.0f) { x.r = 255; x.g = 70;  x.b = 40;  }   // warm    → red
        else                { x.r = 60;  x.g = 220; x.b = 80;  }   // comfort → green
    }

    // ── humidity ──
    float h = read_float("equipment.room_humid", -1000.0f);
    if (h > -100.0f && read_bool("equipment.room_humid_ok", false) && h > 0.05f) {
        Entry& x = e[n++];
        snprintf(x.buf, sizeof(x.buf), "%.0f%%", h);
        if      (h < 30.0f) { x.r = 255; x.g = 150; x.b = 40;  }   // dry   → orange
        else if (h > 60.0f) { x.r = 80;  x.g = 140; x.b = 255; }   // humid → blue
        else                { x.r = 60;  x.g = 220; x.b = 80;  }   // ok    → green
    }

    // ── presence ── require health so a never-seen/dead radar drops the slot (no fake "AWAY")
    float p = read_float("equipment.presence", -1000.0f);
    if (p > -100.0f && read_bool("equipment.presence_ok", false)) {
        Entry& x = e[n++];
        if (p > 0.5f) { snprintf(x.buf, sizeof(x.buf), "HERE"); x.r = 60; x.g = 220; x.b = 80; }
        else          { snprintf(x.buf, sizeof(x.buf), "AWAY"); x.r = 90; x.g = 90;  x.b = 90; }
    }

    const char* buf; uint8_t r, g, b;
    if (n == 0) { buf = "ModESP"; r = g = b = 255; }               // nothing yet → white splash
    else { const Entry& x = e[rot_ % n]; buf = x.buf; r = x.r; g = x.g; b = x.b; }

    // Re-push on a text OR colour change (a threshold flip can recolour the same string).
    if (strncmp(buf, shown_, sizeof(shown_)) != 0 ||
        r != shown_rgb_[0] || g != shown_rgb_[1] || b != shown_rgb_[2]) {
        strncpy(shown_, buf, sizeof(shown_) - 1);
        shown_[sizeof(shown_) - 1] = '\0';
        shown_rgb_[0] = r; shown_rgb_[1] = g; shown_rgb_[2] = b;
        panel.show_text(buf, r, g, b);
        state_set("panel.text", buf);
        ESP_LOGI(TAG, "panel <- '%s' (rgb=%02x%02x%02x)", buf, r, g, b);
    }
#else
    (void)dt_ms;
#endif
}
