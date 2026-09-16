/**
 * @file can_remote_cache.h
 * @brief CanRemoteCache — кеш значень/liveness віддалених CAN-вузлів.
 *
 * Дзеркало BleCentral для транспорту CAN: драйвери-мости (hardware_type "can")
 * підписуються на (node, key_id) на етапі factory і читають кеш у
 * ISensorDriver::read(); CanService оновлює кеш із main-loop task при
 * розборі STATE/HEARTBEAT-кадрів. Фіксовані пули, zero-heap.
 *
 * Потоковість: УСІ виклики — з одного таска (main loop: factory на боті,
 * далі on_update сервісу і драйверів) — синхронізація не потрібна. Час
 * ін'єктується параметром now_us (host-тести без esp_timer).
 *
 * Шарування: це міст між modesp_can (володіє TWAI-нодою) і шаром драйверів.
 * Драйвер залежить від modesp_can; modesp_can НЕ залежить від драйверів.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "modesp/can/can_codec.h"

namespace modesp {

/// Кешоване значення одного віддаленого ключа (одна підписка).
struct CanReading {
    can::ValType type    = can::ValType::F32;
    bool         valid   = false;   // VALID-біт останнього кадру
    float        f32     = 0.0f;
    int32_t      i32     = 0;
    bool         b       = false;
    int64_t      last_us = 0;       // час останнього STATE-кадру; 0 = не бачили
};

class CanRemoteCache {
public:
    static constexpr size_t MAX_NODES         = 16;  // = MAX_REMOTE_DEVICES
    static constexpr size_t MAX_SUBSCRIPTIONS = 16;

    static CanRemoteCache& instance();

    // ── Driver-side (factory на боті) ──

    /// Підписатися на ключ key_id вузла node. Ідемпотентно для тієї самої пари.
    /// Повертає кеш для читання щоциклу або nullptr якщо пул вичерпано.
    const CanReading* subscribe(uint8_t node, uint16_t key_id);

    /// true якщо вузол живий (heartbeat/кадри свіжіші за таймаут). Вузол,
    /// якого ніколи не бачили — offline.
    bool node_online(uint8_t node) const;

    // ── Service-side (main loop) ──

    /// Зареєструвати вузол для трекінгу liveness (з remote_devices реєстру).
    void register_node(uint8_t node);

    /// STATE-кадр від вузла: оновити кеш підписки (no-op без підписки) +
    /// освіжити liveness вузла.
    void on_state_frame(uint8_t node, uint16_t key_id,
                        const can::StatePayload& p, int64_t now_us);

    /// HEARTBEAT від вузла: освіжити liveness (незнайомий вузол трекається
    /// до заповнення пулу — заготовка discovery).
    void on_heartbeat(uint8_t node, uint8_t flags, int64_t now_us);

    /// Періодичний тік liveness: вузли без кадрів довше за timeout_us стають
    /// offline, а їх підписки — invalid. Повертає true якщо хоч один вузол
    /// змінив online-статус (сервіс тоді оновлює remote.<id>.online).
    bool tick(int64_t now_us, int64_t timeout_us);

    // ── Інтроспекція (сервіс публікує стан, webui/логи) ──

    struct NodeSlot {
        uint8_t node       = 0;
        bool    online     = false;
        bool    registered = false;  // з remote_devices (false = лише чутий по шині)
        uint8_t hb_flags   = 0;      // прапори останнього heartbeat (safe_mode тощо)
        int64_t last_us    = 0;      // останній кадр будь-якого виду
    };

    size_t          node_count() const { return node_count_; }
    const NodeSlot* node_at(size_t i) const { return i < node_count_ ? &nodes_[i] : nullptr; }

    /// Скинути все (host-тести).
    void clear();

private:
    struct Sub {
        uint8_t    node   = 0;
        uint16_t   key_id = 0;
        bool       used   = false;
        CanReading r;
    };

    NodeSlot* find_node(uint8_t node);
    const NodeSlot* find_node(uint8_t node) const;
    NodeSlot* touch_node(uint8_t node, int64_t now_us);

    NodeSlot nodes_[MAX_NODES] = {};
    size_t   node_count_ = 0;
    Sub      subs_[MAX_SUBSCRIPTIONS] = {};
    size_t   sub_count_ = 0;
};

} // namespace modesp
