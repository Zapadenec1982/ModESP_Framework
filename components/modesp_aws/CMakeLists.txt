# modesp_aws - AWS IoT Core cloud service (compile-time alternative to modesp_mqtt)
idf_component_register(
    SRCS "src/aws_iot_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core modesp_services esp_event esp_http_server mqtt esp-tls
    PRIV_REQUIRES jsmn modesp_net mbedtls esp_wifi
)

# Доступ до згенерованих headers (mqtt_topics.h, state_meta.h)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/generated")
