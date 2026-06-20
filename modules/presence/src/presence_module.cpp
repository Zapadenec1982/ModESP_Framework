/**
 * @file presence_module.cpp
 * @brief Presence / occupancy logic for the LD2410b radar
 */

#include "presence_module.h"
#include "esp_log.h"

static const char* TAG = "Presence";

PresenceModule::PresenceModule()
    : BaseModule("presence", modesp::ModulePriority::NORMAL)
{}

bool PresenceModule::on_init() {
    // Seed published keys so the UI has values before the first detection.
    state_set("presence.detected", false);
    state_set("presence.state", "none");
    state_set("presence.moving_distance", 0.0f);
    state_set("presence.static_distance", 0.0f);
    state_set("presence.idle_sec", static_cast<int32_t>(0));
    state_set("presence.sensor_ok", false);

    ESP_LOGI(TAG, "Initialized (reads equipment.presence; hold=%lds)",
             (long)read_int("presence.hold_sec", 5));
    return true;
}

void PresenceModule::on_update(uint32_t dt_ms) {
    const bool enabled   = read_bool("presence.enabled", true);
    const bool sensor_ok = read_bool("equipment.presence_ok", false);

    // Raw presence (driver already applies ms-level anti-flap) + optional distance
    // channels. Absent keys → 0.0 (graceful: distance features simply inert).
    const bool  raw   = read_float("equipment.presence", 0.0f) > 0.5f;
    const float move  = read_float("equipment.move_distance", 0.0f);
    const float still = read_float("equipment.still_distance", 0.0f);

    // Zone gate: drop targets farther than max_distance. Only meaningful when a
    // distance channel is bound (otherwise we don't know the distance → no gate).
    const int max_d = read_int("presence.max_distance", 600);
    bool gated = false;
    if (max_d > 0 && (move > 0.0f || still > 0.0f)) {
        float nearest = (move > 0.0f) ? move : still;
        if (still > 0.0f && still < nearest) nearest = still;
        gated = nearest > static_cast<float>(max_d);
    }

    // sensor_ok gate: when the radar disconnects, the driver's read() goes stale
    // and EquipmentBase keeps publishing the LAST equipment.presence value. Without
    // this gate presence.detected would latch ON forever on a dead sensor.
    const bool effective = enabled && sensor_ok && raw && !gated;

    // Occupancy hold: keep detected for hold_sec after the target clears.
    const uint32_t hold_ms = static_cast<uint32_t>(read_int("presence.hold_sec", 5)) * 1000u;
    if (effective) {
        since_clear_ms_ = 0;
        detected_ = true;
    } else {
        if (since_clear_ms_ < hold_ms) since_clear_ms_ += dt_ms;
        if (since_clear_ms_ >= hold_ms) detected_ = false;
    }
    if (!enabled) { detected_ = false; since_clear_ms_ = 0; }

    // Seconds since the last positive detection (0 while present).
    if (detected_) idle_ms_ = 0;
    else if (idle_ms_ < 0xFFFFFFFFu - dt_ms) idle_ms_ += dt_ms;

    // Human-readable state token (localized in the web UI via i18n options).
    const char* st;
    if (!enabled)                         st = "disabled";
    else if (!detected_)                  st = "none";
    else if (move > 0.0f && still > 0.0f) st = "both";
    else if (move > 0.0f)                 st = "moving";
    else if (still > 0.0f)                st = "static";
    else                                  st = "present";

    state_set("presence.detected", detected_);
    state_set("presence.state", st);
    state_set("presence.moving_distance", move);
    state_set("presence.static_distance", still);
    state_set("presence.idle_sec", static_cast<int32_t>(idle_ms_ / 1000u));
    state_set("presence.sensor_ok", sensor_ok);
}
