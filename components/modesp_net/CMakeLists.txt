# modesp_net - Phase 5a: WiFi + HTTP + WebSocket
# Optional: when CONFIG_MODESP_NET_ENABLE is off, contributes zero sources
# (empty component) so offline/BLE-only builds carry no network code.
set(net_srcs "")
if(CONFIG_MODESP_NET_ENABLE)
    list(APPEND net_srcs
         "src/wifi_service.cpp"
         "src/http_service.cpp"
         "src/ws_service.cpp")
endif()

idf_component_register(
    SRCS ${net_srcs}
    INCLUDE_DIRS "include"
    REQUIRES modesp_core modesp_services modesp_hal
             esp_wifi esp_netif esp_http_server esp_event nvs_flash app_update espressif__mdns
    PRIV_REQUIRES jsmn ds18b20 datalogger mbedtls modesp_scenario
)

# Доступ до згенерованих headers (state_meta.h для валідації POST /api/settings)
if(CONFIG_MODESP_NET_ENABLE)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/generated")
endif()
