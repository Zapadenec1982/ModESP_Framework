/**
 * @file module_manager.h
 * @brief Реєстрація, lifecycle та message bus для модулів
 *
 * ModuleManager відповідає за:
 *   - Реєстрацію модулів
 *   - Порядок init/update/stop (по пріоритету)
 *   - Доставку повідомлень через etl::message_bus
 *   - Відстеження heartbeat модулів
 *
 * Модулі реєструються як static об'єкти з main.cpp.
 * ModuleAdapter обгортає BaseModule як etl::message_router для шини.
 */

#pragma once

#include "modesp/types.h"
#include "modesp/base_module.h"
#include "modesp/shared_state.h"

#include "etl/message_bus.h"
#include "etl/message_router.h"
#include "etl/vector.h"
#include "etl/array.h"

namespace modesp {

#ifndef MODESP_MAX_MODULES
#define MODESP_MAX_MODULES 16
#endif

#ifndef MODESP_MAX_BUS_ROUTERS
#define MODESP_MAX_BUS_ROUTERS 24
#endif

// ═══════════════════════════════════════════════════════════════
// ModuleAdapter — обгортає BaseModule як etl::imessage_router
// Catch-all: приймає ВСІ повідомлення і передає в BaseModule.
// В ETL 20.41+ не можна використовувати etl::message_router з
// etl::imessage, тому реалізуємо imessage_router напряму.
// ═══════════════════════════════════════════════════════════════

class ModuleAdapter : public etl::imessage_router {
public:
    // Default id=0 — bus використовує broadcast, тому id не критичний
    ModuleAdapter(etl::message_router_id_t id = 0)
        : etl::imessage_router(id)
        , module_(nullptr)
    {}

    void bind(BaseModule* module) { module_ = module; }
    BaseModule* module() const { return module_; }

    // ── imessage_router interface ──
    void receive(const etl::imessage& msg) override {
        if (module_) {
            module_->on_message(msg);
        }
    }

    // Catch-all: приймає будь-який message_id
    bool accepts(etl::message_id_t) const override {
        return true;
    }

    bool is_null_router() const override  { return false; }
    bool is_producer() const override     { return false; }
    bool is_consumer() const override     { return true; }

private:
    BaseModule* module_;
};

// ═══════════════════════════════════════════════════════════════
// ModuleManager
// ═══════════════════════════════════════════════════════════════

class ModuleManager {
public:
    ModuleManager();
    ~ModuleManager() = default;

    // Не копіюється
    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    // ── Реєстрація ──
    bool register_module(BaseModule& module);

    // ── Lifecycle (викликається App) ──
    bool init_all(SharedState& state);
    void update_all(uint32_t dt_ms);
    void stop_all();

    // ── Перезапуск модуля (викликається WatchdogService) ──
    bool restart_module(BaseModule& module);

    // ── Message Bus ──
    void publish(const etl::imessage& msg);

    // ── Діагностика ──
    size_t module_count() const { return modules_.size(); }

    // Callback для ітерації
    using ModuleCallback = void(*)(BaseModule& module, void* user_data);
    void for_each(ModuleCallback cb, void* user_data);

    // Const iteration (for HTTP API serialization)
    using ModuleVisitor = void(*)(const BaseModule& module, void* user_data);
    void for_each_module(ModuleVisitor visitor, void* user_data) const;

private:
    // Модулі та їх адаптери
    etl::vector<BaseModule*, MODESP_MAX_MODULES> modules_;
    etl::array<ModuleAdapter, MODESP_MAX_MODULES> adapters_;
    size_t adapter_count_ = 0;

    // Message Bus
    etl::message_bus<MODESP_MAX_BUS_ROUTERS> bus_;

    // Зв'язок з SharedState (встановлюється в init_all)
    SharedState* shared_state_ = nullptr;

    // Сортування по пріоритету
    void sort_by_priority();
};

} // namespace modesp
