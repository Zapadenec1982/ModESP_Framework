/**
 * @file can_remote_driver.cpp
 * @brief Міст CAN→роль: віддалений ключ ModESP-вузла як звичайний сенсор.
 */

#include "sdkconfig.h"

#if defined(CONFIG_MODESP_CAN_ENABLE)

#include <string.h>
#include <stdlib.h>

#include "can_remote_driver.h"

#include "modesp/hal/hal.h"
#include "modesp/hal/driver_registry.h"
#include "modesp/can/can_codec.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "can_remote";

namespace modesp {

void CanRemoteDriver::configure(const char* role, const CanReading* reading,
                                uint8_t node, uint32_t stale_ms) {
    strncpy(role_, role, sizeof(role_) - 1);
    role_[sizeof(role_) - 1] = '\0';
    reading_  = reading;
    node_     = node;
    stale_us_ = int64_t(stale_ms) * 1000LL;
}

bool CanRemoteDriver::read(float& value) {
    if (!reading_ || !reading_->valid) return false;
    switch (reading_->type) {
        case can::ValType::F32:  value = reading_->f32; break;
        case can::ValType::I32:  value = float(reading_->i32); break;
        case can::ValType::BOOL: value = reading_->b ? 1.0f : 0.0f; break;
        default: return false;
    }
    return true;
}

bool CanRemoteDriver::is_healthy() const {
    if (!reading_ || !reading_->valid || reading_->last_us == 0) return false;
    if (!CanRemoteCache::instance().node_online(node_)) return false;
    return (esp_timer_get_time() - reading_->last_us) < stale_us_;
}

} // namespace modesp

// ═══════════════════════════════════════════════════════════════
// Factory + реєстрація (патерн ble_xiaomi_th: identity живе на ПРИСТРОЇ,
// binding.address обирає, ЩО саме з пристрою живить роль)
// ═══════════════════════════════════════════════════════════════

namespace {

constexpr size_t MAX_CAN_REMOTE = 8;
modesp::CanRemoteDriver s_pool[MAX_CAN_REMOTE];
size_t s_n = 0;

modesp::ISensorDriver* can_remote_factory(const modesp::Binding& b, modesp::HAL& hal) {
    if (s_n >= MAX_CAN_REMOTE) {
        ESP_LOGE(TAG, "pool exhausted");
        return nullptr;
    }

    // Пристрій (board.json remote_devices + runtime /data/devices.json,
    // merge робить ConfigService) → node-id з identity-блоба.
    auto* dev = hal.find_remote_device(
        etl::string_view(b.hardware_id.c_str(), b.hardware_id.size()));
    if (!dev) {
        ESP_LOGE(TAG, "role '%s': hw '%s' not in board.json nor devices.json",
                 b.role.c_str(), b.hardware_id.c_str());
        return nullptr;
    }
    if (dev->transport != "can") {
        ESP_LOGE(TAG, "'%s': transport '%s' != can", b.hardware_id.c_str(),
                 dev->transport.c_str());
        return nullptr;
    }
    // endptr-перевірка: runtime devices.json не валідується генератором —
    // '1_2'/'1x' не повинні тихо парситись як вузол 1
    char* id_end = nullptr;
    long nid = strtol(dev->identity.c_str(), &id_end, 0);
    if (!id_end || *id_end != '\0' || id_end == dev->identity.c_str() ||
        nid < modesp::can::NODE_MIN || nid > modesp::can::NODE_MAX) {
        ESP_LOGE(TAG, "'%s': identity '%s' не є node-id 1..253",
                 b.hardware_id.c_str(), dev->identity.c_str());
        return nullptr;
    }

    // address = ім'я віддаленого ключа (key-id = djb2-fold16, ДЗЕРКАЛО
    // генератора вузла-джерела) або явний hex-id "0x1234" для can_id-override.
    if (b.address.empty()) {
        ESP_LOGE(TAG, "role '%s': binding потребує address = ім'я віддаленого ключа",
                 b.role.c_str());
        return nullptr;
    }
    uint16_t key_id;
    if (b.address.size() > 2 && b.address[0] == '0' &&
        (b.address[1] == 'x' || b.address[1] == 'X')) {
        char* addr_end = nullptr;
        long raw = strtol(b.address.c_str(), &addr_end, 16);
        if (!addr_end || *addr_end != '\0' || raw < 1 || raw > 0xFFFF) {
            ESP_LOGE(TAG, "role '%s': hex key-id '%s' поза 0x0001..0xFFFF",
                     b.role.c_str(), b.address.c_str());
            return nullptr;
        }
        key_id = uint16_t(raw);
    } else {
        key_id = modesp::can::fold16(b.address.c_str());
    }

    const modesp::CanReading* cache =
        modesp::CanRemoteCache::instance().subscribe(uint8_t(nid), key_id);
    if (!cache) {
        ESP_LOGE(TAG, "CanRemoteCache pool full");
        return nullptr;
    }

    auto& drv = s_pool[s_n++];
    drv.configure(b.role.c_str(), cache, uint8_t(nid),
                  uint32_t(b.setting_or("stale_ms", 5000.0f)));
    ESP_LOGI(TAG, "role '%s' ← node %ld key '%s' (0x%04X)",
             b.role.c_str(), nid, b.address.c_str(), key_id);
    return &drv;
}

} // namespace

MODESP_REGISTER_SENSOR(can_remote, &can_remote_factory)

#endif // CONFIG_MODESP_CAN_ENABLE
