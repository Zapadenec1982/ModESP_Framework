/**
 * @file esp_log_mock.h
 * @brief HOST BUILD: ESP logging stubs → printf.
 */
#pragma once

#ifndef ESP_LOG_MOCK_H
#define ESP_LOG_MOCK_H

#include <cstdio>

// Suppress unused-variable warnings for TAG
#define ESP_LOGI(tag, fmt, ...) do { (void)(tag); printf("[I] " fmt "\n", ##__VA_ARGS__); } while(0)
#define ESP_LOGW(tag, fmt, ...) do { (void)(tag); printf("[W] " fmt "\n", ##__VA_ARGS__); } while(0)
#define ESP_LOGE(tag, fmt, ...) do { (void)(tag); printf("[E] " fmt "\n", ##__VA_ARGS__); } while(0)
#define ESP_LOGD(tag, fmt, ...) do { (void)(tag); } while(0)
#define ESP_LOGV(tag, fmt, ...) do { (void)(tag); } while(0)

// esp_log_level_t stub (referenced in some headers)
typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE
} esp_log_level_t;

#endif // ESP_LOG_MOCK_H
