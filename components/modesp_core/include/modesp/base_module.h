/**
 * @file base_module.h
 * @brief Базовий клас для всіх модулів ModESP v4
 *
 * Кожен модуль (драйвер, сервіс, бізнес-логіка) наслідує BaseModule.
 * Модуль отримує доступ до:
 *   - publish()      → відправити повідомлення на шину
 *   - state_set/get  → читати/писати SharedState
 *   - on_init()      → ініціалізація (викликається один раз)
 *   - on_update()    → періодичне оновлення (100Hz)
 *   - on_message()   → обробка вхідних повідомлень
 *   - on_stop()      → graceful shutdown
 *
 * Модуль НЕ знає про App, ModuleManager, або Message Bus напряму.
 * Зв'язок встановлюється через ModuleManager при реєстрації.
 */

#pragma once

#include "modesp/types.h"
#include "features_config.h"
#include "etl/message.h"
#include "etl/optional.h"

namespace modesp {

// Forward declarations — модуль не знає деталей реалізації
class ModuleManager;
class SharedState;

class BaseModule {
public:
    BaseModule(const char* name, ModulePriority priority = ModulePriority::NORMAL)
        : name_(name)
        , priority_(priority)
    {}

    virtual ~BaseModule() = default;

    // ── Lifecycle (перевизначаються модулем) ──
    virtual bool on_init()                            { return true; }
    virtual void on_update(uint32_t dt_ms)            {}
    virtual void on_message(const etl::imessage& msg) {}
    virtual void on_stop()                            {}

    // ── Ідентифікація ──
    const char*    name()     const { return name_; }
    ModulePriority priority() const { return priority_; }

    // ── Стан модуля ──
    enum class State : uint8_t {
        CREATED,
        INITIALIZED,
        RUNNING,
        STOPPED,
        ERROR
    };
    State state() const { return state_; }

    // ── Heartbeat (оновлюється автоматично ModuleManager) ──
    uint32_t last_update_ms() const { return last_update_ms_; }

protected:
    // ── API для модулів (доступний після реєстрації) ──
    
    // Відправити повідомлення на шину (всім підписникам)
    void publish(const etl::imessage& msg);

    // Записати значення в SharedState
    /// @param track_change false = тихий set (не тригерить WS broadcast)
    bool state_set(const StateKey& key, const StateValue& value, bool track_change = true);

    // Зручні обгортки для state_set
    bool state_set(const char* key, int32_t value, bool track_change = true);
    bool state_set(const char* key, float value, bool track_change = true);
    bool state_set(const char* key, bool value, bool track_change = true);
    bool state_set(const char* key, const char* value, bool track_change = true);

    // Прочитати значення з SharedState
    etl::optional<StateValue> state_get(const StateKey& key) const;
    etl::optional<StateValue> state_get(const char* key) const;

    // BUG-013: типізовані reader-и (замість дублювання в кожному модулі)
    float read_float(const char* key, float def = 0.0f) const {
        auto v = state_get(key);
        if (!v.has_value()) return def;
        const auto* fp = etl::get_if<float>(&v.value());
        return fp ? *fp : def;
    }
    bool read_bool(const char* key, bool def = false) const {
        auto v = state_get(key);
        if (!v.has_value()) return def;
        const auto* bp = etl::get_if<bool>(&v.value());
        return bp ? *bp : def;
    }
    int32_t read_int(const char* key, int32_t def = 0) const {
        auto v = state_get(key);
        if (!v.has_value()) return def;
        const auto* ip = etl::get_if<int32_t>(&v.value());
        return ip ? *ip : def;
    }

    // Перевірити чи feature активна (з features_config.h)
    bool has_feature(const char* feature_name) const {
        return modesp::gen::is_feature_active(name(), feature_name);
    }

private:
    friend class ModuleManager;  // ModuleManager встановлює зв'язки

    const char*    name_;
    ModulePriority priority_;
    State          state_ = State::CREATED;
    volatile uint32_t last_update_ms_ = 0;

    // Зворотні посилання (встановлює ModuleManager при реєстрації)
    ModuleManager* manager_ = nullptr;
    SharedState*   shared_state_ = nullptr;
};

} // namespace modesp
