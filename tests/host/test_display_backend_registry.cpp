/**
 * @file test_display_backend_registry.cpp
 * @brief HOST tests for DisplayBackendRegistry (type-string → factory dispatch).
 *
 * Чиста логіка реєстру (як DriverRegistry). Фабрики backend-ів самі —
 * device-only (#ifdef CONFIG_*), тож тут перевіряємо реєстр через фейк-фабрику.
 */

#include "doctest.h"
#include "display/display_backend_registry.h"
#include "modesp/hal/hal.h"        // modesp::Binding, modesp::HAL (mocked на host)

using namespace modesp::display;

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

TEST_CASE("registry: register + is_known + create dispatch") {
    DisplayBackendRegistry::reset();
    CHECK(DisplayBackendRegistry::register_backend("amt630a", &factory_a));
    CHECK(DisplayBackendRegistry::register_backend("ssd1306", &factory_b));

    CHECK(DisplayBackendRegistry::is_known("amt630a"));
    CHECK(DisplayBackendRegistry::is_known("ssd1306"));
    CHECK_FALSE(DisplayBackendRegistry::is_known("at7456e"));

    modesp::Binding b;
    modesp::HAL hal;
    CHECK(DisplayBackendRegistry::create("amt630a", b, hal) == &g_port_a);
    CHECK(DisplayBackendRegistry::create("ssd1306", b, hal) == &g_port_b);
}

TEST_CASE("registry: unknown type → nullptr, not known") {
    DisplayBackendRegistry::reset();
    DisplayBackendRegistry::register_backend("amt630a", &factory_a);

    modesp::Binding b;
    modesp::HAL hal;
    CHECK(DisplayBackendRegistry::create("nope", b, hal) == nullptr);
    CHECK_FALSE(DisplayBackendRegistry::is_known("nope"));
    CHECK(DisplayBackendRegistry::create(nullptr, b, hal) == nullptr);
}

TEST_CASE("registry: registration is idempotent (first factory wins)") {
    DisplayBackendRegistry::reset();
    CHECK(DisplayBackendRegistry::register_backend("amt630a", &factory_a));
    // Повторна реєстрація того ж типу — no-op (true), фабрика не підмінюється.
    CHECK(DisplayBackendRegistry::register_backend("amt630a", &factory_b));

    modesp::Binding b;
    modesp::HAL hal;
    CHECK(DisplayBackendRegistry::create("amt630a", b, hal) == &g_port_a);
}

TEST_CASE("registry: null args rejected") {
    DisplayBackendRegistry::reset();
    CHECK_FALSE(DisplayBackendRegistry::register_backend(nullptr, &factory_a));
    CHECK_FALSE(DisplayBackendRegistry::register_backend("x", nullptr));
}

TEST_CASE("registry: factory returning nullptr propagates (create can fail)") {
    DisplayBackendRegistry::reset();
    DisplayBackendRegistry::register_backend("broken", &factory_null);
    modesp::Binding b;
    modesp::HAL hal;
    CHECK(DisplayBackendRegistry::is_known("broken"));        // тип відомий
    CHECK(DisplayBackendRegistry::create("broken", b, hal) == nullptr);  // але фабрика не змогла
}

TEST_CASE("registry: capacity bound + reset") {
    DisplayBackendRegistry::reset();
    // Реєстр зберігає const char* (string-літерали з MODESP_REGISTER_DISPLAY),
    // тож імена мають бути окремими стабільними рядками (не один буфер).
    static const char* names[] = {
        "b0","b1","b2","b3","b4","b5","b6","b7",
        "b8","b9","b10","b11","b12","b13","b14","b15",
    };
    REQUIRE(DisplayBackendRegistry::MAX_BACKENDS <= 16);
    for (size_t i = 0; i < DisplayBackendRegistry::MAX_BACKENDS; ++i) {
        CHECK(DisplayBackendRegistry::register_backend(names[i], &factory_a));
    }
    // Понад місткість — відмова (не мовчазне переповнення).
    CHECK_FALSE(DisplayBackendRegistry::register_backend("overflow", &factory_a));

    DisplayBackendRegistry::reset();
    CHECK_FALSE(DisplayBackendRegistry::is_known("b0"));
}
