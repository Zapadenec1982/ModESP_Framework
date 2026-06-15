/**
 * @file display_module.cpp
 * @brief Display module — glue між SharedState, MenuEngine і IDisplayPort.
 */

#include "display_module.h"
#include "modesp/message_types.h"
#include "esp_log.h"

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#ifdef CONFIG_MODESP_DISPLAY_AT7456E
#include "display/at7456e_port.h"
#endif
#ifdef CONFIG_MODESP_DISPLAY_AMT630A
#include "display/amt630a_port.h"
#endif

static const char* TAG = "Display";

namespace {

using namespace modesp::display;

/// Backend за замовчуванням — View у серійний лог (робота без заліза).
class LogPort : public IDisplayPort {
public:
    void present_main(const MainView& v) override {
        ESP_LOGI(TAG, "[MAIN]%s", v.has_menu ? "  (OK=меню)" : "");
        for (const auto& it : v.items) {
            ESP_LOGI(TAG, "  %s  %s%s", it.label.c_str(), it.value.c_str(), it.unit.c_str());
        }
    }
    void present_menu(const MenuView& v) override {
        ESP_LOGI(TAG, "[MENU] %s", v.title.c_str());
        for (size_t i = 0; i < v.items.size(); ++i) {
            const auto& it = v.items[i];
            ESP_LOGI(TAG, " %c %s%s%s",
                     (i == v.selected) ? '>' : ' ',
                     it.label.c_str(),
                     it.value.empty() ? "" : ": ",
                     it.value.c_str());
        }
    }
    void present_edit(const EditView& v) override {
        ESP_LOGI(TAG, "[EDIT] %s = %s   %s", v.title.c_str(), v.value.c_str(), v.range.c_str());
    }
    void present_notice(const Notice& n) override {
        ESP_LOGI(TAG, "[NOTICE %d] %s", static_cast<int>(n.level), n.text.c_str());
    }
    void clear_notice() override {}
};

LogPort s_log_port;

#ifdef CONFIG_MODESP_DISPLAY_AT7456E
At7456ePort s_at7456e_port;
#endif
#ifdef CONFIG_MODESP_DISPLAY_AMT630A
Amt630aPort s_amt630a_port;
#endif

/// Дефолтний backend: обраний у menuconfig (choice), інакше — лог (ADR-002 §2.4).
IDisplayPort* default_port() {
#if defined(CONFIG_MODESP_DISPLAY_AMT630A)
    return &s_amt630a_port;
#elif defined(CONFIG_MODESP_DISPLAY_AT7456E)
    return &s_at7456e_port;
#else
    return &s_log_port;
#endif
}

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
    , port_(default_port())
{}

void DisplayModule::set_port(modesp::display::IDisplayPort* port) {
    port_ = port ? port : &s_log_port;
}

bool DisplayModule::on_init() {
    state_set("display.screen", "main");
    state_set("display.btn_up", false);
    state_set("display.btn_down", false);
    state_set("display.btn_select", false);
    state_set("display.banner", "");
    state_set("display.banner_level", static_cast<int32_t>(0));

    if (!port_->init()) {
        ESP_LOGW(TAG, "Display port init failed — display disabled");
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

void DisplayModule::present_current() {
    using S = modesp::display::MenuEngine::Screen;
    switch (engine_.screen()) {
    case S::MAIN: port_->present_main(engine_.main_view()); break;
    case S::MENU: port_->present_menu(engine_.menu_view()); break;
    case S::EDIT: port_->present_edit(engine_.edit_view()); break;
    }
}

void DisplayModule::on_message(const etl::imessage& msg) {
    // Підписані ВИКЛЮЧНО на UI_NOTICE — рішення «показати» належить продюсеру (ADR-001).
    if (msg.get_message_id() == modesp::msg_id::UI_NOTICE) {
        const auto& n = static_cast<const modesp::MsgUiNotice&>(msg);
        notif_.push(n.level, n.ttl_ms, n.text.c_str());
    }
    BaseModule::on_message(msg);   // arch-3: не «з'їдати» повідомлення (база — no-op, але контракт)
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
    const bool engine_dirty = engine_.consume_dirty();

    notif_.tick(dt_ms);
    const bool notif_dirty = notif_.consume_dirty();

    if (engine_dirty) present_current();

    // module-1: оновити шар банера РІВНО ОДИН раз — якщо змінився банер,
    // або меню перемалювалось і активний банер треба відновити поверх.
    if (notif_dirty || (engine_dirty && notif_.has_active())) {
        if (notif_.has_active()) port_->present_notice(notif_.active());
        else                     port_->clear_notice();
    }
    // Дзеркало display.banner* для WebUI/MQTT — лише при зміні банера.
    if (notif_dirty) {
        if (notif_.has_active()) {
            state_set("display.banner", notif_.active().text.c_str());
            state_set("display.banner_level", static_cast<int32_t>(notif_.active().level));
        } else {
            state_set("display.banner", "");
            state_set("display.banner_level", static_cast<int32_t>(0));
        }
    }

    if (last_screen_ != engine_.screen_name()) {
        last_screen_ = engine_.screen_name();
        state_set("display.screen", last_screen_.c_str());
    }
}
