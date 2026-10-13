/**
 * @file test_module_manager.cpp
 * @brief ModuleManager's deferred stop: on_stop() runs where on_update() runs.
 *
 * The restart, factory-reset, restore and OTA handlers called stop_all() on the httpd task
 * while the main loop went on calling update_all(), and a module's state turns STOPPED only
 * after its on_stop() returns — so a module could be torn down from one task while another
 * was inside its on_update(). request_stop_all() hands the stop to the next update_all(), on
 * the main loop. These cases pin that handshake: nothing stops at the request, everything
 * stops in the next turn in reverse priority, and nothing updates after it.
 */

#include "doctest.h"

#include "modesp/module_manager.h"
#include "modesp/shared_state.h"

#include <string>

namespace {

std::string g_stops;   // on_stop() calls, in order

class Probe : public modesp::BaseModule {
public:
    Probe(const char* name, modesp::ModulePriority p) : BaseModule(name, p) {}
    int updates = 0;
    void on_update(uint32_t) override { ++updates; }
    void on_stop() override { g_stops += name(); g_stops += ' '; }
};

}  // namespace

TEST_CASE("a requested stop runs in the next update_all, in reverse priority, and only once") {
    g_stops.clear();
    modesp::SharedState   state;
    modesp::ModuleManager modules;
    Probe low("low", modesp::ModulePriority::LOW);
    Probe crit("crit", modesp::ModulePriority::CRITICAL);
    Probe high("high", modesp::ModulePriority::HIGH);
    modules.register_module(low);
    modules.register_module(crit);
    modules.register_module(high);
    REQUIRE(modules.init_all(state));

    modules.update_all(10);
    CHECK(low.updates == 1);
    CHECK(crit.updates == 1);
    CHECK(high.updates == 1);

    // The request itself stops nothing: it may come from any task.
    modules.request_stop_all();
    CHECK(g_stops.empty());
    CHECK_FALSE(modules.stop_all_done());
    CHECK(low.state() == modesp::BaseModule::State::RUNNING);

    // The next turn is the stop, and nothing updates in it.
    modules.update_all(10);
    CHECK(g_stops == "low high crit ");
    CHECK(modules.stop_all_done());
    CHECK(low.updates == 1);
    CHECK(crit.updates == 1);
    CHECK(high.updates == 1);
    CHECK(crit.state() == modesp::BaseModule::State::STOPPED);

    // Every turn after it is empty.
    modules.update_all(10);
    CHECK(g_stops == "low high crit ");
    CHECK(low.updates == 1);
}
