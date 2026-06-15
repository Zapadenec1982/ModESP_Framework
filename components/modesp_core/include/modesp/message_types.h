/**
 * @file message_types.h
 * @brief Базові системні повідомлення ModESP v4
 *
 * Тільки core/system повідомлення. Драйвери та модулі
 * визначають свої повідомлення у власних заголовках.
 */

#pragma once

#include "modesp/types.h"
#include "etl/message.h"
#include "etl/string.h"

namespace modesp {

// ── Системні повідомлення ──

struct MsgSystemInit : etl::message<msg_id::SYSTEM_INIT> {};

struct MsgSystemShutdown : etl::message<msg_id::SYSTEM_SHUTDOWN> {
    etl::string<32> reason;
};

struct MsgStateChanged : etl::message<msg_id::STATE_CHANGED> {
    StateKey key;
};

struct MsgTimerTick : etl::message<msg_id::TIMER_TICK> {
    uint32_t uptime_sec;
};

// UI-банер: будь-який модуль (зокрема бізнес-логіка) свідомо публікує, коли хоче
// показати щось на дисплеї. DisplayModule підписаний ВИКЛЮЧНО на цей id (ADR-001).
// level: 0=INFO, 1=WARN, 2=ALARM. ttl_ms 0 = тримати до явного зняття.
struct MsgUiNotice : etl::message<msg_id::UI_NOTICE> {
    uint8_t         level  = 0;
    uint16_t        ttl_ms = 0;
    etl::string<48> text;
};

} // namespace modesp
