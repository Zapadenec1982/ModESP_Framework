/**
 * @file display_module.h
 * @brief Display module — екранне меню, згенероване з маніфестів.
 *
 * Читає віртуальні кнопки display.btn_up/down/select зі SharedState
 * (їх пише WebUI, MQTT або драйвер фізичних кнопок через bindings),
 * віддає події в MenuEngine і штовхає СЕМАНТИЧНІ View у IDisplayPort.
 *
 * Без заліза використовується LogPort (View у серійний лог).
 * Backend дисплея підключається через set_port() / default_port() (ADR-002).
 */

#pragma once

#include "modesp/base_module.h"
#include "display/menu_engine.h"
#include "display/display_port.h"
#include "display/notification_queue.h"

namespace modesp { struct BindingTable; class HAL; }

class DisplayModule : public modesp::BaseModule,
                      private modesp::display::IMenuStateIO {
public:
    DisplayModule();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_message(const etl::imessage& msg) override;

    /// Підключити backend дисплея (за замовчуванням — LogPort).
    void set_port(modesp::display::IDisplayPort* port);

    /// Резолвити активний backend з bindings.json (role/driver) через
    /// DisplayBackendRegistry; піни — з board.json через HAL. Кличеться у
    /// main.cpp після hal.init (як equipment.bind_drivers). Без binding —
    /// лишається LogPort. Має передувати on_init().
    void bind_display(const modesp::BindingTable& bindings, modesp::HAL& hal);

private:
    // IMenuStateIO — місток до SharedState
    etl::optional<modesp::StateValue> get(const char* key) const override {
        return state_get(key);
    }
    bool set(const char* key, const modesp::StateValue& value) override {
        return state_set(modesp::StateKey(key), value);
    }

    /// Rising edge + самоскидання momentary-кнопки.
    bool poll_button(const char* key, bool& prev);

    /// Передати активний View поточного екрана у port_.
    void present_current();

    /// A2: маршрутизувати параметри екрана зі SharedState у port_ (за caps).
    void apply_screen_params();

    modesp::display::MenuEngine        engine_;
    modesp::display::IDisplayPort*     port_ = nullptr;
    modesp::display::NotificationQueue notif_;
    modesp::display::DisplayCaps       caps_{};

    // A2: останні застосовані значення параметрів (виявлення зміни); -1 = ще не задано
    int32_t last_backlight_  = -1;
    int32_t last_contrast_   = -1;
    int32_t last_brightness_ = -1;
    int32_t last_saturation_ = -1;
    int32_t last_input_      = -1;

    bool prev_up_     = false;
    bool prev_down_   = false;
    bool prev_select_ = false;

    etl::string<48> last_screen_;
};
