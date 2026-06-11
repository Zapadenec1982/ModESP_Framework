/**
 * @file menu_engine.cpp
 * @brief Реалізація движка екранного меню
 */

#include "display/menu_engine.h"

#include <cstdio>
#include <cstring>
#include <cmath>

namespace modesp::display {

using gen::DisplayItemType;
using gen::DisplayMenuNode;

// ── helpers ────────────────────────────────────────────────────

/// Додати UTF-8 рядок, не розрізаючи багатобайтову послідовність на межі.
static void append_utf8(etl::istring& dst, const char* src) {
    while (*src) {
        const uint8_t lead = static_cast<uint8_t>(*src);
        size_t len = 1;
        if      ((lead & 0xE0) == 0xC0) len = 2;
        else if ((lead & 0xF0) == 0xE0) len = 3;
        else if ((lead & 0xF8) == 0xF0) len = 4;
        if (dst.size() + len > dst.capacity()) break;
        for (size_t i = 0; i < len && *src; ++i) {
            dst.push_back(*src++);
        }
    }
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ── ctor / public API ──────────────────────────────────────────

MenuEngine::MenuEngine(IMenuStateIO& io, const MenuData& data)
    : io_(io)
    , data_(data)
{
    screen_name_ = "main";
}

void MenuEngine::handle_event(MenuEvent ev) {
    if (ev == MenuEvent::NONE) return;
    idle_acc_ = 0;

    switch (screen_) {
    case Screen::MAIN:
        if (ev == MenuEvent::SELECT) {
            if (data_.root_count > 0) {
                screen_ = Screen::MENU;
                stack_.clear();
                cursor_ = 0;
                scroll_ = 0;
            }
        } else if (main_pages() > 1) {
            // UP/DOWN — ручне перемикання сторінок main values
            const size_t pages = main_pages();
            main_page_ = (ev == MenuEvent::DOWN)
                ? static_cast<uint8_t>((main_page_ + 1) % pages)
                : static_cast<uint8_t>((main_page_ + pages - 1) % pages);
            rotate_acc_ = 0;
        }
        break;

    case Screen::MENU:
        if      (ev == MenuEvent::UP)     nav_up();
        else if (ev == MenuEvent::DOWN)   nav_down();
        else if (ev == MenuEvent::SELECT) nav_select();
        break;

    case Screen::EDIT:
        if      (ev == MenuEvent::UP)     edit_adjust(+1);
        else if (ev == MenuEvent::DOWN)   edit_adjust(-1);
        else if (ev == MenuEvent::SELECT) edit_save();
        break;
    }

    rebuild();
}

void MenuEngine::tick(uint32_t dt_ms) {
    // Idle-timeout: повернення на головний екран (зміни в EDIT відкидаються)
    if (screen_ != Screen::MAIN) {
        idle_acc_ += dt_ms;
        if (idle_acc_ >= IDLE_TIMEOUT_MS) {
            to_main();
            rebuild();
            return;
        }
    }

    // Авто-ротація сторінок main values
    if (screen_ == Screen::MAIN && main_pages() > 1) {
        rotate_acc_ += dt_ms;
        if (rotate_acc_ >= MAIN_ROTATE_MS) {
            rotate_acc_ = 0;
            main_page_ = static_cast<uint8_t>((main_page_ + 1) % main_pages());
            rebuild();
            return;
        }
    }

    // Періодичне оновлення значень на екрані (MAIN і MENU показують live-дані)
    refresh_acc_ += dt_ms;
    if (refresh_acc_ >= REFRESH_MS) {
        refresh_acc_ = 0;
        rebuild();
    }
}

bool MenuEngine::consume_dirty() {
    const bool d = dirty_;
    dirty_ = false;
    return d;
}

// ── навігація ──────────────────────────────────────────────────

uint8_t MenuEngine::list_count() const {
    if (stack_.empty()) {
        return static_cast<uint8_t>(data_.root_count + 1);  // + "назад"
    }
    return static_cast<uint8_t>(data_.nodes[stack_.back().node].child_count + 1);
}

uint8_t MenuEngine::child_node(uint8_t i) const {
    if (stack_.empty()) {
        return static_cast<uint8_t>(data_.root_first + i);
    }
    return static_cast<uint8_t>(data_.nodes[stack_.back().node].first_child + i);
}

void MenuEngine::nav_up() {
    if (cursor_ > 0) --cursor_;
    if (cursor_ < scroll_) scroll_ = cursor_;
}

void MenuEngine::nav_down() {
    if (cursor_ + 1 < list_count()) ++cursor_;
    if (cursor_ >= scroll_ + VISIBLE_ITEMS) {
        scroll_ = static_cast<uint8_t>(cursor_ - VISIBLE_ITEMS + 1);
    }
}

void MenuEngine::nav_select() {
    const uint8_t items = static_cast<uint8_t>(list_count() - 1);

    if (cursor_ >= items) {
        // Віртуальний пункт "назад"
        if (stack_.empty()) {
            to_main();
        } else {
            const Level lvl = stack_.back();
            stack_.pop_back();
            cursor_ = lvl.cursor;
            scroll_ = lvl.scroll;
        }
        return;
    }

    const uint8_t idx = child_node(cursor_);
    const DisplayMenuNode& n = data_.nodes[idx];

    switch (n.type) {
    case DisplayItemType::SUBMENU:
        if (stack_.size() < MAX_DEPTH) {
            stack_.push_back({idx, cursor_, scroll_});
            cursor_ = 0;
            scroll_ = 0;
        }
        break;
    case DisplayItemType::VALUE:
        break;  // read-only — нічого
    default:
        begin_edit(idx);
        break;
    }
}

void MenuEngine::to_main() {
    screen_ = Screen::MAIN;
    stack_.clear();
    cursor_ = 0;
    scroll_ = 0;
    main_page_ = 0;
    rotate_acc_ = 0;
    idle_acc_ = 0;
}

// ── редагування ────────────────────────────────────────────────

void MenuEngine::begin_edit(uint8_t node_idx) {
    const DisplayMenuNode& n = data_.nodes[node_idx];
    edit_node_ = node_idx;
    edit_f_ = 0.0f;
    edit_opt_ = 0;

    auto v = io_.get(n.key);

    switch (n.type) {
    case DisplayItemType::EDIT_FLOAT:
    case DisplayItemType::EDIT_INT:
        if (v.has_value()) {
            if (const auto* f = etl::get_if<float>(&v.value()))   edit_f_ = *f;
            if (const auto* i = etl::get_if<int32_t>(&v.value())) edit_f_ = static_cast<float>(*i);
        }
        edit_f_ = clampf(edit_f_, n.min, n.max);
        break;

    case DisplayItemType::EDIT_BOOL:
        if (v.has_value()) {
            if (const auto* b = etl::get_if<bool>(&v.value())) edit_opt_ = *b ? 1 : 0;
        }
        break;

    case DisplayItemType::EDIT_ENUM: {
        int32_t cur = 0;
        if (v.has_value()) {
            if (const auto* i = etl::get_if<int32_t>(&v.value())) cur = *i;
        }
        for (uint8_t i = 0; i < n.option_count; ++i) {
            if (n.options[i].value == cur) {
                edit_opt_ = i;
                break;
            }
        }
        break;
    }
    default:
        return;
    }

    screen_ = Screen::EDIT;
}

void MenuEngine::edit_adjust(int8_t dir) {
    const DisplayMenuNode& n = data_.nodes[edit_node_];

    switch (n.type) {
    case DisplayItemType::EDIT_FLOAT:
    case DisplayItemType::EDIT_INT:
        edit_f_ = clampf(edit_f_ + dir * n.step, n.min, n.max);
        break;
    case DisplayItemType::EDIT_BOOL:
        edit_opt_ = edit_opt_ ? 0 : 1;
        break;
    case DisplayItemType::EDIT_ENUM:
        if (n.option_count > 0) {
            edit_opt_ = static_cast<uint8_t>(
                (edit_opt_ + n.option_count + dir) % n.option_count);
        }
        break;
    default:
        break;
    }
}

void MenuEngine::edit_save() {
    const DisplayMenuNode& n = data_.nodes[edit_node_];

    switch (n.type) {
    case DisplayItemType::EDIT_FLOAT:
        io_.set(n.key, StateValue(edit_f_));
        break;
    case DisplayItemType::EDIT_INT:
        io_.set(n.key, StateValue(static_cast<int32_t>(lroundf(edit_f_))));
        break;
    case DisplayItemType::EDIT_BOOL:
        io_.set(n.key, StateValue(edit_opt_ != 0));
        break;
    case DisplayItemType::EDIT_ENUM:
        if (n.options && edit_opt_ < n.option_count) {
            io_.set(n.key, StateValue(n.options[edit_opt_].value));
        }
        break;
    default:
        break;
    }

    screen_ = Screen::MENU;
}

// ── кадр ───────────────────────────────────────────────────────

void MenuEngine::format_value(const DisplayMenuNode& n, etl::istring& out) const {
    auto v = io_.get(n.key);
    if (!v.has_value()) {
        append_utf8(out, "---");
        return;
    }
    const StateValue& val = v.value();
    char buf[32] = {0};

    // bool/enum з options — показуємо label
    if (n.options && n.option_count > 0) {
        int32_t iv = 0;
        if      (const auto* b = etl::get_if<bool>(&val))    iv = *b ? 1 : 0;
        else if (const auto* i = etl::get_if<int32_t>(&val)) iv = *i;
        else if (const auto* f = etl::get_if<float>(&val))   iv = static_cast<int32_t>(*f);
        for (uint8_t i = 0; i < n.option_count; ++i) {
            if (n.options[i].value == iv) {
                append_utf8(out, n.options[i].label);
                return;
            }
        }
        snprintf(buf, sizeof(buf), "%d", static_cast<int>(iv));
        append_utf8(out, buf);
        return;
    }

    if (const auto* f = etl::get_if<float>(&val)) {
        snprintf(buf, sizeof(buf), n.format, static_cast<double>(*f));
    } else if (const auto* i = etl::get_if<int32_t>(&val)) {
        snprintf(buf, sizeof(buf), n.format, static_cast<int>(*i));
    } else if (const auto* b = etl::get_if<bool>(&val)) {
        snprintf(buf, sizeof(buf), "%s", *b ? "ON" : "OFF");
    } else if (const auto* s = etl::get_if<StringValue>(&val)) {
        snprintf(buf, sizeof(buf), "%s", s->c_str());
    }
    append_utf8(out, buf);

    if (n.unit[0]) {
        append_utf8(out, " ");
        append_utf8(out, n.unit);
    }
}

size_t MenuEngine::main_pages() const {
    if (data_.main_count == 0) return 1;
    return (data_.main_count + VISIBLE_ITEMS - 1) / VISIBLE_ITEMS;
}

void MenuEngine::build_main(DisplayFrame& f) const {
    if (data_.main_count == 0) {
        f.rows[0] = "ModESP";
        if (data_.root_count > 0) f.rows[DisplayFrame::MAX_ROWS - 1] = "[OK] меню";
        return;
    }

    const size_t first = static_cast<size_t>(main_page_) * VISIBLE_ITEMS;
    size_t row = 0;
    for (size_t i = first; i < data_.main_count && row < VISIBLE_ITEMS; ++i, ++row) {
        const gen::DisplayMainValue& mv = data_.main_values[i];
        etl::istring& line = f.rows[row];
        append_utf8(line, mv.label);
        append_utf8(line, " ");

        char buf[32] = {0};
        auto v = io_.get(mv.key);
        if (v.has_value()) {
            const StateValue& val = v.value();
            if      (const auto* fv = etl::get_if<float>(&val))
                snprintf(buf, sizeof(buf), mv.format, static_cast<double>(*fv));
            else if (const auto* iv = etl::get_if<int32_t>(&val))
                snprintf(buf, sizeof(buf), mv.format, static_cast<int>(*iv));
            else if (const auto* bv = etl::get_if<bool>(&val))
                snprintf(buf, sizeof(buf), "%s", *bv ? "ON" : "OFF");
            else if (const auto* sv = etl::get_if<StringValue>(&val))
                snprintf(buf, sizeof(buf), "%s", sv->c_str());
        } else {
            snprintf(buf, sizeof(buf), "---");
        }
        append_utf8(line, buf);
    }

    if (data_.root_count > 0) {
        f.rows[DisplayFrame::MAX_ROWS - 1] = "[OK] меню";
    }
}

void MenuEngine::build_menu(DisplayFrame& f) const {
    // Заголовок: назва підменю або "Меню" на root-рівні
    if (stack_.empty()) {
        f.rows[0] = "== Меню ==";
    } else {
        append_utf8(f.rows[0], "== ");
        append_utf8(f.rows[0], data_.nodes[stack_.back().node].label);
        append_utf8(f.rows[0], " ==");
    }

    const uint8_t count = list_count();
    const uint8_t items = static_cast<uint8_t>(count - 1);

    size_t row = 1;
    for (uint8_t i = scroll_; i < count && row < DisplayFrame::MAX_ROWS; ++i, ++row) {
        etl::istring& line = f.rows[row];
        append_utf8(line, i == cursor_ ? ">" : " ");

        if (i >= items) {
            append_utf8(line, stack_.empty() ? "< Вихід" : "< Назад");
            continue;
        }

        const DisplayMenuNode& n = data_.nodes[child_node(i)];
        append_utf8(line, n.label);

        if (n.type != DisplayItemType::SUBMENU) {
            append_utf8(line, ": ");
            format_value(n, line);
        }
    }
}

void MenuEngine::build_edit(DisplayFrame& f) const {
    const DisplayMenuNode& n = data_.nodes[edit_node_];
    char buf[32] = {0};

    append_utf8(f.rows[0], "= ");
    append_utf8(f.rows[0], n.label);
    append_utf8(f.rows[0], " =");

    etl::istring& value_row = f.rows[1];
    append_utf8(value_row, "> ");
    switch (n.type) {
    case DisplayItemType::EDIT_FLOAT:
        snprintf(buf, sizeof(buf), n.format, static_cast<double>(edit_f_));
        append_utf8(value_row, buf);
        break;
    case DisplayItemType::EDIT_INT:
        snprintf(buf, sizeof(buf), n.format,
                 static_cast<int>(lroundf(edit_f_)));
        append_utf8(value_row, buf);
        break;
    case DisplayItemType::EDIT_BOOL:
    case DisplayItemType::EDIT_ENUM:
        if (n.options && edit_opt_ < n.option_count) {
            append_utf8(value_row, n.options[edit_opt_].label);
        } else {
            append_utf8(value_row, edit_opt_ ? "ON" : "OFF");
        }
        break;
    default:
        break;
    }
    if (n.unit[0]) {
        append_utf8(value_row, " ");
        append_utf8(value_row, n.unit);
    }

    // Діапазон для числових
    if (n.type == DisplayItemType::EDIT_FLOAT || n.type == DisplayItemType::EDIT_INT) {
        char range[48];
        snprintf(range, sizeof(range), "%g..%g /%g",
                 static_cast<double>(n.min), static_cast<double>(n.max),
                 static_cast<double>(n.step));
        append_utf8(f.rows[2], range);
    }

    f.rows[DisplayFrame::MAX_ROWS - 1] = "[OK] зберегти";
}

void MenuEngine::update_screen_name() {
    screen_name_.clear();
    switch (screen_) {
    case Screen::MAIN:
        screen_name_ = "main";
        break;
    case Screen::MENU:
        append_utf8(screen_name_, "menu:");
        append_utf8(screen_name_,
                    stack_.empty() ? "root" : data_.nodes[stack_.back().node].label);
        break;
    case Screen::EDIT:
        append_utf8(screen_name_, "edit:");
        append_utf8(screen_name_, data_.nodes[edit_node_].label);
        break;
    }
}

void MenuEngine::rebuild() {
    DisplayFrame next;
    switch (screen_) {
    case Screen::MAIN: build_main(next); break;
    case Screen::MENU: build_menu(next); break;
    case Screen::EDIT: build_edit(next); break;
    }

    if (next != frame_) {
        frame_ = next;
        dirty_ = true;
    }
    update_screen_name();
}

} // namespace modesp::display
