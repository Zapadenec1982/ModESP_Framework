/**
 * @file persist_service.cpp
 * @brief Auto-persist readwrite settings to NVS
 *
 * При boot: ітерує STATE_META, для persist==true читає NVS → SharedState.
 * При runtime: SharedState callback ставить dirty flag, on_update() пише з debounce.
 *
 * BUG-012 fix: NVS ключі на основі djb2 хешу імені state key (стабільні
 * при додаванні/видаленні/перестановці ключів). Автоматична міграція зі
 * старих позиційних ключів "p0".."p32" при першому boot.
 *
 * BUG-025 fix: debounce_timer_ більше НЕ скидається в on_state_changed().
 * defrost.interval_timer змінюється щосекунди і скидав глобальний таймер,
 * через що жоден persist key (включаючи thermostat.setpoint) не флашився.
 *
 * NVS wear fix: runtime counters (interval_timer, defrost_count) прибрані
 * з persist — скидаються при ребуті, не зношують NVS.
 */

#include "modesp/services/persist_service.h"
#include "modesp/services/nvs_helper.h"
#include "state_meta.h"
#include "esp_log.h"

#include <cstdio>
#include <cstring>

static const char* TAG = "Persist";

namespace modesp {

PersistService::PersistService()
    : BaseModule("persist", ModulePriority::CRITICAL)
{
    memset(dirty_, 0, sizeof(dirty_));
}

// BUG-012: стабільний NVS ключ — djb2 хеш від імені state key
// Формат: "s" + 7 hex символів = 8 chars (ESP-IDF NVS limit: 15 chars)
void PersistService::make_nvs_key(const char* state_key, char* out, size_t out_size) {
    uint32_t hash = 5381;
    for (const char* p = state_key; *p; p++) {
        hash = ((hash << 5) + hash) + static_cast<unsigned char>(*p);
    }
    snprintf(out, out_size, "s%07lx", (unsigned long)(hash & 0x0FFFFFFFul));
}

// Legacy: позиційний ключ для міграції зі старого формату
void PersistService::make_legacy_nvs_key(size_t index, char* out, size_t out_size) {
    snprintf(out, out_size, "p%u", (unsigned)index);
}

bool PersistService::on_init() {
    if (!ext_state_) {
        ESP_LOGE(TAG, "SharedState не встановлений — викличте set_state() перед init");
        return false;
    }

    // Відновлюємо збережені значення з NVS
    restore_from_nvs();

    // Реєструємо persist callback
    ext_state_->set_persist_callback(on_state_changed, this);

    ESP_LOGI(TAG, "Initialized (debounce=%lu ms)", DEBOUNCE_MS);
    return true;
}

void PersistService::restore_from_nvs() {
    if (!ext_state_) return;

    int restored = 0;
    int migrated = 0;

    // Batch: один handle для всіх read операцій (зменшує heap фрагментацію)
    auto h_ro = nvs_helper::batch_open(NVS_NAMESPACE, true);
    // RW handle для міграції (відкриваємо лише якщо потрібно)
    nvs_handle_t h_rw = 0;

    for (size_t i = 0; i < gen::STATE_META_COUNT; i++) {
        const auto& meta = gen::STATE_META[i];
        if (!meta.persist) continue;

        char nvs_key[16];
        make_nvs_key(meta.key, nvs_key, sizeof(nvs_key));

        char legacy_key[8];
        make_legacy_nvs_key(i, legacy_key, sizeof(legacy_key));

        if (strcmp(meta.type, "float") == 0) {
            float val = 0.0f;
            if (nvs_helper::batch_read_float(h_ro, nvs_key, val)) {
                ext_state_->set(meta.key, val);
                ESP_LOGI(TAG, "Restored %s = %.2f (key: %s)",
                         meta.key, static_cast<double>(val), nvs_key);
                restored++;
            } else if (nvs_helper::batch_read_float(h_ro, legacy_key, val)) {
                // Міграція: legacy → новий ключ
                ext_state_->set(meta.key, val);
                if (!h_rw) h_rw = nvs_helper::batch_open(NVS_NAMESPACE, false);
                nvs_helper::batch_write_float(h_rw, nvs_key, val);
                nvs_helper::batch_erase_key(h_rw, legacy_key);
                ESP_LOGI(TAG, "Migrated %s = %.2f (%s → %s)",
                         meta.key, static_cast<double>(val), legacy_key, nvs_key);
                restored++;
                migrated++;
            } else {
                ext_state_->set(meta.key, meta.default_val);
                ESP_LOGI(TAG, "Default %s = %.2f (not in NVS)",
                         meta.key, static_cast<double>(meta.default_val));
            }
        } else if (strcmp(meta.type, "int") == 0) {
            int32_t val = 0;
            if (nvs_helper::batch_read_i32(h_ro, nvs_key, val)) {
                ext_state_->set(meta.key, val);
                ESP_LOGI(TAG, "Restored %s = %ld (key: %s)",
                         meta.key, (long)val, nvs_key);
                restored++;
            } else if (nvs_helper::batch_read_i32(h_ro, legacy_key, val)) {
                ext_state_->set(meta.key, val);
                if (!h_rw) h_rw = nvs_helper::batch_open(NVS_NAMESPACE, false);
                nvs_helper::batch_write_i32(h_rw, nvs_key, val);
                nvs_helper::batch_erase_key(h_rw, legacy_key);
                ESP_LOGI(TAG, "Migrated %s = %ld (%s → %s)",
                         meta.key, (long)val, legacy_key, nvs_key);
                restored++;
                migrated++;
            } else {
                ext_state_->set(meta.key, static_cast<int32_t>(meta.default_val));
            }
        } else if (strcmp(meta.type, "bool") == 0) {
            bool val = false;
            if (nvs_helper::batch_read_bool(h_ro, nvs_key, val)) {
                ext_state_->set(meta.key, val);
                ESP_LOGI(TAG, "Restored %s = %s (key: %s)",
                         meta.key, val ? "true" : "false", nvs_key);
                restored++;
            } else if (nvs_helper::batch_read_bool(h_ro, legacy_key, val)) {
                ext_state_->set(meta.key, val);
                if (!h_rw) h_rw = nvs_helper::batch_open(NVS_NAMESPACE, false);
                nvs_helper::batch_write_bool(h_rw, nvs_key, val);
                nvs_helper::batch_erase_key(h_rw, legacy_key);
                ESP_LOGI(TAG, "Migrated %s = %s (%s → %s)",
                         meta.key, val ? "true" : "false", legacy_key, nvs_key);
                restored++;
                migrated++;
            } else {
                ext_state_->set(meta.key, meta.default_val != 0.0f);
            }
        }
    }

    nvs_helper::batch_close(h_ro);
    if (h_rw) nvs_helper::batch_close(h_rw);

    ESP_LOGI(TAG, "Restored %d keys from NVS", restored);
    if (migrated > 0) {
        ESP_LOGI(TAG, "Migrated %d keys from legacy positional format", migrated);
    }
}

void PersistService::on_state_changed(const StateKey& key, const StateValue& value, void* user_data) {
    auto* self = static_cast<PersistService*>(user_data);
    (void)value;

    // Шукаємо ключ в STATE_META з persist==true
    for (size_t i = 0; i < gen::STATE_META_COUNT && i < MAX_PERSIST_KEYS; i++) {
        const auto& meta = gen::STATE_META[i];
        if (!meta.persist) continue;

        // Порівнюємо ключі
        if (key == StateKey(meta.key)) {
            self->dirty_[i] = true;
            // BUG-025: НЕ скидаємо debounce_timer_ тут.
            // defrost.interval_timer змінюється щосекунди і скидав таймер,
            // через що жоден persist key ніколи не флашився в NVS.
            return;
        }
    }
}

void PersistService::on_update(uint32_t dt_ms) {
    // Перевіряємо чи є dirty keys
    bool has_dirty = false;
    for (size_t i = 0; i < gen::STATE_META_COUNT && i < MAX_PERSIST_KEYS; i++) {
        if (dirty_[i]) {
            has_dirty = true;
            break;
        }
    }

    if (!has_dirty) return;

    // Debounce — чекаємо DEBOUNCE_MS від першого dirty flag (BUG-025)
    debounce_timer_ += dt_ms;
    if (debounce_timer_ < DEBOUNCE_MS) return;

    flush_to_nvs();
    debounce_timer_ = 0;
}

// BUG-014: примусовий запис — викликається перед restart
void PersistService::flush_now() {
    flush_to_nvs();
    debounce_timer_ = 0;
}

void PersistService::flush_to_nvs() {
    if (!ext_state_) return;

    // Batch: один handle для всіх write операцій (зменшує heap фрагментацію)
    auto h = nvs_helper::batch_open(NVS_NAMESPACE, false);
    if (!h) return;

    int saved = 0;

    for (size_t i = 0; i < gen::STATE_META_COUNT && i < MAX_PERSIST_KEYS; i++) {
        if (!dirty_[i]) continue;
        dirty_[i] = false;

        const auto& meta = gen::STATE_META[i];
        if (!meta.persist) continue;

        auto val = ext_state_->get(meta.key);
        if (!val.has_value()) continue;

        // BUG-012: стабільний хеш-ключ замість позиційного
        char nvs_key[16];
        make_nvs_key(meta.key, nvs_key, sizeof(nvs_key));

        bool ok = false;
        if (strcmp(meta.type, "float") == 0) {
            const auto* fp = etl::get_if<float>(&val.value());
            if (fp) {
                ok = nvs_helper::batch_write_float(h, nvs_key, *fp);
            } else {
                // BUG-023: fallback — int32_t → float конвертація
                const auto* ip = etl::get_if<int32_t>(&val.value());
                if (ip) ok = nvs_helper::batch_write_float(h, nvs_key, static_cast<float>(*ip));
            }
        } else if (strcmp(meta.type, "int") == 0) {
            const auto* ip = etl::get_if<int32_t>(&val.value());
            if (ip) {
                ok = nvs_helper::batch_write_i32(h, nvs_key, *ip);
            } else {
                // BUG-023: fallback — float → int32_t конвертація
                const auto* fp = etl::get_if<float>(&val.value());
                if (fp) ok = nvs_helper::batch_write_i32(h, nvs_key, static_cast<int32_t>(*fp));
            }
        } else if (strcmp(meta.type, "bool") == 0) {
            const auto* bp = etl::get_if<bool>(&val.value());
            if (bp) ok = nvs_helper::batch_write_bool(h, nvs_key, *bp);
        }

        if (ok) {
            saved++;
            ESP_LOGI(TAG, "Saved %s to NVS (key: %s)", meta.key, nvs_key);
        } else {
            ESP_LOGW(TAG, "Failed to save %s to NVS", meta.key);
        }
    }

    // Один commit + close для всіх записів
    nvs_helper::batch_close(h);

    if (saved > 0) {
        ESP_LOGI(TAG, "Flushed %d keys to NVS", saved);
    }
}

} // namespace modesp
