/**
 * @file mock_drivers.h
 * @brief HOST BUILD: Mock ISensorDriver and IActuatorDriver for unit tests.
 *
 * Simple controllable mocks — test code sets values directly,
 * EquipmentModule reads them through the interface.
 */
#pragma once

#include "modesp/hal/driver_interfaces.h"
#include <cstring>

namespace modesp {

// ═══════════════════════════════════════════════════════════════
// MockSensorDriver — controllable sensor for tests
// ═══════════════════════════════════════════════════════════════

class MockSensorDriver : public ISensorDriver {
public:
    explicit MockSensorDriver(const char* role_name, const char* type_name = "ds18b20")
        : role_(role_name), type_(type_name) {}

    bool init() override { return true; }
    void update(uint32_t) override {}

    bool read(float& value) override {
        if (!healthy_) return false;
        value = value_;
        return read_ok_;
    }

    bool is_healthy() const override { return healthy_; }
    const char* role() const override { return role_; }
    const char* type() const override { return type_; }

    // ── Test control ──
    void set_value(float v) { value_ = v; read_ok_ = true; }
    void set_healthy(bool h) { healthy_ = h; }
    void set_read_ok(bool ok) { read_ok_ = ok; }

private:
    const char* role_;
    const char* type_;
    float value_ = 0.0f;
    bool healthy_ = true;
    bool read_ok_ = true;
};

// ═══════════════════════════════════════════════════════════════
// MockActuatorDriver — controllable actuator for tests
// ═══════════════════════════════════════════════════════════════

class MockActuatorDriver : public IActuatorDriver {
public:
    explicit MockActuatorDriver(const char* role_name, const char* type_name = "relay")
        : role_(role_name), type_(type_name) {}

    bool init() override { return true; }
    void update(uint32_t) override {}

    bool set(bool state) override {
        state_ = state;
        set_count_++;
        return true;
    }

    bool get_state() const override { return state_; }
    const char* role() const override { return role_; }
    const char* type() const override { return type_; }
    bool is_healthy() const override { return true; }
    uint32_t switch_count() const override { return set_count_; }

    // ── Test control ──
    void force_state(bool s) { state_ = s; }
    uint32_t get_set_count() const { return set_count_; }

private:
    const char* role_;
    const char* type_;
    bool state_ = false;
    uint32_t set_count_ = 0;
};

} // namespace modesp
