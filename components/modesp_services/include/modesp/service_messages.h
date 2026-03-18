/**
 * @file service_messages.h
 * @brief Повідомлення системних сервісів (Phase 2)
 *
 * Розширює базові message_types.h повідомленнями для:
 * - ErrorService (помилки, Safe Mode)
 * - WatchdogService (таймаути модулів)
 * - LoggerService (записи логів)
 */

#pragma once

#include "modesp/types.h"
#include "etl/message.h"
#include "etl/string.h"

namespace modesp {

// ═══════════════════════════════════════════════════════════════
// Error severity levels
// ═══════════════════════════════════════════════════════════════

enum class ErrorSeverity : uint8_t {
    INFO     = 0,   // Тільки лог
    WARNING  = 1,   // Лог + повідомлення на шину
    ERROR    = 2,   // Лог + деградований режим
    CRITICAL = 3,   // Лог + спроба перезапуску модуля
    FATAL    = 4,   // Safe Mode
};

// ═══════════════════════════════════════════════════════════════
// Error record (зберігається в ErrorService history)
// ═══════════════════════════════════════════════════════════════

struct ErrorRecord {
    uint32_t timestamp_ms;                  // Час виникнення
    etl::string<MODESP_MAX_MODULE_NAME_LENGTH> source;  // Ім'я модуля
    int32_t code;                           // Код помилки
    ErrorSeverity severity;
    etl::string<48> description;            // Опис
};

// ═══════════════════════════════════════════════════════════════
// Log levels
// ═══════════════════════════════════════════════════════════════

enum class LogLevel : uint8_t {
    DEBUG    = 0,
    INFO     = 1,
    WARNING  = 2,
    ERROR    = 3,
    CRITICAL = 4,
};

// ═══════════════════════════════════════════════════════════════
// Log entry (зберігається в LoggerService ring buffer)
// ═══════════════════════════════════════════════════════════════

struct LogEntry {
    uint32_t timestamp_ms;
    LogLevel level;
    etl::string<MODESP_MAX_MODULE_NAME_LENGTH> source;
    etl::string<64> message;
};

// ═══════════════════════════════════════════════════════════════
// Service messages (msg_id 50-99)
// ═══════════════════════════════════════════════════════════════

// ErrorService → шина: сталася помилка
struct MsgSystemError : etl::message<msg_id::SYSTEM_ERROR> {
    etl::string<MODESP_MAX_MODULE_NAME_LENGTH> source;
    int32_t code;
    ErrorSeverity severity;
    etl::string<48> description;
};

// ErrorService → шина: увімкнено Safe Mode
struct MsgSafeMode : etl::message<msg_id::SYSTEM_SAFE_MODE> {
    etl::string<48> reason;
};

// WatchdogService → шина: модуль не відповідає
struct MsgModuleTimeout : etl::message<msg_id::MODULE_TIMEOUT> {
    etl::string<MODESP_MAX_MODULE_NAME_LENGTH> module_name;
    uint32_t last_seen_ms;
};

// LoggerService → шина: новий запис (для WebSocket та інших)
struct MsgLogEntry : etl::message<msg_id::LOG_ENTRY> {
    LogLevel level;
    etl::string<MODESP_MAX_MODULE_NAME_LENGTH> source;
    etl::string<64> message;
};

} // namespace modesp
