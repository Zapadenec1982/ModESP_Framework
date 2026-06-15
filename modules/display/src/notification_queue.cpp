/**
 * @file notification_queue.cpp
 * @brief Реалізація priority-черги банерів (ADR-001).
 */

#include "display/notification_queue.h"

namespace modesp::display {

static NoticeLevel to_level(uint8_t l) {
    if (l >= 2) return NoticeLevel::ALARM;
    if (l == 1) return NoticeLevel::WARN;
    return NoticeLevel::INFO;
}

void NotificationQueue::push(uint8_t level, uint16_t ttl_ms, const char* text) {
    Entry e;
    e.level     = level;
    e.infinite  = (ttl_ms == 0);
    e.remaining = ttl_ms;
    e.seq       = next_seq_++;
    e.text.assign(text);

    if (q_.full()) {
        // витіснити найнижчий за пріоритетом
        size_t mi = 0;
        for (size_t i = 1; i < q_.size(); ++i)
            if (q_[i].level < q_[mi].level) mi = i;
        if (level > q_[mi].level) q_[mi] = e;
        else return;  // новий — найнижчий, дропаємо його
    } else {
        q_.push_back(e);
    }
    recompute();
}

void NotificationQueue::tick(uint32_t dt_ms) {
    // notif-1: відлічувати TTL УСІХ finite-банерів (не лише активного) і стерти прострочені.
    size_t w = 0;
    for (size_t r = 0; r < q_.size(); ++r) {
        bool keep = true;
        if (!q_[r].infinite) {
            if (q_[r].remaining > dt_ms) q_[r].remaining -= dt_ms;
            else keep = false;            // вийшов TTL
        }
        if (keep) {
            if (w != r) q_[w] = q_[r];
            ++w;
        }
    }
    while (q_.size() > w) q_.pop_back();
    recompute();
}

bool NotificationQueue::consume_dirty() {
    const bool d = dirty_;
    dirty_ = false;
    return d;
}

size_t NotificationQueue::active_index() const {
    if (q_.empty()) return CAP;
    size_t bi = 0;
    for (size_t i = 1; i < q_.size(); ++i) {
        // notif-3: max level, при рівних — earliest seq (стабільний FIFO навіть після overflow-заміни)
        if (q_[i].level > q_[bi].level ||
            (q_[i].level == q_[bi].level && q_[i].seq < q_[bi].seq)) bi = i;
    }
    return bi;
}

void NotificationQueue::recompute() {
    const size_t ai = active_index();
    if (ai >= q_.size()) {
        if (active_valid_) dirty_ = true;
        active_valid_ = false;
        return;
    }
    Notice n;
    n.level = to_level(q_[ai].level);
    n.text.assign(q_[ai].text.c_str());
    if (!active_valid_ || n.level != active_.level || n.text != active_.text) {
        dirty_ = true;
    }
    active_       = n;
    active_valid_ = true;
}

} // namespace modesp::display
