/**
 * @file ota_handler.h
 * @brief Cloud OTA handler — download firmware via HTTP, flash, reboot
 *
 * Receives OTA command from cloud via MQTT (cmd/_ota with JSON payload),
 * downloads firmware binary over HTTP, validates (magic byte, board match,
 * SHA256 checksum), writes to OTA partition, and restarts.
 *
 * Runs in a separate FreeRTOS task to avoid blocking MQTT event handler.
 * Only one OTA operation at a time (atomic guard).
 *
 * Status feedback via SharedState:
 *   _ota.status   — "idle" | "downloading" | "verifying" | "rebooting" | "error"
 *   _ota.progress — 0..100 (download percentage)
 *   _ota.error    — error description or ""
 */

#pragma once

namespace modesp {

class SharedState;

namespace ota_handler {

/// Parameters extracted from _ota JSON payload
struct OtaParams {
    char url[256];       ///< HTTP URL to firmware binary
    char version[32];    ///< Target firmware version (e.g., "1.2.3")
    char checksum[80];   ///< SHA256 checksum ("sha256:<64 hex chars>")
};

/**
 * Start OTA download+flash in a separate FreeRTOS task.
 *
 * @param params  OTA parameters (url, version, checksum)
 * @param state   SharedState pointer for status feedback
 * @return true if task was started, false if OTA already in progress
 */
bool start_ota(const OtaParams& params, SharedState* state);

/**
 * Check if OTA is currently in progress.
 */
bool is_in_progress();

} // namespace ota_handler
} // namespace modesp
