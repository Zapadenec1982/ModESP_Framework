/**
 * @file menu_engine.h
 * @brief Движок екранного меню — навігація по згенерованому дереву.
 *
 * Споживає дерево меню з generated/display_screens.h (DisplayMenuNode) через DI
 * (MenuData). АГНОСТИЧНИЙ до геометрії дисплея: віддає СЕМАНТИЧНІ View
 * (MainView/MenuView/EditView, display_port.h), а не готовий кадр. Скрол/курсор-
 * презентацію рахує драйвер (CharGridLayout). FSM: MAIN → MENU → EDIT.
 *
 * Zero heap: etl-контейнери, фіксовані розміри. Безпечний для on_update().
 */

#pragma once

#include <cstdint>
#include "etl/optional.h"
#include "etl/string.h"
#include "etl/vector.h"
#include "modesp/types.h"
#include "display_screens.h"
#include "display/display_port.h"

namespace modesp::display {

enum class MenuEvent : uint8_t { NONE, UP, DOWN, SELECT };

/// Доступ движка до стану — відв'язка від BaseModule/SharedState.
class IMenuStateIO {
public:
    virtual ~IMenuStateIO() = default;
    virtual etl::optional<StateValue> get(const char* key) const = 0;
    virtual bool set(const char* key, const StateValue& value) = 0;
};

/// Дані меню. Прошивка передає згенеровані масиви з display_screens.h,
/// тести — статичні фікстури з тими ж структурами.
struct MenuData {
    const gen::DisplayMenuNode*  nodes;
    size_t                       node_count;
    uint8_t                      root_first;
    uint8_t                      root_count;
    const gen::DisplayMainValue* main_values;
    size_t                       main_count;
    // Capability-gate (ADR-003 §3.3): паралельний масив cap-вимог на вузол + поточні caps
    // backend-у. node_caps == nullptr → гейтингу немає (фікстури тестів не зачеплені).
    const gen::DisplayCap*       node_caps = nullptr;
    DisplayCaps                  caps = {};
};

class MenuEngine {
public:
    enum class Screen : uint8_t { MAIN, MENU, EDIT };

    static constexpr uint32_t IDLE_TIMEOUT_MS = 30000;  // авто-повернення на MAIN
    static constexpr uint32_t REFRESH_MS      = 500;    // оновлення значень на екрані
    static constexpr size_t   MAX_DEPTH       = 4;

    MenuEngine(IMenuStateIO& io, const MenuData& data);

    /// Оновити можливості backend-у (гейтинг складу меню). Кличеться після resolve порту.
    void set_caps(const DisplayCaps& caps);

    /// Обробити подію кнопки (скидає idle-таймер, перебудовує View).
    void handle_event(MenuEvent ev);

    /// Періодичний тік: оновлення значень, idle-timeout.
    void tick(uint32_t dt_ms);

    Screen screen() const { return screen_; }

    // Активний семантичний View (валідний відповідно до screen()).
    const MainView& main_view() const { return main_view_; }
    const MenuView& menu_view() const { return menu_view_; }
    const EditView& edit_view() const { return edit_view_; }

    /// true один раз після зміни активного View (для драйвера).
    bool consume_dirty();

    /// "main" | "menu:<label>" | "edit:<label>" — для display.screen.
    const char* screen_name() const { return screen_name_.c_str(); }

private:
    struct Level {
        uint8_t node;
        uint8_t cursor;   // курсор; скрол більше НЕ зберігаємо (рахує драйвер)
    };

    // ── навігація ──
    bool    cap_visible_(uint8_t node_idx) const;  // вузол видимий за поточних caps (ADR-003 §3.3)
    uint8_t list_count() const;             // ВИДИМІ items + 1 (back)
    uint8_t child_node(uint8_t i) const;    // index у data_.nodes (i-й ВИДИМИЙ child)
    void    nav_up();
    void    nav_down();
    void    nav_select();
    void    to_main();

    // ── редагування ──
    void begin_edit(uint8_t node_idx);
    void edit_adjust(int8_t dir);
    void edit_save();

    // ── побудова View ──
    void rebuild();
    void build_main(MainView& v) const;
    void build_menu(MenuView& v) const;
    void build_edit(EditView& v) const;
    void format_value(const gen::DisplayMenuNode& n, etl::istring& out) const;
    void update_screen_name();

    IMenuStateIO&  io_;
    MenuData       data_;

    Screen screen_ = Screen::MAIN;

    // MENU
    etl::vector<Level, MAX_DEPTH> stack_;   // батьківські рівні
    uint8_t cursor_ = 0;

    // EDIT
    uint8_t edit_node_ = 0;
    float   edit_f_    = 0.0f;   // буфер для FLOAT/INT
    uint8_t edit_opt_  = 0;      // індекс опції для ENUM, 0/1 для BOOL
    bool    edit_dirty_ = false; // чи користувач реально змінював (editenum-1)
    bool    edit_known_ = true;  // чи поточне значення знайдено в опціях (ENUM)

    // таймери
    uint32_t idle_acc_    = 0;
    uint32_t refresh_acc_ = 0;

    // кешований активний View + dirty
    MainView main_view_;
    MenuView menu_view_;
    EditView edit_view_;
    Screen   built_screen_ = Screen::MAIN;
    bool     dirty_ = false;
    etl::string<48> screen_name_;
};

} // namespace modesp::display
