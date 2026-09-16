/**
 * @file can_messages.h
 * @brief MsgRemoteEvent — конверт транзитної події ModESP-CAN (ADR-004).
 *
 * Симетричний в обидва боки:
 *   node == 0 → запит НАДІСЛАТИ подію на шину CAN: модуль публікує його в
 *               локальну шину, CanService ловить у on_message і кодує в
 *               EVENT-кадр (якщо event_id оголошений у can_events.out);
 *   node != 0 → подія ПРИЙНЯТА від вузла node: CanService публікує його в
 *               локальну шину з main-loop task; споживачі фільтрують за event_id.
 *
 * event_id — стабільний 16-бітний ID з generated/can_registry.h
 * (djb2-fold16 від "<module>.<event>" або явний can_id з маніфеста).
 *
 * Header навмисно НЕ гейтиться CONFIG_MODESP_CAN_ENABLE: модуль може публікувати
 * події безумовно — без компонента вони просто нікому не потрібні (zero-cost).
 */

#pragma once

#include "modesp/types.h"
#include "etl/message.h"

namespace modesp {

struct MsgRemoteEvent : etl::message<msg_id::CAN_REMOTE_EVENT> {
    uint8_t  node     = 0;    // 0 = надіслати; інакше — вузол-джерело
    uint16_t event_id = 0;    // стабільний ID з can_registry.h
    uint8_t  len      = 0;    // 0..8 байт корисного навантаження
    uint8_t  data[8]  = {};
};

} // namespace modesp
