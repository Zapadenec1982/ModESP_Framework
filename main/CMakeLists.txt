idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/generated"
    PRIV_REQUIRES modesp_core modesp_services modesp_hal modesp_net equipment datalogger
)

# Cloud backend: compile-time Kconfig choice
if(CONFIG_MODESP_CLOUD_AWS)
    idf_component_get_property(main_lib ${COMPONENT_NAME} COMPONENT_LIB)
    target_link_libraries(${main_lib} PRIVATE idf::modesp_aws)
else()
    idf_component_get_property(main_lib ${COMPONENT_NAME} COMPONENT_LIB)
    target_link_libraries(${main_lib} PRIVATE idf::modesp_mqtt)
endif()
