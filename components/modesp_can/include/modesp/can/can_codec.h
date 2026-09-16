/**
 * @file can_codec.h
 * @brief ModESP-CAN v1 frame codec (ADR-004) — pure header, host-testable.
 *
 * Кадровий рівень власного протоколу поверх класичного CAN 2.0B:
 *   - 29-бітний arbitration ID: prio(3) | kind(2) | src(8) | id16(16);
 *   - STATE-payload:     [0]=тип+VALID, [1..4]=значення LE (bool: 1 байт);
 *   - HEARTBEAT-payload: proto_ver, flags, registry_crc16, uptime_s;
 *   - стабільні 16-бітні ID = djb2-fold16 від імені ключа/події.
 *
 * УВАГА (wire-контракт): djb2_32/fold16/crc16_ccitt ДЗЕРКАЛЯТЬ Python-реалізацію
 * в tools/generate_ui.py (can_djb2_32/can_fold16/can_crc16_ccitt). Спільні
 * тест-вектори: tests/host/test_can_codec.cpp ↔ tools/tests/test_can_registry.py.
 * Зміна будь-якої з реалізацій ламає сумісність із прошитими вузлами.
 *
 * Жодних залежностей від ESP-IDF/FreeRTOS — компілюється на host як є.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace modesp::can {

// ═══════════════════════════════════════════════════════════════
// Константи протоколу
// ═══════════════════════════════════════════════════════════════

inline constexpr uint8_t  PROTO_VER       = 1;
inline constexpr uint16_t HEARTBEAT_MAGIC = 0x4D43;   // "MC" — id16 heartbeat-кадру
inline constexpr uint8_t  NODE_MIN        = 1;
inline constexpr uint8_t  NODE_MAX        = 253;      // 0 invalid; 254/255 зарезервовано

/// Вид кадру — біти [25:24] arbitration ID
enum class FrameKind : uint8_t {
    HEARTBEAT = 0,
    EVENT     = 1,
    STATE     = 2,
    CMD       = 3,   // зарезервовано (адресний запис rw-ключів, v1.1)
};

/// Тип значення STATE-кадру — біти 0-3 нульового байта payload
enum class ValType : uint8_t { F32 = 0, I32 = 1, BOOL = 2 };

/// Пріоритети за замовчуванням (біти [28:26], 0 = найвищий)
inline constexpr uint8_t PRIO_EVENT     = 2;
inline constexpr uint8_t PRIO_STATE     = 4;
inline constexpr uint8_t PRIO_HEARTBEAT = 6;

// ═══════════════════════════════════════════════════════════════
// 29-бітний arbitration ID
// ═══════════════════════════════════════════════════════════════

struct FrameId {
    uint8_t   prio = PRIO_STATE;         // 0..7
    FrameKind kind = FrameKind::STATE;
    uint8_t   src  = 0;                  // node-id відправника
    uint16_t  id16 = 0;                  // key-id / event-id / HEARTBEAT_MAGIC
};

inline constexpr uint32_t pack_id(const FrameId& f) {
    return (uint32_t(f.prio & 0x7u) << 26)
         | (uint32_t(uint8_t(f.kind) & 0x3u) << 24)
         | (uint32_t(f.src) << 16)
         | f.id16;
}

inline constexpr FrameId unpack_id(uint32_t raw) {
    return FrameId{
        uint8_t((raw >> 26) & 0x7u),
        FrameKind((raw >> 24) & 0x3u),
        uint8_t((raw >> 16) & 0xFFu),
        uint16_t(raw & 0xFFFFu),
    };
}

// ═══════════════════════════════════════════════════════════════
// Хеші / CRC (дзеркало tools/generate_ui.py — НЕ ЧІПАТИ без зміни обох)
// ═══════════════════════════════════════════════════════════════

/// Класичний djb2 (hash*33 + c) по байтах UTF-8 рядка.
inline uint32_t djb2_32(const char* s) {
    uint32_t h = 5381;
    while (*s) {
        h = h * 33u + uint8_t(*s++);
    }
    return h;
}

/// 16-бітний стабільний ID: xor-fold djb2_32; 0 зарезервовано → 1.
inline uint16_t fold16(const char* s) {
    uint32_t h = djb2_32(s);
    uint16_t v = uint16_t((h ^ (h >> 16)) & 0xFFFFu);
    return v ? v : uint16_t(1);
}

/// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). Вектор: "123456789" → 0x29B1.
inline uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = uint16_t(crc ^ (uint16_t(data[i]) << 8));
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
        }
    }
    return crc;
}

// ═══════════════════════════════════════════════════════════════
// STATE payload: [0] = тип (біти 0-3) | VALID (біт 6); [1..4] = значення LE
// ═══════════════════════════════════════════════════════════════

inline constexpr uint8_t STATE_VALID_BIT = 0x40;
inline constexpr uint8_t STATE_TYPE_MASK = 0x0F;

struct StatePayload {
    ValType type  = ValType::F32;
    bool    valid = false;   // false ⇔ компаньйон <key>_ok == false у джерела
    float   f32   = 0.0f;    // значуще лише поле, що відповідає type
    int32_t i32   = 0;
    bool    b     = false;
};

/// Кодує STATE-payload у out[8]; повертає DLC (2 для bool, 5 для f32/i32).
inline size_t encode_state(const StatePayload& p, uint8_t out[8]) {
    out[0] = uint8_t(uint8_t(p.type) & STATE_TYPE_MASK);
    if (p.valid) out[0] |= STATE_VALID_BIT;

    if (p.type == ValType::BOOL) {
        out[1] = p.b ? 1 : 0;
        return 2;
    }
    uint32_t raw;
    if (p.type == ValType::F32) {
        memcpy(&raw, &p.f32, sizeof(raw));
    } else {
        raw = uint32_t(p.i32);
    }
    out[1] = uint8_t(raw & 0xFF);
    out[2] = uint8_t((raw >> 8) & 0xFF);
    out[3] = uint8_t((raw >> 16) & 0xFF);
    out[4] = uint8_t((raw >> 24) & 0xFF);
    return 5;
}

/// Декодує STATE-payload; false = зіпсований кадр (короткий DLC / невідомий тип).
inline bool decode_state(const uint8_t* data, size_t dlc, StatePayload& out) {
    if (dlc < 2) return false;
    uint8_t t = uint8_t(data[0] & STATE_TYPE_MASK);
    if (t > uint8_t(ValType::BOOL)) return false;
    out.type  = ValType(t);
    out.valid = (data[0] & STATE_VALID_BIT) != 0;

    if (out.type == ValType::BOOL) {
        out.b = data[1] != 0;
        return true;
    }
    if (dlc < 5) return false;
    uint32_t raw = uint32_t(data[1])
                 | (uint32_t(data[2]) << 8)
                 | (uint32_t(data[3]) << 16)
                 | (uint32_t(data[4]) << 24);
    if (out.type == ValType::F32) {
        memcpy(&out.f32, &raw, sizeof(out.f32));
    } else {
        out.i32 = int32_t(raw);
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════
// HEARTBEAT payload: [0]=proto_ver, [1]=flags, [2..3]=registry_crc16 LE,
//                    [4..7]=uptime_s LE. Період 1 с; таймаут вузла 3.5 с.
// ═══════════════════════════════════════════════════════════════

inline constexpr uint8_t HB_FLAG_SAFE_MODE = 0x01;

struct HeartbeatPayload {
    uint8_t  proto_ver      = PROTO_VER;
    uint8_t  flags          = 0;
    uint16_t registry_crc16 = 0;
    uint32_t uptime_s       = 0;
};

inline size_t encode_heartbeat(const HeartbeatPayload& p, uint8_t out[8]) {
    out[0] = p.proto_ver;
    out[1] = p.flags;
    out[2] = uint8_t(p.registry_crc16 & 0xFF);
    out[3] = uint8_t((p.registry_crc16 >> 8) & 0xFF);
    out[4] = uint8_t(p.uptime_s & 0xFF);
    out[5] = uint8_t((p.uptime_s >> 8) & 0xFF);
    out[6] = uint8_t((p.uptime_s >> 16) & 0xFF);
    out[7] = uint8_t((p.uptime_s >> 24) & 0xFF);
    return 8;
}

inline bool decode_heartbeat(const uint8_t* data, size_t dlc, HeartbeatPayload& out) {
    if (dlc < 8) return false;
    out.proto_ver      = data[0];
    out.flags          = data[1];
    out.registry_crc16 = uint16_t(data[2]) | (uint16_t(data[3]) << 8);
    out.uptime_s       = uint32_t(data[4])
                       | (uint32_t(data[5]) << 8)
                       | (uint32_t(data[6]) << 16)
                       | (uint32_t(data[7]) << 24);
    return true;
}

} // namespace modesp::can
