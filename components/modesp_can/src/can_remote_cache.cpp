/**
 * @file can_remote_cache.cpp
 * @brief Кеш віддалених CAN-вузлів: фіксовані пули, один таск, zero-heap.
 */

#include "modesp/can/can_remote_cache.h"

namespace modesp {

CanRemoteCache& CanRemoteCache::instance() {
    static CanRemoteCache inst;
    return inst;
}

CanRemoteCache::NodeSlot* CanRemoteCache::find_node(uint8_t node) {
    for (size_t i = 0; i < node_count_; i++) {
        if (nodes_[i].node == node) return &nodes_[i];
    }
    return nullptr;
}

const CanRemoteCache::NodeSlot* CanRemoteCache::find_node(uint8_t node) const {
    for (size_t i = 0; i < node_count_; i++) {
        if (nodes_[i].node == node) return &nodes_[i];
    }
    return nullptr;
}

CanRemoteCache::NodeSlot* CanRemoteCache::touch_node(uint8_t node, int64_t now_us) {
    NodeSlot* n = find_node(node);
    if (!n) {
        if (node_count_ >= MAX_NODES) return nullptr;   // пул вичерпано — ігноруємо
        n = &nodes_[node_count_++];
        n->node = node;
    }
    n->last_us = now_us;
    return n;
}

const CanReading* CanRemoteCache::subscribe(uint8_t node, uint16_t key_id) {
    if (node < can::NODE_MIN || node > can::NODE_MAX || key_id == 0) return nullptr;

    // Ідемпотентність: та сама пара (node, key_id) → той самий слот.
    for (size_t i = 0; i < sub_count_; i++) {
        if (subs_[i].used && subs_[i].node == node && subs_[i].key_id == key_id) {
            return &subs_[i].r;
        }
    }
    if (sub_count_ >= MAX_SUBSCRIPTIONS) return nullptr;

    Sub& s = subs_[sub_count_++];
    s.node   = node;
    s.key_id = key_id;
    s.used   = true;

    // Вузол підписки трекається для liveness навіть без remote_devices запису.
    if (!find_node(node) && node_count_ < MAX_NODES) {
        nodes_[node_count_].node = node;
        node_count_++;
    }
    return &s.r;
}

bool CanRemoteCache::node_online(uint8_t node) const {
    const NodeSlot* n = find_node(node);
    return n && n->online;
}

void CanRemoteCache::register_node(uint8_t node) {
    if (node < can::NODE_MIN || node > can::NODE_MAX) return;
    NodeSlot* n = find_node(node);
    if (!n) {
        if (node_count_ >= MAX_NODES) return;
        n = &nodes_[node_count_++];
        n->node = node;
    }
    n->registered = true;
}

void CanRemoteCache::on_state_frame(uint8_t node, uint16_t key_id,
                                    const can::StatePayload& p, int64_t now_us) {
    touch_node(node, now_us);
    for (size_t i = 0; i < sub_count_; i++) {
        Sub& s = subs_[i];
        if (!s.used || s.node != node || s.key_id != key_id) continue;
        s.r.type    = p.type;
        s.r.valid   = p.valid;
        s.r.f32     = p.f32;
        s.r.i32     = p.i32;
        s.r.b       = p.b;
        s.r.last_us = now_us;
        // Пар (node, key_id) у пулі не буває двох (subscribe ідемпотентний) —
        // але добиваємо цикл до кінця свідомо: дешево і без прихованих інваріантів.
    }
}

void CanRemoteCache::on_heartbeat(uint8_t node, uint8_t flags, int64_t now_us) {
    NodeSlot* n = touch_node(node, now_us);
    if (n) n->hb_flags = flags;
}

bool CanRemoteCache::tick(int64_t now_us, int64_t timeout_us) {
    bool changed = false;
    for (size_t i = 0; i < node_count_; i++) {
        NodeSlot& n = nodes_[i];
        bool alive = n.last_us != 0 && (now_us - n.last_us) < timeout_us;
        if (alive != n.online) {
            n.online = alive;
            changed  = true;
            if (!alive) {
                // Вузол зник → усі його підписки стають несправними, щоб
                // is_healthy() драйвера-моста впав одразу, не чекаючи stale_ms.
                for (size_t j = 0; j < sub_count_; j++) {
                    if (subs_[j].used && subs_[j].node == n.node) {
                        subs_[j].r.valid = false;
                    }
                }
            }
        }
    }
    return changed;
}

void CanRemoteCache::clear() {
    for (size_t i = 0; i < MAX_NODES; i++) nodes_[i] = NodeSlot{};
    for (size_t i = 0; i < MAX_SUBSCRIPTIONS; i++) subs_[i] = Sub{};
    node_count_ = 0;
    sub_count_  = 0;
}

} // namespace modesp
