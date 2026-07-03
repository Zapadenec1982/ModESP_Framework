/**
 * @file menu_engine.cpp
 * @brief Реалізація движка екранного меню (віддає семантичні View).
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

// Конверсійний символ printf-формату (після останнього валідного '%').
static char fmt_conv(const char* fmt) {
    char last = 's';
    for (const char* p = fmt; *p; ) {
        if (*p != '%') { ++p; continue; }
        ++p;                               // після '%'
        if (*p == '%') { ++p; continue; }  // %% — літерал
        while (*p && (strchr("-+ #0", *p) || (*p >= '0' && *p <= '9') || *p == '.' || *p == '*')) ++p;
        while (*p && strchr("hljztL", *p)) ++p;
        if (*p) { last = *p; ++p; }
    }
    return last;
}

// mem-1: форматувати StateValue за ФОРМАТОМ (формат = джерело істини типу аргументу),
// а НЕ за рантайм-варіантом — інакше "%.1f" з int-аргументом = varargs-UB.
static void format_state(const char* fmt, const StateValue& val, etl::istring& out) {
    char buf[32] = {0};
    const char c = fmt_conv(fmt);
    const bool isF = (c=='f'||c=='F'||c=='e'||c=='E'||c=='g'||c=='G'||c=='a'||c=='A');
    const bool isI = (c=='d'||c=='i'||c=='u'||c=='x'||c=='X'||c=='o'||c=='c');

    double dv = 0.0; int iv = 0; bool numeric = false;
    if      (const auto* f = etl::get_if<float>(&val))   { dv = *f; iv = static_cast<int>(*f); numeric = true; }
    else if (const auto* i = etl::get_if<int32_t>(&val)) { dv = static_cast<double>(*i); iv = *i; numeric = true; }
    else if (const auto* b = etl::get_if<bool>(&val))    { dv = *b ? 1.0 : 0.0; iv = *b ? 1 : 0; numeric = true; }

    if      (isF && numeric) snprintf(buf, sizeof(buf), fmt, dv);
    else if (isI && numeric) snprintf(buf, sizeof(buf), fmt, iv);
    else if (c == 's') {
        if      (const auto* s = etl::get_if<StringValue>(&val)) snprintf(buf, sizeof(buf), fmt, s->c_str());
        else if (const auto* b = etl::get_if<bool>(&val))        snprintf(buf, sizeof(buf), "%s", *b ? "ON" : "OFF");
        else                                                     snprintf(buf, sizeof(buf), "%s", "?");
    } else {
        snprintf(buf, sizeof(buf), "?");
    }
    append_utf8(out, buf);
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
    refresh_acc_ = 0;   // menu-2: уникнути зайвого rebuild за лічені мс після події

    switch (screen_) {
    case Screen::MAIN:
        if (ev == MenuEvent::SELECT && data_.root_count > 0) {
            screen_ = Screen::MENU;
            stack_.clear();
            cursor_ = 0;
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

    // Періодичне оновлення значень на екрані (live-дані)
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

void MenuEngine::set_caps(const DisplayCaps& caps) {
    data_.caps = caps;   // склад меню гейтиться capability (ADR-003 §3.3)
    rebuild();
}

// ── навігація ──────────────────────────────────────────────────

// Вузол видимий, якщо немає cap-вимоги або backend має відповідну можливість.
// node_caps == nullptr (фікстури тестів) → гейтингу немає.
bool MenuEngine::cap_visible_(uint8_t node_idx) const {
    if (!data_.node_caps) return true;
    switch (data_.node_caps[node_idx]) {
    case gen::DisplayCap::NONE:         return true;
    case gen::DisplayCap::COLOR:        return data_.caps.has_color;
    case gen::DisplayCap::BACKLIGHT:    return data_.caps.has_backlight;
    case gen::DisplayCap::VIDEO_PARAMS: return data_.caps.has_video_params;
    case gen::DisplayCap::INPUTS:       return data_.caps.has_inputs;
    case gen::DisplayCap::BACKDROP:     return data_.caps.has_backdrop;
    case gen::DisplayCap::POWER:        return data_.caps.has_power;
    }
    return true;
}

uint8_t MenuEngine::list_count() const {
    const uint8_t base = stack_.empty()
        ? data_.root_first : static_cast<uint8_t>(data_.nodes[stack_.back().node].first_child);
    const uint8_t raw = stack_.empty()
        ? data_.root_count : data_.nodes[stack_.back().node].child_count;
    uint8_t vis = 0;
    for (uint8_t i = 0; i < raw; ++i)
        if (cap_visible_(static_cast<uint8_t>(base + i))) ++vis;
    return static_cast<uint8_t>(vis + 1);  // + "назад"
}

uint8_t MenuEngine::child_node(uint8_t i) const {
    const uint8_t base = stack_.empty()
        ? data_.root_first : static_cast<uint8_t>(data_.nodes[stack_.back().node].first_child);
    const uint8_t raw = stack_.empty()
        ? data_.root_count : data_.nodes[stack_.back().node].child_count;
    uint8_t vis = 0;
    for (uint8_t k = 0; k < raw; ++k) {
        const uint8_t idx = static_cast<uint8_t>(base + k);
        if (!cap_visible_(idx)) continue;   // пропустити прихований capability-вузол
        if (vis == i) return idx;
        ++vis;
    }
    return base;  // fallback (для i < list_count()-1 не досягається)
}

void MenuEngine::nav_up() {
    if (cursor_ > 0) --cursor_;
}

void MenuEngine::nav_down() {
    // menu-1: курсор не виходить за рендеровний набір (View усікається до MAX_MENU_ITEMS).
    uint8_t last = static_cast<uint8_t>(list_count() - 1);
    if (last >= MAX_MENU_ITEMS) last = MAX_MENU_ITEMS - 1;
    if (cursor_ < last) ++cursor_;
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
        }
        return;
    }

    const uint8_t idx = child_node(cursor_);
    const DisplayMenuNode& n = data_.nodes[idx];

    switch (n.type) {
    case DisplayItemType::SUBMENU:
        if (stack_.size() < MAX_DEPTH) {
            stack_.push_back({idx, cursor_});
            cursor_ = 0;
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
    idle_acc_ = 0;
}

// ── редагування ────────────────────────────────────────────────

void MenuEngine::begin_edit(uint8_t node_idx) {
    const DisplayMenuNode& n = data_.nodes[node_idx];
    edit_node_ = node_idx;
    edit_f_ = 0.0f;
    edit_opt_ = 0;
    edit_dirty_ = false;
    edit_known_ = true;

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
        edit_known_ = false;
        for (uint8_t i = 0; i < n.option_count; ++i) {
            if (n.options[i].value == cur) {
                edit_opt_ = i;
                edit_known_ = true;
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
    edit_dirty_ = true;

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
        // editenum-1: не затирати невідоме значення, якщо користувач нічого не змінив
        if ((edit_dirty_ || edit_known_) && n.options && edit_opt_ < n.option_count) {
            io_.set(n.key, StateValue(n.options[edit_opt_].value));
        }
        break;
    default:
        break;
    }

    screen_ = Screen::MENU;
}

// ── побудова View ──────────────────────────────────────────────

void MenuEngine::format_value(const DisplayMenuNode& n, etl::istring& out) const {
    auto v = io_.get(n.key);
    if (!v.has_value()) {
        append_utf8(out, "---");
        return;
    }
    const StateValue& val = v.value();

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
        char buf[12];
        snprintf(buf, sizeof(buf), "%d", static_cast<int>(iv));
        append_utf8(out, buf);
        return;
    }

    format_state(n.format, val, out);

    if (n.unit[0]) {
        append_utf8(out, " ");
        append_utf8(out, n.unit);
    }
}

void MenuEngine::build_main(MainView& v) const {
    v.items.clear();
    v.has_menu = (data_.root_count > 0);

    for (size_t i = 0; i < data_.main_count && v.items.size() < MAX_MAIN_ITEMS; ++i) {
        const gen::DisplayMainValue& mv = data_.main_values[i];
        MainItem it;
        append_utf8(it.label, mv.label);

        auto val = io_.get(mv.key);
        if (val.has_value()) format_state(mv.format, val.value(), it.value);  // mem-1
        else                 append_utf8(it.value, "---");
        v.items.push_back(it);
    }
}

void MenuEngine::build_menu(MenuView& v) const {
    v.items.clear();
    if (stack_.empty()) {
        append_utf8(v.title, "Меню");
    } else {
        append_utf8(v.title, data_.nodes[stack_.back().node].label);
    }

    const uint8_t count = list_count();
    const uint8_t items = static_cast<uint8_t>(count - 1);

    for (uint8_t i = 0; i < count && v.items.size() < MAX_MENU_ITEMS; ++i) {
        MenuItem it;
        if (i >= items) {
            it.is_back = true;
            append_utf8(it.label, stack_.empty() ? "Вихід" : "Назад");
        } else {
            const DisplayMenuNode& n = data_.nodes[child_node(i)];
            append_utf8(it.label, n.label);
            if (n.type == DisplayItemType::SUBMENU) {
                it.is_submenu = true;
            } else {
                format_value(n, it.value);
            }
        }
        v.items.push_back(it);
    }

    v.selected = cursor_;
}

void MenuEngine::build_edit(EditView& v) const {
    const DisplayMenuNode& n = data_.nodes[edit_node_];
    char buf[32] = {0};

    append_utf8(v.title, n.label);

    switch (n.type) {
    case DisplayItemType::EDIT_FLOAT:
        snprintf(buf, sizeof(buf), n.format, static_cast<double>(edit_f_));
        append_utf8(v.value, buf);
        break;
    case DisplayItemType::EDIT_INT:
        snprintf(buf, sizeof(buf), n.format, static_cast<int>(lroundf(edit_f_)));
        append_utf8(v.value, buf);
        break;
    case DisplayItemType::EDIT_BOOL:
    case DisplayItemType::EDIT_ENUM:
        if (n.options && edit_opt_ < n.option_count) {
            append_utf8(v.value, n.options[edit_opt_].label);
        } else {
            append_utf8(v.value, edit_opt_ ? "ON" : "OFF");
        }
        break;
    default:
        break;
    }
    if (n.unit[0]) {
        append_utf8(v.value, " ");
        append_utf8(v.value, n.unit);
    }

    if (n.type == DisplayItemType::EDIT_FLOAT || n.type == DisplayItemType::EDIT_INT) {
        char range[48];
        snprintf(range, sizeof(range), "%g..%g /%g",
                 static_cast<double>(n.min), static_cast<double>(n.max),
                 static_cast<double>(n.step));
        append_utf8(v.range, range);
    }
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
    bool changed = (screen_ != built_screen_);
    built_screen_ = screen_;

    switch (screen_) {
    case Screen::MAIN: {
        MainView nv;
        build_main(nv);
        if (changed || nv != main_view_) { main_view_ = nv; changed = true; }
        break;
    }
    case Screen::MENU: {
        MenuView nv;
        build_menu(nv);
        if (changed || nv != menu_view_) { menu_view_ = nv; changed = true; }
        break;
    }
    case Screen::EDIT: {
        EditView nv;
        build_edit(nv);
        if (changed || nv != edit_view_) { edit_view_ = nv; changed = true; }
        break;
    }
    }

    if (changed) dirty_ = true;
    update_screen_name();
}

} // namespace modesp::display
