/**
 * @file watchdog_service.h
 * @brief Software watchdog — моніторинг heartbeat модулів
 *
 * Перевіряє що кожен модуль отримує on_update() вчасно.
 * ModuleManager оновлює last_update_ms при кожному update.
 * WatchdogService порівнює з таймаутом по пріоритету модуля.
 *
 * При таймауті:
 *   1. Публікує MsgModuleTimeout
 *   2. Спроба перезапуску модуля (до MAX_RESTARTS разів)
 *   3. Після вичерпання спроб — повідомлення ErrorService
 */

#pragma once

#include "modesp/base_module.h"
#include "modesp/module_manager.h"
#include "modesp/service_messages.h"
#include "etl/unordered_map.h"

namespace modesp {

class ErrorService;  // Forward declaration

class WatchdogService : public BaseModule {
public:
    WatchdogService(ErrorService& error_service, ModuleManager& manager);

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_stop() override;

    // Таймаути по пріоритету (ms)
    struct Timeouts {
        uint32_t critical_ms = 5000;    // 5 сек
        uint32_t high_ms     = 10000;   // 10 сек
        uint32_t normal_ms   = 30000;   // 30 сек
        uint32_t low_ms      = 60000;   // 1 хв
    };
    Timeouts timeouts;

    static constexpr uint8_t MAX_RESTARTS = 3;

    // Інтервал перевірки (ms) — не кожен update, а раз на секунду
    uint32_t check_interval_ms = 1000;

private:
    uint32_t get_timeout_for(ModulePriority priority) const;

    ErrorService& error_service_;
    ModuleManager& module_manager_;
    uint32_t elapsed_ms_ = 0;
    uint32_t uptime_ms_ = 0;

    // Лічильники перезапусків по модулях
    struct RestartInfo {
        uint8_t count = 0;
        bool reported = false;  // Чи вже повідомлено ErrorService
    };
    etl::unordered_map<StateKey, RestartInfo, MODESP_MAX_MODULES> restart_counts_;
};

} // namespace modesp
