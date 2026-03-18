/**
 * @file persist_service.h
 * @brief Auto-persist readwrite settings to NVS
 *
 * Зберігає readwrite state keys з persist:true в NVS.
 * При boot — відновлює збережені значення з NVS в SharedState.
 * При зміні — записує в NVS з debounce (5 секунд).
 *
 * NVS key strategy (BUG-012 fix): стабільний хеш від імені ключа
 * (djb2 → "s" + 7 hex chars). Позиційні ключі "p0".."p32" мігруються
 * автоматично при першому boot після оновлення.
 *
 * NVS namespace: "persist".
 */

#pragma once

#include "modesp/base_module.h"
#include "modesp/shared_state.h"

namespace modesp {

class PersistService : public BaseModule {
public:
    PersistService();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

    /// Ін'єкція залежності — SharedState
    void set_state(SharedState* state) { ext_state_ = state; }

    /// BUG-014: примусовий запис dirty keys в NVS (перед restart)
    void flush_now();

private:
    static constexpr const char* NVS_NAMESPACE = "persist";
    static constexpr uint32_t DEBOUNCE_MS = 5000;  // 5 секунд debounce
    static constexpr size_t MAX_PERSIST_KEYS = gen::STATE_META_COUNT;

    SharedState* ext_state_ = nullptr;

    // Dirty flags для кожного persist key в STATE_META
    bool dirty_[MAX_PERSIST_KEYS] = {};
    uint32_t debounce_timer_ = 0;

    /// Відновити збережені значення з NVS → SharedState
    void restore_from_nvs();

    /// Записати dirty keys в NVS
    void flush_to_nvs();

    /// BUG-012: стабільний NVS ключ на основі djb2 хешу імені state key
    static void make_nvs_key(const char* state_key, char* out, size_t out_size);

    /// Legacy: позиційний ключ "p0", "p1"... (для міграції)
    static void make_legacy_nvs_key(size_t index, char* out, size_t out_size);

    /// Persist callback — викликається з SharedState::set()
    static void on_state_changed(const StateKey& key, const StateValue& value, void* user_data);
};

} // namespace modesp
