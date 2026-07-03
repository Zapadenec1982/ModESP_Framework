# modesp_aws - AWS IoT Core cloud service (compile-time alternative to modesp_mqtt)
# Optional: compiled only when selected as the cloud backend
# (CONFIG_MODESP_CLOUD_AWS); otherwise contributes zero sources.
set(aws_srcs "")
if(CONFIG_MODESP_CLOUD_AWS)
    list(APPEND aws_srcs "src/aws_iot_service.cpp")
endif()

idf_component_register(
    SRCS ${aws_srcs}
    INCLUDE_DIRS "include"
    # modesp_net: public — aws_iot_service.h включає modesp/net/cloud_service.h
    # (ICloudService). esp_http_server приходить транзитивно через нього.
    REQUIRES modesp_core modesp_services modesp_net esp_event esp_http_server mqtt esp-tls
    PRIV_REQUIRES jsmn mbedtls esp_wifi
)

# Доступ до згенерованих headers (mqtt_topics.h, state_meta.h)
if(CONFIG_MODESP_CLOUD_AWS)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/generated")
endif()
