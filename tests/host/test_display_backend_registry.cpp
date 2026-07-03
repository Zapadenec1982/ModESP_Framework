/**
 * @file test_display_backend_registry.cpp
 * @brief HOST tests for DriverRegistry display-backend dispatch
 *        (type-string → factory; колишній module-local DisplayBackendRegistry).
 *
 * Чиста логіка реєстру. Фабрики backend-ів самі — device-only, тож тут
 * перевіряємо реєстр через фейк-фабрику.
 */

#include "doctest.h"
#include "modesp/hal/driver_registry.h"
#include "modesp/hal/display_port.h"
#include "modesp/hal/hal.h"        // modesp::Binding, modesp::HAL (mocked на host)

using namespace modesp::display;
using modesp::DriverRegistry;

namespace {

/// Мінімальний IDisplayPort — лише чисті віртуали.
class FakePort : public IDisplayPort {
public:
    void present_main(const MainView&) override {}
    void present_menu(const MenuView&) override {}
    void present_edit(const EditView&) override {}
    void present_notice(const Notice&) override {}
    void clear_notice() override {}
};

FakePort g_port_a;
FakePort g_port_b;

IDisplayPort* factory_a(const modesp::Binding&, modesp::HAL&) { return &g_port_a; }
IDisplayPort* factory_b(const modesp::Binding&, modesp::HAL&) { return &g_port_b; }
IDisplayPort* factory_null(const modesp::Binding&, modesp::HAL&) { return nullptr; }

} // namespace

TEST_CASE("display registry: register + has + create dispatch") {
    DriverRegistry::reset();
    CHECK(DriverRegistry::register_display("amt630a", &factory_a));
    CHECK(DriverRegistry::register_display("ssd1306", &factory_b));

    CHECK(DriverRegistry::has_display("amt630a"));
    CHECK(DriverRegistry::has_display("ssd1306"));
    CHECK_FALSE(DriverRegistry::has_display("at7456e"));
    // display-типи НЕ видимі для sensor/actuator-шляху DriverManager-а
    CHECK_FALSE(DriverRegistry::is_known("amt630a"));
    CHECK(DriverRegistry::is_module_backend("amt630a"));

    modesp::Binding b;
    modesp::HAL hal;
    CHECK(DriverRegistry::create_display("amt630a", b, hal) == &g_port_a);
    CHECK(DriverRegistry::create_display("ssd1306", b, hal) == &g_port_b);
}

TEST_CASE("display registry: unknown type → nullptr, not known") {
    DriverRegistry::reset();
    DriverRegistry::register_display("amt630a", &factory_a);

    modesp::Binding b;
    modesp::HAL hal;
    CHECK(DriverRegistry::create_display("nope", b, hal) == nullptr);
    CHECK_FALSE(DriverRegistry::has_display("nope"));
    CHECK(DriverRegistry::create_display(nullptr, b, hal) == nullptr);
}

TEST_CASE("display registry: registration is idempotent (first factory wins)") {
    DriverRegistry::reset();
    CHECK(DriverRegistry::register_display("amt630a", &factory_a));
    // Повторна реєстрація того ж типу — no-op (true), фабрика не підмінюється.
    CHECK(DriverRegistry::register_display("amt630a", &factory_b));

    modesp::Binding b;
    modesp::HAL hal;
    CHECK(DriverRegistry::create_display("amt630a", b, hal) == &g_port_a);
}

TEST_CASE("display registry: null args rejected") {
    DriverRegistry::reset();
    CHECK_FALSE(DriverRegistry::register_display(nullptr, &factory_a));
    CHECK_FALSE(DriverRegistry::register_display("x", nullptr));
}

TEST_CASE("display registry: factory returning nullptr propagates") {
    DriverRegistry::reset();
    DriverRegistry::register_display("broken", &factory_null);
    modesp::Binding b;
    modesp::HAL hal;
    CHECK(DriverRegistry::has_display("broken"));                       // тип відомий
    CHECK(DriverRegistry::create_display("broken", b, hal) == nullptr); // але фабрика не змогла
}

TEST_CASE("display registry: capacity bound + reset") {
    DriverRegistry::reset();
    // Реєстр зберігає const char* (string-літерали з MODESP_REGISTER_DISPLAY),
    // тож імена мають бути окремими стабільними рядками (не один буфер).
    static const char* names[] = {
        "b0","b1","b2","b3","b4","b5","b6","b7",
        "b8","b9","b10","b11","b12","b13","b14","b15",
    };
    REQUIRE(DriverRegistry::MAX_DRIVER_TYPES <= 16);
    for (size_t i = 0; i < DriverRegistry::MAX_DRIVER_TYPES; ++i) {
        CHECK(DriverRegistry::register_display(names[i], &factory_a));
    }
    // Понад місткість — відмова (не мовчазне переповнення).
    CHECK_FALSE(DriverRegistry::register_display("overflow", &factory_a));

    DriverRegistry::reset();
    CHECK_FALSE(DriverRegistry::has_display("b0"));
}
