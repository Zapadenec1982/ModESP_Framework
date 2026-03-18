/**
 * @file watchdog_service.cpp
 * @brief Реалізація software watchdog для модулів
 */

#include "modesp/watchdog_service.h"
#include "modesp/error_service.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "WatchdogSvc";

namespace modesp {

WatchdogService::WatchdogService(ErrorService& error_service, ModuleManager& manager)
    : BaseModule("watchdog", ModulePriority::CRITICAL)
    , error_service_(error_service)
    , module_manager_(manager)
{}

bool WatchdogService::on_init() {
    ESP_LOGI(TAG, "WatchdogService initialized (timeouts: C=%lu H=%lu N=%lu L=%lu ms)",
             timeouts.critical_ms, timeouts.high_ms,
             timeouts.normal_ms, timeouts.low_ms);
    return true;
}

void WatchdogService::on_update(uint32_t dt_ms) {
    uptime_ms_ += dt_ms;
    elapsed_ms_ += dt_ms;
    if (elapsed_ms_ < check_interval_ms) return;
    elapsed_ms_ = 0;

    // Перевіряємо модулі
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    struct CheckCtx {
        WatchdogService* self;
        uint32_t now_ms;
    };
    CheckCtx ctx = { this, now_ms };

    module_manager_.for_each([](BaseModule& module, void* user_data) {
        auto* ctx = static_cast<CheckCtx*>(user_data);
        auto* self = ctx->self;

        // Пропускаємо себе та ErrorService
        if (&module == static_cast<BaseModule*>(self)) return;
        if (module.state() != BaseModule::State::RUNNING) return;

        uint32_t timeout = self->get_timeout_for(module.priority());
        uint32_t last = module.last_update_ms();

        // На старті last_update_ms = 0, ігноруємо поки система не прогрілась
        if (last == 0 && self->uptime_ms_ < timeout) return;
        if (last == 0 && self->uptime_ms_ >= timeout) {
            // Модуль ніколи не оновлювався, але достатньо часу пройшло
        } else if (ctx->now_ms - last < timeout) {
            return;  // Все ОК
        }

        // ── Таймаут! ──
        StateKey module_key(module.name());

        // Перевіряємо лічильник перезапусків
        auto it = self->restart_counts_.find(module_key);
        if (it == self->restart_counts_.end()) {
            RestartInfo info;
            self->restart_counts_.insert({module_key, info});
            it = self->restart_counts_.find(module_key);
        }

        auto& info = it->second;

        if (info.count < MAX_RESTARTS) {
            info.count++;
            ESP_LOGW(TAG, "Module '%s' timeout! Restart %d/%d",
                     module.name(), info.count, MAX_RESTARTS);

            // Публікуємо повідомлення
            MsgModuleTimeout msg;
            msg.module_name = module.name();
            msg.last_seen_ms = last;
            self->publish(msg);

            // Реально перезапускаємо модуль
            if (self->module_manager_.restart_module(module)) {
                ESP_LOGI(TAG, "Module '%s' restarted OK", module.name());
            } else {
                ESP_LOGE(TAG, "Module '%s' restart failed", module.name());
            }

        } else if (!info.reported) {
            info.reported = true;
            ESP_LOGE(TAG, "Module '%s' exceeded max restarts (%d)",
                     module.name(), MAX_RESTARTS);
            self->error_service_.report(
                "watchdog", -200, ErrorSeverity::CRITICAL,
                "Module exceeded max restarts");
        }
    }, &ctx);
}

void WatchdogService::on_stop() {
    ESP_LOGI(TAG, "WatchdogService stopped");
}

uint32_t WatchdogService::get_timeout_for(ModulePriority priority) const {
    switch (priority) {
        case ModulePriority::CRITICAL: return timeouts.critical_ms;
        case ModulePriority::HIGH:     return timeouts.high_ms;
        case ModulePriority::NORMAL:   return timeouts.normal_ms;
        case ModulePriority::LOW:      return timeouts.low_ms;
        default:                       return timeouts.normal_ms;
    }
}

} // namespace modesp
