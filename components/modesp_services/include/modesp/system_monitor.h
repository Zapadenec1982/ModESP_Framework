/**
 * @file system_monitor.h
 * @brief Моніторинг системних ресурсів: heap, uptime, boot reason
 *
 * Періодично публікує стан системи в SharedState.
 * Перевіряє порогові значення heap та звертається до ErrorService.
 * Фіксує причину перезавантаження при старті.
 */

#pragma once

#include "modesp/base_module.h"

namespace modesp {

class ErrorService;  // Forward declaration

class SystemMonitor : public BaseModule {
public:
    explicit SystemMonitor(ErrorService& error_service);

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_stop() override;

    // Порогові значення heap (bytes)
    uint32_t heap_warning_threshold  = 20000;
    uint32_t heap_critical_threshold = 10000;

    // Інтервал оновлення стану (ms)
    uint32_t report_interval_ms = 5000;

private:
    ErrorService& error_service_;
    uint32_t elapsed_ms_ = 0;
    uint32_t min_free_heap_ = UINT32_MAX;
    bool heap_warning_sent_ = false;
    bool heap_critical_sent_ = false;
};

} // namespace modesp
