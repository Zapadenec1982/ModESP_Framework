/**
 * @file nvs_helper.cpp
 * @brief NVS read/write helper implementations
 */

#include "modesp/services/nvs_helper.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include <cstring>

static const char* TAG = "NVS";

namespace modesp {
namespace nvs_helper {

bool init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "NVS initialized");
    return true;
}

bool read_str(const char* ns, const char* key, char* out, size_t max_len) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    size_t len = max_len;
    err = nvs_get_str(handle, key, out, &len);
    nvs_close(handle);
    return err == ESP_OK;
}

bool write_str(const char* ns, const char* key, const char* value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open '%s' failed: %s", ns, esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS write '%s.%s' failed: %s", ns, key, esp_err_to_name(err));
    }
    return err == ESP_OK;
}

bool read_float(const char* ns, const char* key, float& out) {
    // NVS has no native float — store as blob of 4 bytes
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    size_t len = sizeof(float);
    err = nvs_get_blob(handle, key, &out, &len);
    nvs_close(handle);
    return err == ESP_OK && len == sizeof(float);
}

bool write_float(const char* ns, const char* key, float value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_set_blob(handle, key, &value, sizeof(float));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

bool read_i32(const char* ns, const char* key, int32_t& out) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    err = nvs_get_i32(handle, key, &out);
    nvs_close(handle);
    return err == ESP_OK;
}

bool write_i32(const char* ns, const char* key, int32_t value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_set_i32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

bool read_bool(const char* ns, const char* key, bool& out) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    uint8_t val = 0;
    err = nvs_get_u8(handle, key, &val);
    nvs_close(handle);
    if (err == ESP_OK) {
        out = (val != 0);
    }
    return err == ESP_OK;
}

bool write_bool(const char* ns, const char* key, bool value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_set_u8(handle, key, value ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

bool erase_key(const char* ns, const char* key) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_erase_key(handle, key);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    // ESP_ERR_NVS_NOT_FOUND — ключ вже не існує, не помилка
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

// --- Batch API: один open/close для множинних операцій ---

nvs_handle_t batch_open(const char* ns, bool readonly) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(ns, readonly ? NVS_READONLY : NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS batch_open '%s' failed: %s", ns, esp_err_to_name(err));
        return 0;
    }
    return handle;
}

void batch_close(nvs_handle_t handle) {
    if (!handle) return;
    esp_err_t err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS batch commit failed: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}

bool batch_read_float(nvs_handle_t handle, const char* key, float& out) {
    if (!handle) return false;
    size_t len = sizeof(float);
    return nvs_get_blob(handle, key, &out, &len) == ESP_OK && len == sizeof(float);
}

bool batch_read_i32(nvs_handle_t handle, const char* key, int32_t& out) {
    if (!handle) return false;
    return nvs_get_i32(handle, key, &out) == ESP_OK;
}

bool batch_read_bool(nvs_handle_t handle, const char* key, bool& out) {
    if (!handle) return false;
    uint8_t val = 0;
    esp_err_t err = nvs_get_u8(handle, key, &val);
    if (err == ESP_OK) out = (val != 0);
    return err == ESP_OK;
}

bool batch_write_float(nvs_handle_t handle, const char* key, float value) {
    if (!handle) return false;
    return nvs_set_blob(handle, key, &value, sizeof(float)) == ESP_OK;
}

bool batch_write_i32(nvs_handle_t handle, const char* key, int32_t value) {
    if (!handle) return false;
    return nvs_set_i32(handle, key, value) == ESP_OK;
}

bool batch_write_bool(nvs_handle_t handle, const char* key, bool value) {
    if (!handle) return false;
    return nvs_set_u8(handle, key, value ? 1 : 0) == ESP_OK;
}

bool batch_erase_key(nvs_handle_t handle, const char* key) {
    if (!handle) return false;
    esp_err_t err = nvs_erase_key(handle, key);
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

bool read_blob(const char* ns, const char* key, void* out, size_t max_len, size_t& out_len) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    size_t len = max_len;
    err = nvs_get_blob(handle, key, out, &len);
    nvs_close(handle);

    if (err == ESP_OK) {
        out_len = len;
        return true;
    }
    return false;
}

bool write_blob(const char* ns, const char* key, const void* data, size_t len) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open '%s' failed: %s", ns, esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(handle, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS write blob '%s.%s' (%u bytes) failed: %s",
                 ns, key, (unsigned)len, esp_err_to_name(err));
    }
    return err == ESP_OK;
}

} // namespace nvs_helper
} // namespace modesp
