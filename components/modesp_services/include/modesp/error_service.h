/**
 * @file error_service.h
 * @brief Централізована обробка помилок + Safe Mode
 *
 * Найважливіший сервіс. Збирає помилки від модулів, зберігає
 * історію останніх 16 помилок, вмикає Safe Mode при FATAL.
 *
 * Safe Mode:
 *   - Публікує MsgSafeMode на шину
 *   - Актуатори повинні вимкнутись при отриманні MsgSafeMode
 *   - Сенсори та моніторинг продовжують працювати
 *   - Очікує ручне втручання або перезавантаження
 */

#pragma once

#include "modesp/base_module.h"
#include "modesp/service_messages.h"
#include "etl/circular_buffer.h"

namespace modesp {

#ifndef MODESP_MAX_ERROR_HISTORY
#define MODESP_MAX_ERROR_HISTORY 16
#endif

class ErrorService : public BaseModule {
public:
    ErrorService();

    // ── BaseModule lifecycle ──
    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_message(const etl::imessage& msg) override;
    void on_stop() override;

    // ── API для модулів (викликається з будь-якого потоку) ──
    void report(const char* source, int32_t code,
                ErrorSeverity severity, const char* description);

    // ── Стан ──
    size_t error_count() const { return total_errors_; }
    bool is_safe_mode() const { return safe_mode_; }

    // ── Історія (для діагностики) ──
    const etl::circular_buffer<ErrorRecord, MODESP_MAX_ERROR_HISTORY>& history() const {
        return history_;
    }

private:
    void enter_safe_mode(const char* reason);

    etl::circular_buffer<ErrorRecord, MODESP_MAX_ERROR_HISTORY> history_;
    uint32_t total_errors_ = 0;
    bool safe_mode_ = false;
    uint32_t uptime_ms_ = 0;
};

} // namespace modesp
