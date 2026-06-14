/**
 * @file scenario_persist.cpp
 * @brief Off-loop NVS persistence backend for the scenario engine.
 *
 * See scenario_persist.h for the design rationale (single-writer FIFO,
 * Deferred vs CrashCritical, crash-recovery correctness).
 */

#include "scenario_persist.h"

#include "modesp/scenario/nvs_token.h"     // SEQ_TOKEN_SIZE
#include "modesp/services/nvs_helper.h"    // write_blob / read_blob

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include <cstdio>
#include <cstring>

namespace modesp::scenario_persist {
namespace {

using modesp::scenario::SEQ_TOKEN_SIZE;

constexpr const char* TAG = "scn_persist";
constexpr const char* NVS_NS = "scnstate";

// Queue holds one in-flight token per slot's worth of headroom.
constexpr UBaseType_t QUEUE_DEPTH = 8;

// Drain task: low priority so it never preempts the control loop. Equal to the
// ESP-IDF main task priority (1) — when a producer blocks (crash-critical flush
// handshake) it yields and the drain task runs; it never interrupts a tick
// mid-execution. ~4 KB stack covers nvs_open/nvs_set_blob/nvs_commit.
constexpr uint32_t   DRAIN_TASK_STACK = 4096;
constexpr UBaseType_t DRAIN_TASK_PRIO = 1;

// Crash-critical handshake budgets. Generous — the normal path completes in
// (queue backlog + 1) flash writes; timeouts only fire if the drain task is
// pathologically stuck, in which case we fall back to a direct write rather
// than silently lose a crash-critical token.
constexpr TickType_t SEND_TIMEOUT  = pdMS_TO_TICKS(1000);
constexpr TickType_t FLUSH_TIMEOUT = pdMS_TO_TICKS(3000);

struct Item {
    uint8_t  slot;
    uint16_t len;
    bool     flush;                      // true → signal flush_done after commit
    uint8_t  token[SEQ_TOKEN_SIZE];
};

QueueHandle_t     queue_       = nullptr;
SemaphoreHandle_t flush_mutex_ = nullptr;  // serialises crash-critical flushes
SemaphoreHandle_t flush_done_  = nullptr;  // drain → producer completion signal
volatile bool     flush_result_ = false;   // result of the last flushed item
bool              async_enabled_ = false;

void make_key(uint8_t slot, char* out, size_t out_sz) {
    std::snprintf(out, out_sz, "t%u", static_cast<unsigned>(slot));
}

// The one and only place that touches NVS for the scenario namespace (except
// the degraded sync fallback, which only runs when the queue/task don't exist).
bool do_nvs_write(uint8_t slot, const uint8_t* token, size_t len) {
    char key[8];
    make_key(slot, key, sizeof(key));
    return modesp::nvs_helper::write_blob(NVS_NS, key, token, len);
}

void drain_task(void*) {
    Item it;
    for (;;) {
        if (xQueueReceive(queue_, &it, portMAX_DELAY) != pdTRUE) continue;
        bool ok = do_nvs_write(it.slot, it.token, it.len);
        if (!ok) {
            ESP_LOGW(TAG, "deferred NVS write failed (slot %u)",
                     static_cast<unsigned>(it.slot));
        }
        if (it.flush) {
            flush_result_ = ok;
            xSemaphoreGive(flush_done_);   // wake the blocked crash-critical producer
        }
    }
}

bool enqueue_deferred(const Item& it) {
    // Non-blocking: phase-change writes must never stall the tick. If the queue
    // is full the drain task is lagging — drop this snapshot (the next phase
    // change supersedes it; recovery lands at most one phase stale). Logged, not
    // silent.
    if (xQueueSend(queue_, &it, 0) != pdTRUE) {
        ESP_LOGW(TAG, "persist queue full — dropped deferred token (slot %u)",
                 static_cast<unsigned>(it.slot));
        return false;
    }
    return true;
}

bool flush_crash_critical(const Item& base) {
    // Serialise crash-critical flushes so only one producer waits on flush_done_
    // at a time (started fires on the HTTP task, terminal on the tick task).
    xSemaphoreTake(flush_mutex_, portMAX_DELAY);

    // Clear any stale completion signal before arming this handshake.
    xSemaphoreTake(flush_done_, 0);
    flush_result_ = false;

    Item it = base;
    it.flush = true;

    bool ok;
    if (xQueueSend(queue_, &it, SEND_TIMEOUT) != pdTRUE) {
        // Queue stuck full — last resort: write directly so the crash-critical
        // token is not lost (degraded; only under a stalled drain task).
        ESP_LOGE(TAG, "persist queue stuck — direct crash-critical write (slot %u)",
                 static_cast<unsigned>(it.slot));
        ok = do_nvs_write(it.slot, it.token, it.len);
    } else if (xSemaphoreTake(flush_done_, FLUSH_TIMEOUT) == pdTRUE) {
        ok = flush_result_;                // committed by the drain task (FIFO → durable)
    } else {
        ESP_LOGE(TAG, "persist flush timeout — direct crash-critical write (slot %u)",
                 static_cast<unsigned>(it.slot));
        ok = do_nvs_write(it.slot, it.token, it.len);
    }

    xSemaphoreGive(flush_mutex_);
    return ok;
}

}  // namespace

bool init() {
    if (async_enabled_) return true;       // idempotent

    queue_       = xQueueCreate(QUEUE_DEPTH, sizeof(Item));
    flush_mutex_ = xSemaphoreCreateMutex();
    flush_done_  = xSemaphoreCreateBinary();

    BaseType_t task_ok = pdFAIL;
    if (queue_ && flush_mutex_ && flush_done_) {
        task_ok = xTaskCreate(drain_task, "scn_persist", DRAIN_TASK_STACK,
                              nullptr, DRAIN_TASK_PRIO, nullptr);
    }

    if (!queue_ || !flush_mutex_ || !flush_done_ || task_ok != pdPASS) {
        // Allocation failed — tear down partial state and degrade to synchronous
        // direct writes (correct, just back on the control loop).
        if (queue_)       { vQueueDelete(queue_);            queue_ = nullptr; }
        if (flush_mutex_) { vSemaphoreDelete(flush_mutex_);  flush_mutex_ = nullptr; }
        if (flush_done_)  { vSemaphoreDelete(flush_done_);   flush_done_ = nullptr; }
        async_enabled_ = false;
        ESP_LOGE(TAG, "init failed — falling back to synchronous NVS writes");
        return false;
    }

    async_enabled_ = true;
    ESP_LOGI(TAG, "async scenario persistence ready (queue=%u, prio=%u)",
             static_cast<unsigned>(QUEUE_DEPTH), static_cast<unsigned>(DRAIN_TASK_PRIO));
    return true;
}

bool write(void* /*user*/, uint8_t slot, const uint8_t* token, std::size_t len,
           modesp::scenario::NvsWriteMode mode) {
    if (len > SEQ_TOKEN_SIZE) return false;

    if (!async_enabled_) {
        // Degraded path: no queue/task — write synchronously regardless of mode.
        return do_nvs_write(slot, token, len);
    }

    Item it;
    it.slot  = slot;
    it.len   = static_cast<uint16_t>(len);
    it.flush = false;
    std::memcpy(it.token, token, len);

    if (mode == modesp::scenario::NvsWriteMode::CrashCritical) {
        return flush_crash_critical(it);
    }
    return enqueue_deferred(it);
}

bool read(void* /*user*/, uint8_t slot, uint8_t* buf, std::size_t* in_out_len) {
    char key[8];
    make_key(slot, key, sizeof(key));
    size_t out_len = 0;
    bool ok = modesp::nvs_helper::read_blob(NVS_NS, key, buf, *in_out_len, out_len);
    if (ok) *in_out_len = out_len;
    return ok;
}

}  // namespace modesp::scenario_persist
