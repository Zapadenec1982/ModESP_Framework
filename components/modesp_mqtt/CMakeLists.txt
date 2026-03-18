idf_component_register(
    SRCS "src/mqtt_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core modesp_services esp_event esp_http_server mqtt esp-tls
    PRIV_REQUIRES jsmn modesp_net mbedtls
)

# Доступ до згенерованих headers (mqtt_topics.h, state_meta.h)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/generated")
