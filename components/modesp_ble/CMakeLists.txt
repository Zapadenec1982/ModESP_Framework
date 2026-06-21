# modesp_ble — BLE transport service (single NimBLE host owner).
# Optional: when CONFIG_MODESP_BLE_ENABLE is off, contributes zero sources
# (empty component) so non-BLE builds carry no BLE code/flash.

set(ble_srcs "")
if(CONFIG_MODESP_BLE_ENABLE)
    list(APPEND ble_srcs "src/ble_service.cpp")
endif()

idf_component_register(
    SRCS ${ble_srcs}
    INCLUDE_DIRS "include"
    REQUIRES modesp_core
    PRIV_REQUIRES bt esp_coex nvs_flash jsmn modesp_net esp_rom
)

# Phase 1 GATT server reuses the generated key lists (mqtt_topics.h) +
# validation metadata (state_meta.h) — same single source as MQTT/HTTP.
if(CONFIG_MODESP_BLE_ENABLE)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/generated")
endif()
