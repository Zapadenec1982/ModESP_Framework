/**
 * @file freertos_mock.h
 * @brief HOST BUILD: FreeRTOS stubs using std::mutex.
 *
 * Replaces FreeRTOS semaphore API with C++ std::mutex.
 * Included first in every translation unit via host_prefix.h.
 */
#pragma once

// Guard against real FreeRTOS headers
#ifndef FREERTOS_MOCK_H
#define FREERTOS_MOCK_H

#include <mutex>
#include <cassert>
#include <cstdint>

// ── Semaphore → std::mutex ──
using SemaphoreHandle_t = std::mutex*;

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    return new std::mutex();
}

inline int xSemaphoreTake(SemaphoreHandle_t m, int /*timeout_ticks*/) {
    if (m) m->lock();
    return 1;  // pdTRUE
}

inline void xSemaphoreGive(SemaphoreHandle_t m) {
    if (m) m->unlock();
}

inline void vSemaphoreDelete(SemaphoreHandle_t m) {
    delete m;
}

// ── FreeRTOS macros ──
#define configASSERT(x)      assert(x)
#define pdTRUE               1
#define pdFALSE              0
#define pdMS_TO_TICKS(ms)    (ms)
#define portMAX_DELAY        0xFFFFFFFF

// ── Task handle stub (not used in host tests) ──
using TaskHandle_t = void*;

#endif // FREERTOS_MOCK_H
