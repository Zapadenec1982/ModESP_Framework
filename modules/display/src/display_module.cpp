/**
 * @file display_module.cpp
 * @brief Display module — glue між SharedState, MenuEngine і рендерером
 */

#include "display_module.h"
#include "esp_log.h"

static const char* TAG = "Display";

namespace {

/// Рендерер за замовчуванням — кадр у серійний лог (робота без заліза).
class LogRenderer : public modesp::display::IDisplayRenderer {
public:
    void render(const modesp::display::DisplayFrame& frame) override {
        ESP_LOGI(TAG, "+----------------------+");
        for (const auto& row : frame.rows) {
            ESP_LOGI(TAG, "| %s", row.c_str());
        }
        ESP_LOGI(TAG, "+----------------------+");
    }
};

LogRenderer s_log_renderer;

} // namespace

DisplayModule::DisplayModule()
    : BaseModule("display", modesp::ModulePriority::LOW)
    , engine_(*this, modesp::display::MenuData{
          modesp::gen::MENU_NODES,
          modesp::gen::MENU_NODES_COUNT,
          modesp::gen::MENU_ROOT_FIRST,
          modesp::gen::MENU_ROOT_COUNT,
          modesp::gen::MAIN_VALUES,
          modesp::gen::MAIN_VALUES_COUNT,
      })
    , renderer_(&s_log_renderer)
{}

void DisplayModule::set_renderer(modesp::display::IDisplayRenderer* renderer) {
    renderer_ = renderer ? renderer : &s_log_renderer;
}

bool DisplayModule::on_init() {
    state_set("display.screen", "main");
    state_set("display.btn_up", false);
    state_set("display.btn_down", false);
    state_set("display.btn_select", false);

    if (!renderer_->init()) {
        ESP_LOGW(TAG, "Renderer init failed — display disabled");
        return true;  // не валимо систему через дисплей
    }

    ESP_LOGI(TAG, "Initialized (%u menu nodes, %u main values)",
             static_cast<unsigned>(modesp::gen::MENU_NODES_COUNT),
             static_cast<unsigned>(modesp::gen::MAIN_VALUES_COUNT));
    return true;
}

bool DisplayModule::poll_button(const char* key, bool& prev) {
    const bool cur = read_bool(key, false);
    const bool edge = cur && !prev;
    prev = cur;
    if (cur) {
        state_set(key, false);  // momentary: самоскидання після обробки
    }
    return edge;
}

void DisplayModule::on_update(uint32_t dt_ms) {
    if (!read_bool("display.enabled", true)) {
        return;
    }

    using modesp::display::MenuEvent;

    if (poll_button("display.btn_up", prev_up_)) {
        engine_.handle_event(MenuEvent::UP);
    }
    if (poll_button("display.btn_down", prev_down_)) {
        engine_.handle_event(MenuEvent::DOWN);
    }
    if (poll_button("display.btn_select", prev_select_)) {
        engine_.handle_event(MenuEvent::SELECT);
    }

    engine_.tick(dt_ms);

    if (engine_.consume_dirty()) {
        renderer_->render(engine_.frame());
    }

    if (last_screen_ != engine_.screen_name()) {
        last_screen_ = engine_.screen_name();
        state_set("display.screen", last_screen_.c_str());
    }
}
