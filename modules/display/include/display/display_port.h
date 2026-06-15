/**
 * @file display_port.h
 * @brief Семантичний шов display-підсистеми (ADR-002).
 *
 * АБСТРАКТНИЙ модуль (DisplayModule/MenuEngine) штовхає СЕМАНТИЧНИЙ намір
 * (MainView/MenuView/Notice) у IDisplayPort. Драйвер-адаптер (LogPort,
 * At7456ePort, Amt630aPort, Ssd1306Port) сам вирішує layout/колір/масштаб/
 * скрол під своє залізо. Модуль НЕ знає рядків/колонок/пікселів/кольорів/чипа.
 *
 * Zero heap: лише etl-контейнери фіксованого розміру. Безпечно з on_update().
 * Єдиний шов display-підсистеми (legacy IDisplayRenderer/DisplayFrame видалено).
 * Деталі: docs/display/ADR-002-display-architecture.md.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include "etl/string.h"
#include "etl/vector.h"

namespace modesp::display {

// Forward-декларації опційних capability-інтерфейсів (повертаються як вказівники).
class IVideoInputs;
class IGraphicRenderer;

// ── Семантичні enum-и (намір, не presentation) ──

/// Семантична важливість сповіщення (дзеркалить NoticeLevel з ADR-001).
/// Драйвер сам мапить у колір / інверсію / рамку під своє залізо.
enum class NoticeLevel : uint8_t { INFO = 0, WARN = 1, ALARM = 2 };

// ── Доменні View: logical items + семантичні ролі, БЕЗ координат/кольорів/пікселів ──

/// Пункт головного (idle) екрана: назва + відформатоване значення + одиниця.
struct MainItem {
    etl::string<24> label;
    etl::string<16> value;   ///< Уже відформатоване движком (format-рядок з маніфесту).
    etl::string<8>  unit;
};
static constexpr size_t MAX_MAIN_ITEMS = 8;   ///< Логічний максимум, НЕ екранний розмір.

struct MainView {
    etl::vector<MainItem, MAX_MAIN_ITEMS> items;
    bool has_menu = false;   ///< Показати підказку входу в меню.
};

/// Пункт списку меню. value порожнє для SUBMENU/BACK.
struct MenuItem {
    etl::string<24> label;
    etl::string<16> value;
    bool is_submenu = false;
    bool is_back    = false;
};
static constexpr size_t MAX_MENU_ITEMS = 16;  ///< Логічний максимум пунктів одного рівня.

struct MenuView {
    etl::string<24>                       title;
    etl::vector<MenuItem, MAX_MENU_ITEMS> items;
    uint8_t                               selected = 0;  ///< ІНДЕКС пункту, не рядок. Скрол рахує драйвер.
};

/// Екран редагування одного параметра. value/range — вже відформатовані движком.
struct EditView {
    etl::string<24> title;   ///< Назва параметра.
    etl::string<24> value;   ///< Поточне значення + одиниця (відформатоване).
    etl::string<24> range;   ///< Підказка діапазону "5..40 /0.5" або порожньо.
};

/// Банер сповіщення (ADR-001). level — семантичний.
struct Notice {
    NoticeLevel     level = NoticeLevel::INFO;
    etl::string<48> text;
};

/// Можливості backend-а — СЕМАНТИЧНІ: керують СКЛАДОМ меню, не виглядом.
/// НЕ класти сюди cols/rows/width_px — це протікання геометрії (ADR-002 §8).
struct DisplayCaps {
    bool    has_color        = false;  ///< AMT630A: програмована палітра
    bool    has_backlight    = false;  ///< AMT630A: PWM-підсвітка
    bool    has_video_params = false;  ///< AMT630A: video brightness/contrast/saturation
    bool    has_inputs       = false;  ///< AMT630A: вибір CVBS-входу
    uint8_t input_count      = 0;
};

// ── Порівняння View (dirty-detection у MenuEngine + host-тести) ──
inline bool operator==(const MainItem& a, const MainItem& b) {
    return a.label == b.label && a.value == b.value && a.unit == b.unit;
}
inline bool operator!=(const MainItem& a, const MainItem& b) { return !(a == b); }

inline bool operator==(const MainView& a, const MainView& b) {
    return a.has_menu == b.has_menu && a.items == b.items;
}
inline bool operator!=(const MainView& a, const MainView& b) { return !(a == b); }

inline bool operator==(const MenuItem& a, const MenuItem& b) {
    return a.is_submenu == b.is_submenu && a.is_back == b.is_back
        && a.label == b.label && a.value == b.value;
}
inline bool operator!=(const MenuItem& a, const MenuItem& b) { return !(a == b); }

inline bool operator==(const MenuView& a, const MenuView& b) {
    return a.selected == b.selected && a.title == b.title && a.items == b.items;
}
inline bool operator!=(const MenuView& a, const MenuView& b) { return !(a == b); }

inline bool operator==(const EditView& a, const EditView& b) {
    return a.title == b.title && a.value == b.value && a.range == b.range;
}
inline bool operator!=(const EditView& a, const EditView& b) { return !(a == b); }

// ── СЕМАНТИЧНИЙ ШОВ — те, що вміють УСІ backend-и ──
class IDisplayPort {
public:
    virtual ~IDisplayPort() = default;

    /// Ініціалізація заліза. false ⇒ дисплей вимкнено, система продовжує жити.
    virtual bool init() { return true; }

    /// Можливості — модуль читає ОДИН раз для фільтрації складу меню.
    virtual DisplayCaps caps() const { return {}; }

    // present_* — модуль штовхає ПОВНИЙ семантичний стан кадру.
    // Diff / тіньовий буфер — приватна справа драйвера (дельти через шов НЕ передавати).
    virtual void present_main(const MainView& view) = 0;
    virtual void present_menu(const MenuView& view) = 0;
    virtual void present_edit(const EditView& view) = 0;
    virtual void present_notice(const Notice& notice) = 0;
    virtual void clear_notice() = 0;

    // ── Скалярні параметри екрана: no-op default (вміють не всі backend-и) ──
    // Нормалізовано 0..100%; драйвер мапить у свій діапазон. Показ пунктів меню —
    // лише за відповідним caps() (has_backlight / has_video_params).
    virtual void set_backlight(uint8_t pct)  { (void)pct; }
    virtual void set_contrast(uint8_t pct)   { (void)pct; }
    virtual void set_brightness(uint8_t pct) { (void)pct; }
    virtual void set_saturation(uint8_t pct) { (void)pct; }

    // ── Структурно-чужорідні можливості: capability-інтерфейси через as_*()→nullptr
    //    (zero-cost, без RTTI). Реалізує лише той backend, що вміє. ──
    virtual IVideoInputs*     as_video_inputs() { return nullptr; }  ///< CVBS select (AMT630A)
    virtual IGraphicRenderer* as_graphic()      { return nullptr; }  ///< icon/bar  (AMT630A)
};

// ── Опційні capability-інтерфейси (тягнуть власні методи поза базовим vtable) ──

class IVideoInputs {
public:
    virtual ~IVideoInputs() = default;
    virtual void    select_input(uint8_t n) = 0;
    virtual uint8_t input_count() const = 0;
};

class IGraphicRenderer {
public:
    virtual ~IGraphicRenderer() = default;
    virtual void draw_icon(uint8_t win, uint8_t x, uint8_t y, uint16_t tile) = 0;
    virtual void draw_bar (uint8_t win, uint8_t x, uint8_t y, uint8_t pct)  = 0;
};

} // namespace modesp::display
