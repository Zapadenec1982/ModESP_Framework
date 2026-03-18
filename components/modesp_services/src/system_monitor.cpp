/**
 * @file system_monitor.cpp
 * @brief Реалізація SystemMonitor — heap, uptime, boot reason
 */

#include "modesp/system_monitor.h"
#include "modesp/error_service.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <ctime>

static const char* TAG = "SystemMonitor";

namespace modesp {

SystemMonitor::SystemMonitor(ErrorService& error_service)
    : BaseModule("sys_monitor", ModulePriority::LOW)
    , error_service_(error_service)
{}

bool SystemMonitor::on_init() {
    // Boot reason tracking
    esp_reset_reason_t reason = esp_reset_reason();
    state_set("system.reset_reason", static_cast<int32_t>(reason));

    const char* reason_str = "unknown";
    switch (reason) {
        case ESP_RST_POWERON:  reason_str = "power_on";   break;
        case ESP_RST_SW:       reason_str = "software";    break;
        case ESP_RST_PANIC:    reason_str = "panic";       break;
        case ESP_RST_INT_WDT:  reason_str = "int_wdt";    break;
        case ESP_RST_TASK_WDT: reason_str = "task_wdt";   break;
        case ESP_RST_WDT:      reason_str = "wdt";        break;
        case ESP_RST_BROWNOUT: reason_str = "brownout";    break;
        case ESP_RST_DEEPSLEEP:reason_str = "deep_sleep";  break;
        default: break;
    }
    state_set("system.boot_reason", reason_str);

    ESP_LOGI(TAG, "Boot reason: %s (%d)", reason_str, (int)reason);

    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT || reason == ESP_RST_INT_WDT) {
        error_service_.report("system", static_cast<int32_t>(reason),
                             ErrorSeverity::WARNING, "Watchdog reset detected");
    }
    if (reason == ESP_RST_BROWNOUT) {
        error_service_.report("system", static_cast<int32_t>(reason),
                             ErrorSeverity::WARNING, "Brownout reset — check power");
    }
    if (reason == ESP_RST_PANIC) {
        error_service_.report("system", static_cast<int32_t>(reason),
                             ErrorSeverity::ERROR, "Panic reset — check logs");
    }

    uint32_t free_heap = esp_get_free_heap_size();
    min_free_heap_ = free_heap;
    state_set("system.heap_free", static_cast<int32_t>(free_heap));
    state_set("system.heap_min", static_cast<int32_t>(min_free_heap_));

    ESP_LOGI(TAG, "SystemMonitor initialized. Free heap: %lu bytes", free_heap);
    return true;
}

void SystemMonitor::on_update(uint32_t dt_ms) {
    elapsed_ms_ += dt_ms;
    if (elapsed_ms_ < report_interval_ms) return;
    elapsed_ms_ = 0;

    uint32_t free_heap = esp_get_free_heap_size();
    if (free_heap < min_free_heap_) {
        min_free_heap_ = free_heap;
    }

    uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    // track_change=false: діагностичні ключі не тригерять WS delta
    state_set("system.heap_free", static_cast<int32_t>(free_heap), false);
    state_set("system.heap_min", static_cast<int32_t>(min_free_heap_), false);
    state_set("system.heap_largest", static_cast<int32_t>(largest_block), false);

    if (free_heap < heap_critical_threshold && !heap_critical_sent_) {
        heap_critical_sent_ = true;
        error_service_.report("sys_monitor", -100,
                             ErrorSeverity::CRITICAL, "Heap critically low!");
        ESP_LOGE(TAG, "CRITICAL: Free heap %lu < %lu", free_heap, heap_critical_threshold);
    } else if (free_heap < heap_warning_threshold && !heap_warning_sent_) {
        heap_warning_sent_ = true;
        error_service_.report("sys_monitor", -101,
                             ErrorSeverity::WARNING, "Heap low");
        ESP_LOGW(TAG, "WARNING: Free heap %lu < %lu", free_heap, heap_warning_threshold);
    }

    if (free_heap > heap_warning_threshold) {
        heap_warning_sent_ = false;
        heap_critical_sent_ = false;
    }

    // Публікація поточного часу
    time_t now = time(nullptr);
    if (now > 1700000000) {  // Час валідний (після 2023)
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        char time_str[9];   // "HH:MM:SS"
        strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
        state_set("system.time", time_str, false);

        char date_str[11];  // "DD.MM.YYYY"
        strftime(date_str, sizeof(date_str), "%d.%m.%Y", &timeinfo);
        state_set("system.date", date_str, false);
    }
}

void SystemMonitor::on_stop() {
    ESP_LOGI(TAG, "SystemMonitor stopped. Min free heap: %lu bytes", min_free_heap_);
}

} // namespace modesp
