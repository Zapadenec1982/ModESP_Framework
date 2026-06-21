/**
 * @file panel_module.cpp
 * @brief Show equipment.room_temp on the iPixel BLE LED panel.
 */
#include "panel_module.h"
#include "sdkconfig.h"            // CONFIG_* must be defined before the guards below
#include "esp_log.h"
#include <cstdio>
#include <cstring>

#if defined(CONFIG_MODESP_BLE_CENTRAL)
#include "modesp/ble/ble_panel.h"
#endif

static const char* TAG = "Panel";

PanelModule::PanelModule()
    : BaseModule("panel", modesp::ModulePriority::LOW)
{}

bool PanelModule::on_init() {
    state_set("panel.text", "");
    ESP_LOGI(TAG, "Panel content module — equipment.room_temp → LED panel");
    return true;
}

void PanelModule::on_update(uint32_t dt_ms) {
#if defined(CONFIG_MODESP_BLE_CENTRAL)
    // Re-evaluate ~2 Hz; show_text() blocks per chunk, so don't run it every tick.
    since_ms_ += dt_ms;
    if (since_ms_ < 500) return;
    since_ms_ = 0;

    auto& panel = modesp::BlePanel::instance();
    if (!panel.is_connected()) { shown_[0] = '\0'; return; }

    float t = read_float("equipment.room_temp", -1000.0f);
    char buf[20];
    if (t <= -100.0f) snprintf(buf, sizeof(buf), "ModESP");      // key absent yet → splash
    else              snprintf(buf, sizeof(buf), "%.1fC", t);    // e.g. "28.5C"

    if (strncmp(buf, shown_, sizeof(shown_)) != 0) {
        strncpy(shown_, buf, sizeof(shown_) - 1);
        shown_[sizeof(shown_) - 1] = '\0';
        panel.show_text(buf);
        state_set("panel.text", buf);
        ESP_LOGI(TAG, "panel <- '%s'", buf);
    }
#else
    (void)dt_ms;
#endif
}
