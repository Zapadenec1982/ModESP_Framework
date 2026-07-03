# Module list comes from project.json via generated/modules.cmake (PRODUCT_MODULES) —
# adding/removing a module in project.json is the ONLY step; no manual list here.
# CMAKE_CURRENT_LIST_DIR, not CMAKE_SOURCE_DIR: during ESP-IDF's requirements-expansion
# pass CMAKE_SOURCE_DIR points at build/ (see components/modesp_hal/CMakeLists.txt).
get_filename_component(_modesp_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${_modesp_root}/generated/modules.cmake")   # PRODUCT_MODULES

# scenario_persist.cpp is only meaningful with the scenario engine compiled in.
# NOTE: SRCS may not depend on CONFIG_* during the requirements-expansion pass,
# but idf_component_register()'s real registration runs in the build-config phase
# where CONFIG_* are available — same pattern as modesp_ble.
set(main_srcs "main.cpp")
if(CONFIG_MODESP_SCENARIO_ENABLE)
    list(APPEND main_srcs "scenario_persist.cpp")
endif()

idf_component_register(
    SRCS ${main_srcs}
    INCLUDE_DIRS "${_modesp_root}/generated"
    PRIV_REQUIRES modesp_core modesp_services modesp_hal modesp_net modesp_scenario ${PRODUCT_MODULES}
)

# Cloud backend: compile-time Kconfig choice (MODESP_CLOUD_NONE links neither)
if(CONFIG_MODESP_CLOUD_AWS)
    idf_component_get_property(main_lib ${COMPONENT_NAME} COMPONENT_LIB)
    target_link_libraries(${main_lib} PRIVATE idf::modesp_aws)
elseif(CONFIG_MODESP_CLOUD_MQTT)
    idf_component_get_property(main_lib ${COMPONENT_NAME} COMPONENT_LIB)
    target_link_libraries(${main_lib} PRIVATE idf::modesp_mqtt)
endif()

# BLE transport: optional, compile-time Kconfig toggle (menu "ModESP BLE")
if(CONFIG_MODESP_BLE_ENABLE)
    idf_component_get_property(main_lib ${COMPONENT_NAME} COMPONENT_LIB)
    target_link_libraries(${main_lib} PRIVATE idf::modesp_ble)
endif()
