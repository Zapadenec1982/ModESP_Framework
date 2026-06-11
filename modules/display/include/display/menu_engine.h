/**
 * @file menu_engine.h
 * @brief Движок екранного меню — навігація по згенерованому дереву
 *
 * Споживає дерево меню з generated/display_screens.h (DisplayMenuNode),
 * але отримує його через DI (MenuData) — прошивка передає згенеровані
 * масиви, host-тести — власні фікстури.
 *
 * FSM: MAIN (головний екран з main values) → MENU (список) → EDIT
 * (редагування значення з clamp по min/max/step зі state-маніфесту).
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
#include "display/renderer.h"

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
};

class MenuEngine {
public:
    enum class Screen : uint8_t { MAIN, MENU, EDIT };

    static constexpr uint32_t IDLE_TIMEOUT_MS = 30000;  // авто-повернення на MAIN
    static constexpr uint32_t MAIN_ROTATE_MS  = 4000;   // зміна сторінки main values
    static constexpr uint32_t REFRESH_MS      = 500;    // оновлення значень на екрані
    static constexpr size_t   VISIBLE_ITEMS   = DisplayFrame::MAX_ROWS - 1;
    static constexpr size_t   MAX_DEPTH       = 4;

    MenuEngine(IMenuStateIO& io, const MenuData& data);

    /// Обробити подію кнопки (скидає idle-таймер, перебудовує кадр).
    void handle_event(MenuEvent ev);

    /// Періодичний тік: оновлення значень, ротація MAIN, idle-timeout.
    void tick(uint32_t dt_ms);

    const DisplayFrame& frame() const { return frame_; }

    /// true один раз після зміни кадру (для рендерера).
    bool consume_dirty();

    Screen screen() const { return screen_; }

    /// "main" | "menu:<label>" | "edit:<label>" — для display.screen.
    const char* screen_name() const { return screen_name_.c_str(); }

private:
    struct Level {
        uint8_t node;
        uint8_t cursor;
        uint8_t scroll;
    };

    // ── навігація ──
    uint8_t list_count() const;             // items + 1 (back)
    uint8_t child_node(uint8_t i) const;    // index в data_.nodes
    void    nav_up();
    void    nav_down();
    void    nav_select();
    void    to_main();

    // ── редагування ──
    void begin_edit(uint8_t node_idx);
    void edit_adjust(int8_t dir);
    void edit_save();

    // ── кадр ──
    void rebuild();
    void build_main(DisplayFrame& f) const;
    void build_menu(DisplayFrame& f) const;
    void build_edit(DisplayFrame& f) const;
    void format_value(const gen::DisplayMenuNode& n, etl::istring& out) const;
    void update_screen_name();
    size_t main_pages() const;

    IMenuStateIO&  io_;
    MenuData       data_;

    Screen screen_ = Screen::MAIN;

    // MENU
    etl::vector<Level, MAX_DEPTH> stack_;  // батьківські рівні
    uint8_t cursor_ = 0;
    uint8_t scroll_ = 0;

    // EDIT
    uint8_t edit_node_ = 0;
    float   edit_f_    = 0.0f;   // буфер для FLOAT/INT
    uint8_t edit_opt_  = 0;      // індекс опції для ENUM, 0/1 для BOOL

    // таймери
    uint32_t idle_acc_    = 0;
    uint32_t rotate_acc_  = 0;
    uint32_t refresh_acc_ = 0;
    uint8_t  main_page_   = 0;

    DisplayFrame        frame_;
    bool                dirty_ = false;
    etl::string<48>     screen_name_;
};

} // namespace modesp::display
