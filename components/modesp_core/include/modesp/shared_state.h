/**
 * @file shared_state.h
 * @brief Thread-safe key-value сховище стану
 *
 * Централізоване сховище для обміну даними між модулями.
 * Використовує etl::unordered_map з фіксованим розміром.
 *
 * Характеристики:
 *   - Zero heap allocation
 *   - Thread-safe (FreeRTOS mutex)
 *   - O(1) average доступ
 *   - ~6KB RAM при 96 entries
 */

#pragma once

#include "modesp/types.h"
#include "etl/unordered_map.h"
#include "etl/vector.h"
#include "etl/optional.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "state_meta.h"  // MODESP_MAX_STATE_ENTRIES (auto-generated)

namespace modesp {

/// Максимум змінених ключів між WS broadcast-ами.
/// При переповненні — fallback на повну серіалізацію.
static constexpr size_t MAX_CHANGED_KEYS = 32;

class SharedState {
public:
    SharedState();
    ~SharedState();

    // Не копіюється
    SharedState(const SharedState&) = delete;
    SharedState& operator=(const SharedState&) = delete;

    // ── Основний API ──
    /// @param track_change true = додати до changed_keys_ для WS broadcast.
    ///   false = тихий set (таймери, лічильники — не тригерять WS delta).
    bool set(const StateKey& key, const StateValue& value, bool track_change = true);
    etl::optional<StateValue> get(const StateKey& key) const;
    bool has(const StateKey& key) const;
    bool remove(const StateKey& key);
    size_t size() const;
    void clear();

    // ── Зручні обгортки ──
    bool set(const char* key, int32_t value, bool track_change = true);
    bool set(const char* key, float value, bool track_change = true);
    bool set(const char* key, bool value, bool track_change = true);
    bool set(const char* key, const char* value, bool track_change = true);

    etl::optional<StateValue> get(const char* key) const;

    // ── Ітерація (для серіалізації, з блокуванням!) ──
    using Map = etl::unordered_map<StateKey, StateValue, MODESP_MAX_STATE_ENTRIES>;

    // Callback викликається під mutex — не робити довгих операцій!
    using IterCallback = void(*)(const StateKey& key, const StateValue& value, void* user_data);

    /// Ітерація по ВСІХ ключах (для GET /api/state, initial WS)
    void for_each(IterCallback cb, void* user_data) const;

    /// Ітерація лише по змінених ключах + атомарне очищення.
    /// Повертає true якщо були зміни.
    bool for_each_changed_and_clear(IterCallback cb, void* user_data);

    /// true якщо є зміни для WS broadcast
    bool has_changes() const;

    /// true якщо changed_keys_ переповнився — потрібен full broadcast
    bool needs_full_broadcast() const;

    // Version counter — incremented on every tracked set().
    uint32_t version() const;

    // BUG-018: лічильник відмов set() для діагностики
    uint32_t set_failures() const { return set_failures_; }

    // ── Persist callback ──
    // Викликається ПІСЛЯ set() якщо значення змінилось (незалежно від track_change).
    // PersistService реєструє callback для збереження в NVS.
    using PersistCallback = void(*)(const StateKey& key, const StateValue& value, void* user_data);
    void set_persist_callback(PersistCallback cb, void* user_data = nullptr);

private:
    Map map_;
    mutable SemaphoreHandle_t mutex_;
    uint32_t version_ = 0;
    uint32_t set_failures_ = 0;  // BUG-018
    PersistCallback persist_cb_ = nullptr;
    void* persist_user_data_ = nullptr;

    // ── Delta tracking для WS broadcasts ──
    etl::vector<StateKey, MAX_CHANGED_KEYS> changed_keys_;
    bool force_full_ = false;  // true якщо changed_keys_ переповнився
};

} // namespace modesp
