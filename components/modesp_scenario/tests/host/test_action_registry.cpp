/**
 * @file test_action_registry.cpp
 * @brief Smoke tests для new ActionRegistry (singleton-killed).
 *
 * Verifies registration, lookup, collision detection, hash consistency.
 */

#include "modesp/scenario/action_registry.h"
#include "modesp/scenario/action_param.h"

#include <cassert>
#include <cstdio>

using namespace modesp::scenario;

static ActionStatus stub_action(ActionContext&) { return ActionStatus::OK; }
static ActionStatus stub_action2(ActionContext&) { return ActionStatus::OK; }

int main() {
    // 1. Fresh registry starts empty
    {
        ActionRegistry reg;
        assert(reg.action_count() == 0);
        assert(reg.condition_count() == 0);
        assert(reg.find_action(djb2_hash16("any")) == nullptr);
        assert(reg.find_condition(djb2_hash16("any")) == nullptr);
    }

    // 2. Register і retrieve
    {
        ActionRegistry reg;
        ActionDescriptor d{
            djb2_hash16("log"), "log", &stub_action, 0, 8
        };
        assert(reg.register_action(d));
        assert(reg.action_count() == 1);
        auto* found = reg.find_action(djb2_hash16("log"));
        assert(found != nullptr);
        assert(found->fn == &stub_action);
    }

    // 3. Hash/name mismatch rejected
    {
        ActionRegistry reg;
        ActionDescriptor bad{0xDEAD, "log", &stub_action, 0, 0};
        assert(!reg.register_action(bad));
        assert(reg.action_count() == 0);
    }

    // 4. Duplicate hash collision rejected
    {
        ActionRegistry reg;
        ActionDescriptor d{djb2_hash16("log"), "log", &stub_action, 0, 0};
        assert(reg.register_action(d));
        assert(!reg.register_action(d));
    }

    // 5. Actions і conditions у separate namespaces — same hash OK
    {
        ActionRegistry reg;
        ActionDescriptor d{djb2_hash16("foo"), "foo", &stub_action, 0, 0};
        assert(reg.register_action(d));
        assert(reg.register_condition(d));
        assert(reg.action_count() == 1);
        assert(reg.condition_count() == 1);
        assert(reg.find_action(d.hash) != nullptr);
        assert(reg.find_condition(d.hash) != nullptr);
    }

    // 6. Multiple distinct registrations
    {
        ActionRegistry reg;
        reg.register_action({djb2_hash16("a"), "a", &stub_action,  0, 0});
        reg.register_action({djb2_hash16("b"), "b", &stub_action2, 0, 0});
        assert(reg.action_count() == 2);
        assert(reg.find_action(djb2_hash16("a"))->fn == &stub_action);
        assert(reg.find_action(djb2_hash16("b"))->fn == &stub_action2);
    }

    // 7. clear() resets registry
    {
        ActionRegistry reg;
        reg.register_action({djb2_hash16("x"), "x", &stub_action, 0, 0});
        reg.register_condition({djb2_hash16("y"), "y", &stub_action, 0, 0});
        reg.clear();
        assert(reg.action_count() == 0);
        assert(reg.condition_count() == 0);
    }

    std::printf("test_action_registry: OK (7 cases)\n");
    return 0;
}
