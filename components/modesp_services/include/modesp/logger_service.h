/**
 * @file logger_service.h
 * @brief Ring buffer логування для ModESP
 *
 * Зберігає останні N записів логів в RAM.
 * Дублює в ESP-IDF лог (ESP_LOGx).
 * Може публікувати MsgLogEntry на шину для WebSocket.
 *
 * Не замінює ESP_LOGx — доповнює, даючи доступ до логів
 * через SharedState та шину повідомлень.
 */

#pragma once

#include "modesp/base_module.h"
#include "modesp/service_messages.h"
#include "etl/circular_buffer.h"

namespace modesp {

#ifndef MODESP_MAX_LOG_ENTRIES
#define MODESP_MAX_LOG_ENTRIES 64
#endif

class LoggerService : public BaseModule {
public:
    LoggerService();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_stop() override;

    // ── Основний API ──
    void log(LogLevel level, const char* source, const char* message);

    // ── Читання логів (для WebSocket, діагностики) ──
    using EntryCallback = void(*)(const LogEntry&, void* user_data);
    void read_recent(size_t count, EntryCallback cb, void* user_data) const;

    size_t entry_count() const { return entries_.size(); }

    // ── Налаштування ──
    LogLevel min_level = LogLevel::INFO;
    bool publish_to_bus = false;  // Публікувати MsgLogEntry на шину

private:
    etl::circular_buffer<LogEntry, MODESP_MAX_LOG_ENTRIES> entries_;
    uint32_t uptime_ms_ = 0;
    uint32_t total_logged_ = 0;
};

} // namespace modesp
