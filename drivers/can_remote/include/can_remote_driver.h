/**
 * @file can_remote_driver.h
 * @brief Драйвер-міст: віддалений ключ ModESP-вузла по CAN як ISensorDriver.
 *
 * Дзеркало ble_xiaomi_th для транспорту CAN: factory на боті резолвить
 * remote-пристрій (transport "can", identity = node-id) через HAL, підписує
 * (node, key_id) у CanRemoteCache і читає кеш у read(). Роль прив'язується
 * по capability — модуль-споживач не знає, що дані приїхали по CAN.
 *
 * Lifecycle:
 *   1) factory: configure(role, reading*, stale_ms)
 *   2) DriverManager: init()
 *   3) main loop: update(dt) — no-op (кеш веде CanService)
 *   4) споживач: read(value) / is_healthy()
 */

#pragma once

#include "sdkconfig.h"

#if defined(CONFIG_MODESP_CAN_ENABLE)

#include "modesp/hal/driver_interfaces.h"
#include "modesp/can/can_remote_cache.h"

namespace modesp {

class CanRemoteDriver : public ISensorDriver {
public:
    void configure(const char* role, const CanReading* reading,
                   uint8_t node, uint32_t stale_ms);

    bool init() override { return reading_ != nullptr; }
    void update(uint32_t dt_ms) override { (void)dt_ms; }

    /// true + значення, якщо кеш містить валідний кадр (bool → 0/1)
    bool read(float& value) override;

    /// Вузол online + кадр валідний + свіжіший за stale_ms
    bool is_healthy() const override;

    const char* role() const override { return role_; }
    const char* type() const override { return "can_remote"; }

private:
    char              role_[24] = {};
    const CanReading* reading_  = nullptr;
    uint8_t           node_     = 0;
    int64_t           stale_us_ = 5000 * 1000LL;
};

} // namespace modesp

#endif // CONFIG_MODESP_CAN_ENABLE
