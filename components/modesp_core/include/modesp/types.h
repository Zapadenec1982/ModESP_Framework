/**
 * @file types.h
 * @brief Базові типи ModESP v4
 *
 * Визначає фундаментальні типи, що використовуються всіма компонентами:
 * - StateKey:   ключ для SharedState (etl::string<24>)
 * - StateValue: значення (etl::variant)
 * - msg_id:     діапазони ID повідомлень
 * - ModulePriority: пріоритети модулів
 *
 * Zero heap allocation: все на стеку з фіксованими розмірами.
 */

#pragma once

#include "etl/string.h"
#include "etl/variant.h"
#include "etl/optional.h"
#include "etl/message.h"

namespace modesp {
// ═══════════════════════════════════════════════════════════════
// Версія прошивки
// ═══════════════════════════════════════════════════════════════

constexpr const char* FIRMWARE_VERSION = "4.0.0";
constexpr const char* BUILD_DATE = __DATE__ " " __TIME__;

// ═══════════════════════════════════════════════════════════════
// Compile-time розміри (можна перевизначити через Kconfig)
// ═══════════════════════════════════════════════════════════════

#ifndef MODESP_MAX_KEY_LENGTH
#define MODESP_MAX_KEY_LENGTH 32   // Найдовший ключ: defrost.consecutive_timeouts (28 chars)
#endif

#ifndef MODESP_MAX_STRING_VALUE_LENGTH
#define MODESP_MAX_STRING_VALUE_LENGTH 32
#endif

#ifndef MODESP_MAX_MODULE_NAME_LENGTH
#define MODESP_MAX_MODULE_NAME_LENGTH 16
#endif

// ═══════════════════════════════════════════════════════════════
// StateKey — ключ для SharedState
// ═══════════════════════════════════════════════════════════════

using StateKey = etl::string<MODESP_MAX_KEY_LENGTH>;

// ═══════════════════════════════════════════════════════════════
// StateValue — значення в SharedState
// ═══════════════════════════════════════════════════════════════
// Підтримувані типи:
//   int32_t         — лічильники, коди, enum
//   float           — температура, тиск, напруга
//   bool            — стани (on/off, open/closed)
//   etl::string<32> — короткі текстові значення

using StringValue = etl::string<MODESP_MAX_STRING_VALUE_LENGTH>;
using StateValue  = etl::variant<int32_t, float, bool, StringValue>;

// ═══════════════════════════════════════════════════════════════
// ModulePriority — визначає порядок init/update/shutdown
// ═══════════════════════════════════════════════════════════════

enum class ModulePriority : uint8_t {
    CRITICAL = 0,   // Error service, watchdog — першими стартують, останніми зупиняються
    HIGH     = 1,   // Сенсори, актуатори, конфігурація
    NORMAL   = 2,   // Бізнес-логіка (thermostat, alarm)
    LOW      = 3,   // Логування, моніторинг, persistence
};

// ═══════════════════════════════════════════════════════════════
// Message ID діапазони
// ═══════════════════════════════════════════════════════════════
// Системні:    0-49
// Сервіси:    50-99
// HAL:       100-109
// Драйвери:  110-149
// Модулі:    150-249

namespace msg_id {
    // ── Системні (core) ──
    constexpr etl::message_id_t SYSTEM_INIT       = 0;
    constexpr etl::message_id_t SYSTEM_SHUTDOWN    = 1;
    constexpr etl::message_id_t SYSTEM_ERROR       = 2;
    constexpr etl::message_id_t SYSTEM_HEALTH      = 3;
    constexpr etl::message_id_t STATE_CHANGED      = 4;
    constexpr etl::message_id_t CONFIG_CHANGED     = 5;
    constexpr etl::message_id_t TIMER_TICK         = 6;
    constexpr etl::message_id_t SYSTEM_SAFE_MODE   = 7;

    // ── Сервіси ──
    constexpr etl::message_id_t MODULE_TIMEOUT     = 50;
    constexpr etl::message_id_t CONFIG_LOADED      = 51;
    constexpr etl::message_id_t CONFIG_SAVED       = 52;
    constexpr etl::message_id_t CONFIG_RESET       = 53;
    constexpr etl::message_id_t LOG_ENTRY          = 54;
    constexpr etl::message_id_t PERSIST_SAVED      = 55;

    // ── MQTT ──
    constexpr etl::message_id_t MQTT_CONNECTED    = 56;
    constexpr etl::message_id_t MQTT_DISCONNECTED = 57;
    constexpr etl::message_id_t MQTT_ERROR        = 58;

    // ── HAL ──
    constexpr etl::message_id_t GPIO_CHANGED       = 100;

    // ── Драйвери ──
    constexpr etl::message_id_t SENSOR_READING     = 110;
    constexpr etl::message_id_t SENSOR_ERROR       = 111;
    constexpr etl::message_id_t ACTUATOR_COMMAND   = 120;
    constexpr etl::message_id_t ACTUATOR_FEEDBACK  = 121;

    // ── Модулі ──
    constexpr etl::message_id_t ALARM_TRIGGERED    = 150;
    constexpr etl::message_id_t ALARM_CLEARED      = 151;
    constexpr etl::message_id_t SETPOINT_CHANGED   = 160;
    constexpr etl::message_id_t DEFROST_START      = 170;
    constexpr etl::message_id_t DEFROST_END        = 171;
} // namespace msg_id

// ═══════════════════════════════════════════════════════════════
// Спільні константи таймерів
// ═══════════════════════════════════════════════════════════════

/// Початкове значення таймера — "достатньо часу пройшло, дозволяємо дію".
/// Використовується для anti-short-cycle, min_on/off, relay min_switch.
static constexpr uint32_t TIMER_SATISFIED = 999999;

} // namespace modesp
