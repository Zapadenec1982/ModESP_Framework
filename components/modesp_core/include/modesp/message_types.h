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

} // namespace modesp
