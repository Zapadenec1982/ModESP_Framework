/**
 * @file app.h
 * @brief Application lifecycle — ініціалізація та основний цикл
 *
 * App — тонка обгортка, що зв'язує ModuleManager + SharedState.
 * Використовується тільки з main.cpp.
 * Модулі НЕ повинні залежати від App — вони працюють через BaseModule API.
 *
 * TODO: в майбутньому прибрати singleton, передавати залежності явно.
 */

#pragma once

#include "modesp/module_manager.h"
#include "modesp/shared_state.h"

namespace modesp {

#ifndef MODESP_MAIN_LOOP_HZ
#define MODESP_MAIN_LOOP_HZ 100
#endif

#ifndef MODESP_UPDATE_BUDGET_MS
#define MODESP_UPDATE_BUDGET_MS 8
#endif

#ifndef MODESP_WDT_TIMEOUT_MS
#define MODESP_WDT_TIMEOUT_MS 5000   // 5 секунд HW watchdog
#endif

class App {
public:
    // Singleton (тимчасово, для Phase 1)
    static App& instance();

    // Не копіюється
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // ── Lifecycle ──
    bool init();
    void run();   // Блокує назавжди (main loop)
    void stop();  // Graceful shutdown

    // ── Доступ до підсистем (тільки для main.cpp!) ──
    ModuleManager& modules() { return modules_; }
    SharedState&   state()   { return state_; }

    // ── Стан ──
    uint32_t uptime_sec() const;
    bool     is_running() const { return running_; }

private:
    App() = default;

    ModuleManager modules_;
    SharedState   state_;
    bool          running_ = false;
    uint32_t      boot_time_us_ = 0;
};

} // namespace modesp
