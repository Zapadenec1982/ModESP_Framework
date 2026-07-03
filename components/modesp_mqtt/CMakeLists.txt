# modesp_mqtt — Mosquitto/self-hosted MQTT cloud backend.
# Optional: compiled only when selected as the cloud backend
# (CONFIG_MODESP_CLOUD_MQTT); otherwise contributes zero sources.
set(mqtt_srcs "")
if(CONFIG_MODESP_CLOUD_MQTT)
    list(APPEND mqtt_srcs "src/mqtt_service.cpp")
endif()

idf_component_register(
    SRCS ${mqtt_srcs}
    INCLUDE_DIRS "include"
    # modesp_net: public — mqtt_service.h включає modesp/net/cloud_service.h
    # (ICloudService). esp_http_server приходить транзитивно через нього.
    REQUIRES modesp_core modesp_services modesp_net esp_event esp_http_server mqtt esp-tls
    PRIV_REQUIRES jsmn mbedtls
)

# Доступ до згенерованих headers (mqtt_topics.h, state_meta.h)
if(CONFIG_MODESP_CLOUD_MQTT)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/generated")
endif()
