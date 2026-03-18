/**
 * @file driver_manager_host.cpp
 * @brief HOST BUILD: Stub DriverManager methods for Equipment tests.
 *
 * Equipment calls bind_drivers(DriverManager&) which uses find_sensor/find_actuator.
 * In host tests we inject mock drivers directly, so these stubs just return nullptr.
 */

#include "mocks/freertos_mock.h"
#include "mocks/esp_log_mock.h"
#include "modesp/hal/driver_manager.h"

namespace modesp {

ISensorDriver* DriverManager::find_sensor(etl::string_view) {
    return nullptr;
}

IActuatorDriver* DriverManager::find_actuator(etl::string_view) {
    return nullptr;
}

void DriverManager::update_all(uint32_t) {}

} // namespace modesp
