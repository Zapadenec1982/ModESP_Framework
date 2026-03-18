/**
 * @file esp_timer_mock.h
 * @brief HOST BUILD: esp_timer stubs.
 */
#pragma once

#ifndef ESP_TIMER_MOCK_H
#define ESP_TIMER_MOCK_H

#include <cstdint>

inline int64_t esp_timer_get_time() { return 0; }

typedef struct esp_timer* esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void* arg);

#endif // ESP_TIMER_MOCK_H
