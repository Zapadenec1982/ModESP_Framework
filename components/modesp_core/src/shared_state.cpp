/**
 * @file shared_state.cpp
 * @brief Реалізація thread-safe key-value сховища
 */

#include "modesp/shared_state.h"
#include "esp_log.h"

static const char* TAG = "SharedState";

namespace modesp {

SharedState::SharedState() {
    mutex_ = xSemaphoreCreateMutex();
    configASSERT(mutex_ != nullptr);
}

SharedState::~SharedState() {
    if (mutex_) {
        vSemaphoreDelete(mutex_);
    }
}

bool SharedState::set(const StateKey& key, const StateValue& value, bool track_change) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        set_failures_++;  // BUG-018
        ESP_LOGE(TAG, "Mutex timeout on set('%s') [failures=%lu]",
                 key.c_str(), (unsigned long)set_failures_);
        return false;
    }

    if (map_.full() && !map_.count(key)) {
        set_failures_++;
        // Знімаємо дані до release mutex, логуємо ПІСЛЯ — інакше ризик deadlock
        // (ESP_LOG бере свій внутрішній lock; якщо інший core тримає log lock і чекає
        // state mutex — класичний ABBA deadlock на двоядерному ESP32)
        int sz = (int)map_.size();
        unsigned long failures = (unsigned long)set_failures_;
        xSemaphoreGive(mutex_);
        ESP_LOGE(TAG, "STATE MAP FULL (%d/%d) — cannot add '%s' [failures=%lu]",
                 sz, MODESP_MAX_STATE_ENTRIES, key.c_str(), failures);
        return false;
    }

    // Перевірка чи значення змінилось (для persist callback)
    bool changed = true;
    auto it = map_.find(key);
    if (it != map_.end()) {
        changed = !(it->second == value);
    }

    map_[key] = value;

    if (changed) {
        // Інкрементуємо version тільки при реальній зміні (BUG-017 fix)
        if (track_change) {
            version_++;
        }

        // Delta tracking: додаємо ключ до changed_keys_ (без дублікатів)
        if (track_change) {
            bool found = false;
            for (const auto& k : changed_keys_) {
                if (k == key) { found = true; break; }
            }
            if (!found) {
                if (!changed_keys_.full()) {
                    changed_keys_.push_back(key);
                } else {
                    force_full_ = true;  // Переповнення — fallback на full broadcast
                }
            }
        }
    }

    // Зберігаємо callback локально перед звільненням mutex
    auto cb = persist_cb_;
    auto ud = persist_user_data_;
    xSemaphoreGive(mutex_);

    // Persist callback ПОЗА mutex — незалежно від track_change
    if (changed && cb) {
        cb(key, value, ud);
    }

    return true;
}

etl::optional<StateValue> SharedState::get(const StateKey& key) const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return etl::nullopt;
    }

    auto it = map_.find(key);
    etl::optional<StateValue> result;
    if (it != map_.end()) {
        result = it->second;
    }
    xSemaphoreGive(mutex_);
    return result;
}

bool SharedState::has(const StateKey& key) const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool result = map_.count(key) > 0;
    xSemaphoreGive(mutex_);
    return result;
}

bool SharedState::remove(const StateKey& key) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    size_t removed = map_.erase(key);
    xSemaphoreGive(mutex_);
    return removed > 0;
}

size_t SharedState::size() const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t s = map_.size();
    xSemaphoreGive(mutex_);
    return s;
}

void SharedState::clear() {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;
    map_.clear();
    xSemaphoreGive(mutex_);
}

// ── Зручні обгортки ──

bool SharedState::set(const char* key, int32_t value, bool track_change) {
    return set(StateKey(key), StateValue(value), track_change);
}

bool SharedState::set(const char* key, float value, bool track_change) {
    return set(StateKey(key), StateValue(value), track_change);
}

bool SharedState::set(const char* key, bool value, bool track_change) {
    return set(StateKey(key), StateValue(value), track_change);
}

bool SharedState::set(const char* key, const char* value, bool track_change) {
    return set(StateKey(key), StateValue(StringValue(value)), track_change);
}

etl::optional<StateValue> SharedState::get(const char* key) const {
    return get(StateKey(key));
}

// ── Version ──

uint32_t SharedState::version() const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    uint32_t v = version_;
    xSemaphoreGive(mutex_);
    return v;
}

// ── Delta tracking ──

bool SharedState::has_changes() const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool result = !changed_keys_.empty() || force_full_;
    xSemaphoreGive(mutex_);
    return result;
}

bool SharedState::needs_full_broadcast() const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool result = force_full_;
    xSemaphoreGive(mutex_);
    return result;
}

bool SharedState::for_each_changed_and_clear(IterCallback cb, void* user_data) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;

    bool had_changes = false;

    if (force_full_) {
        // Переповнення changed_keys_ — серіалізуємо ВСІ ключі
        for (auto& pair : map_) {
            cb(pair.first, pair.second, user_data);
        }
        had_changes = true;
    } else {
        // Серіалізуємо тільки змінені ключі
        for (const auto& key : changed_keys_) {
            auto it = map_.find(key);
            if (it != map_.end()) {
                cb(it->first, it->second, user_data);
                had_changes = true;
            }
        }
    }

    changed_keys_.clear();
    force_full_ = false;

    xSemaphoreGive(mutex_);
    return had_changes;
}

// ── Persist callback ──

void SharedState::set_persist_callback(PersistCallback cb, void* user_data) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;
    persist_cb_ = cb;
    persist_user_data_ = user_data;
    xSemaphoreGive(mutex_);
}

// ── Ітерація ──

void SharedState::for_each(IterCallback cb, void* user_data) const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;
    for (auto& pair : map_) {
        cb(pair.first, pair.second, user_data);
    }
    xSemaphoreGive(mutex_);
}

} // namespace modesp
