# modesp_net - Phase 5a: WiFi + HTTP + WebSocket
idf_component_register(
    SRCS "src/wifi_service.cpp"
         "src/http_service.cpp"
         "src/ws_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core modesp_services modesp_hal
             esp_wifi esp_netif esp_http_server esp_event nvs_flash app_update espressif__mdns
    PRIV_REQUIRES jsmn ds18b20 datalogger mbedtls
)

# Доступ до згенерованих headers (state_meta.h для валідації POST /api/settings)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/generated")
