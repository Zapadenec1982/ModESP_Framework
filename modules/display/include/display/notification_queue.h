/**
 * @file notification_queue.h
 * @brief Priority-черга банерів сповіщень (ADR-001).
 *
 * Активний банер = з найвищим level (FIFO при рівних). TTL рахується у tick().
 * При переповненні витісняється НАЙНИЖЧИЙ; новий ALARM ніколи не дропається.
 * Живе в DisplayModule. Zero-heap (etl), host-тестована, без заліза.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include "etl/vector.h"
#include "etl/string.h"
#include "modesp/hal/display_port.h"

namespace modesp::display {

class NotificationQueue {
public:
    static constexpr size_t CAP = 8;

    /// Додати сповіщення. level 0=INFO/1=WARN/2=ALARM; ttl_ms 0 = до явного зняття.
    void push(uint8_t level, uint16_t ttl_ms, const char* text);

    /// Відлік TTL активного банера + перерахунок активного.
    void tick(uint32_t dt_ms);

    /// true один раз після зміни активного банера.
    bool consume_dirty();

    bool          has_active() const { return active_valid_; }
    const Notice& active() const { return active_; }

private:
    struct Entry {
        uint8_t         level;
        uint32_t        remaining;   // мс; ігнорується якщо infinite
        bool            infinite;
        uint32_t        seq;         // монотонний порядок вставки — FIFO при рівних рівнях
        etl::string<48> text;
    };

    size_t active_index() const;     // індекс активного (max level, earliest seq); >=size якщо порожньо
    void   recompute();

    etl::vector<Entry, CAP> q_;
    Notice   active_;
    bool     active_valid_ = false;
    bool     dirty_        = false;
    uint32_t next_seq_     = 0;
};

} // namespace modesp::display
