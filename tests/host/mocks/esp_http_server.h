/**
 * @file esp_http_server.h
 * @brief HOST BUILD: Minimal esp_http_server stubs for DataLogger compilation.
 *
 * DataLogger's serialize_log_chunked() uses httpd_req_t* and httpd_resp_send_chunk().
 * These stubs allow compilation without ESP-IDF HTTP server.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include "hal_types_mock.h"  // esp_err_t, ESP_OK, ESP_FAIL

/// Мінімальний httpd_req_t stub
typedef struct {
    void* user_ctx;
} httpd_req_t;

/// Stub: httpd_resp_send_chunk — в тестах просто ігноруємо
inline esp_err_t httpd_resp_send_chunk(httpd_req_t* req, const char* buf, int len) {
    (void)req; (void)buf; (void)len;
    return ESP_OK;
}
