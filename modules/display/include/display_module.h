/**
 * @file display_module.h
 * @brief Display module — екранне меню, згенероване з маніфестів
 *
 * Читає віртуальні кнопки display.btn_up/down/select зі SharedState
 * (їх пише WebUI, MQTT або драйвер фізичних кнопок через bindings),
 * віддає події в MenuEngine і рендерить кадр через IDisplayRenderer.
 *
 * Без заліза використовується LogRenderer (кадр у серійний лог).
 * Драйвер дисплея підключається через set_renderer().
 */

#pragma once

#include "modesp/base_module.h"
#include "display/menu_engine.h"
#include "display/renderer.h"

class DisplayModule : public modesp::BaseModule,
                      private modesp::display::IMenuStateIO {
public:
    DisplayModule();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

    /// Підключити драйвер дисплея (за замовчуванням — LogRenderer).
    void set_renderer(modesp::display::IDisplayRenderer* renderer);

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

    modesp::display::MenuEngine        engine_;
    modesp::display::IDisplayRenderer* renderer_ = nullptr;

    bool prev_up_     = false;
    bool prev_down_   = false;
    bool prev_select_ = false;

    etl::string<48> last_screen_;
};
