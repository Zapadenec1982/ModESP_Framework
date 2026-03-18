/**
 * @file nvs_helper.h
 * @brief Simple NVS read/write helpers
 *
 * Thin wrappers around ESP-IDF NVS API.
 * Each function opens a handle, performs the operation, and closes it.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "nvs.h"

namespace modesp {
namespace nvs_helper {

/// Initialize NVS flash. Call once at boot before any NVS operations.
bool init();

/// Read a string value. Returns false if key not found or error.
bool read_str(const char* ns, const char* key, char* out, size_t max_len);

/// Write a string value. Returns false on error.
bool write_str(const char* ns, const char* key, const char* value);

/// Read a float value. Returns false if key not found or error.
bool read_float(const char* ns, const char* key, float& out);

/// Write a float value. Returns false on error.
bool write_float(const char* ns, const char* key, float value);

/// Read an int32_t value. Returns false if key not found or error.
bool read_i32(const char* ns, const char* key, int32_t& out);

/// Write an int32_t value. Returns false on error.
bool write_i32(const char* ns, const char* key, int32_t value);

/// Read a bool value (stored as uint8_t). Returns false if key not found or error.
bool read_bool(const char* ns, const char* key, bool& out);

/// Write a bool value (stored as uint8_t). Returns false on error.
bool write_bool(const char* ns, const char* key, bool value);

/// Erase a single key from NVS. Returns false on error.
bool erase_key(const char* ns, const char* key);

/// Read a blob (binary data) from NVS. Returns actual size in out_len. Returns false if not found.
bool read_blob(const char* ns, const char* key, void* out, size_t max_len, size_t& out_len);

/// Write a blob (binary data) to NVS. Returns false on error.
bool write_blob(const char* ns, const char* key, const void* data, size_t len);

// --- Batch API: один open/close для множинних операцій ---

/// Відкрити NVS handle для batch операцій. Повертає 0 при помилці.
nvs_handle_t batch_open(const char* ns, bool readonly);

/// Закрити handle з commit (для write) або без (readonly).
void batch_close(nvs_handle_t handle);

/// Batch read/write — працюють з відкритим handle
bool batch_read_float(nvs_handle_t handle, const char* key, float& out);
bool batch_read_i32(nvs_handle_t handle, const char* key, int32_t& out);
bool batch_read_bool(nvs_handle_t handle, const char* key, bool& out);
bool batch_write_float(nvs_handle_t handle, const char* key, float value);
bool batch_write_i32(nvs_handle_t handle, const char* key, int32_t value);
bool batch_write_bool(nvs_handle_t handle, const char* key, bool value);
bool batch_erase_key(nvs_handle_t handle, const char* key);

} // namespace nvs_helper
} // namespace modesp
