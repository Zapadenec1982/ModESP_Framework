cmake_minimum_required(VERSION 3.16)
project(modesp_host_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Root of ModESP_v4 project ──
get_filename_component(ROOT "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)

# ── ETL library — pure C++, no platform dependencies ──
# Try managed_components first (if idf_component_manager has downloaded it)
set(ETL_INCLUDE "${ROOT}/managed_components/marcel-cd__etlcpp/etl/include")
if(NOT EXISTS "${ETL_INCLUDE}")
    # Fallback: ESP-IDF Component Manager global cache
    # Pattern: %USERPROFILE%/AppData/Local/Espressif/ComponentManager/Cache/*/marcel-cd__etlcpp_*/etl/include
    if(DEFINED ENV{USERPROFILE})
        set(_ETL_CACHE_BASE "$ENV{USERPROFILE}/AppData/Local/Espressif/ComponentManager/Cache")
    else()
        set(_ETL_CACHE_BASE "$ENV{HOME}/.espressif/ComponentManager/Cache")
    endif()
    file(GLOB _ETL_GLOB "${_ETL_CACHE_BASE}/*/marcel-cd__etlcpp_*/etl/include")
    list(SORT _ETL_GLOB ORDER DESCENDING)
    list(GET _ETL_GLOB 0 ETL_INCLUDE)
endif()
if(NOT EXISTS "${ETL_INCLUDE}")
    message(FATAL_ERROR "ETL library not found. Run 'idf.py build' first to download dependencies.")
endif()
message(STATUS "  ETL path: ${ETL_INCLUDE}")

# ── Include directories ──
# CRITICAL: mocks/ must be FIRST so that stub headers intercept ESP-IDF includes.
include_directories(
    "${CMAKE_SOURCE_DIR}/mocks"                             # freertos_mock.h, esp_log_mock.h, etc.
    "${CMAKE_SOURCE_DIR}/mocks/freertos"                    # freertos/FreeRTOS.h, semphr.h (via relative path)
    "${CMAKE_SOURCE_DIR}/mocks/driver"                      # driver/gpio.h, driver/i2c_master.h
    "${ETL_INCLUDE}"                                        # etl/*.h (pure C++, no stubs needed)
    "${ROOT}/components/modesp_core/include"                # modesp/shared_state.h, base_module.h, types.h
    "${ROOT}/generated"                                     # features_config.h, state_meta.h, etc.
    "${ROOT}/components/modesp_hal/include"                  # modesp/hal/driver_interfaces.h, hal_types.h
    "${ROOT}/modules/thermostat/include"                    # thermostat_module.h
    "${ROOT}/modules/defrost/include"                       # defrost_module.h
    "${ROOT}/modules/protection/include"                    # protection_module.h
    "${ROOT}/modules/equipment/include"                     # equipment_module.h
    "${ROOT}/modules/datalogger/include"                    # datalogger_module.h
    "${CMAKE_SOURCE_DIR}"                                   # for #include "doctest.h"
)

# ── Core host-build sources (replace ESP-IDF dependent originals) ──
set(CORE_SOURCES
    "${CMAKE_SOURCE_DIR}/shared_state_host.cpp"
    "${CMAKE_SOURCE_DIR}/base_module_host.cpp"
    "${CMAKE_SOURCE_DIR}/module_manager_host.cpp"
    "${CMAKE_SOURCE_DIR}/driver_manager_host.cpp"
)

# ── Business module sources (compile directly — only need esp_log.h which is mocked) ──
set(MODULE_SOURCES
    "${ROOT}/modules/thermostat/src/thermostat_module.cpp"
    "${ROOT}/modules/defrost/src/defrost_module.cpp"
    "${ROOT}/modules/protection/src/protection_module.cpp"
    "${ROOT}/modules/equipment/src/equipment_module.cpp"
    "${ROOT}/modules/datalogger/src/datalogger_module.cpp"
)

# ── Test sources ──
set(TEST_SOURCES
    "${CMAKE_SOURCE_DIR}/test_main.cpp"
    "${CMAKE_SOURCE_DIR}/test_thermostat.cpp"
    "${CMAKE_SOURCE_DIR}/test_defrost.cpp"
    "${CMAKE_SOURCE_DIR}/test_protection.cpp"
    "${CMAKE_SOURCE_DIR}/test_equipment.cpp"
    "${CMAKE_SOURCE_DIR}/test_datalogger.cpp"
)

add_executable(test_runner
    ${CORE_SOURCES}
    ${MODULE_SOURCES}
    ${TEST_SOURCES}
)

# ── Force-include prefix header path ──
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/mocks/host_prefix.h" HOST_PREFIX)

# ── Compile options ──
target_compile_options(test_runner PRIVATE
    -Wall
    -Wno-unused-variable
    -Wno-unused-parameter
    -Wno-unused-function
    -DHOST_BUILD=1
    -DETL_NO_PROFILE_HEADER
    "-include${HOST_PREFIX}"
)

# ── Windows-specific definitions ──
if(WIN32)
    target_compile_definitions(test_runner PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
    # Static link runtime libs — eliminates MSYS2 DLL dependencies
    # (libstdc++, libgcc, libwinpthread, libmpc etc.)
    target_link_options(test_runner PRIVATE -static)
endif()

# ── Link with system threading library (for std::mutex in freertos_mock.h) ──
if(UNIX)
    target_link_libraries(test_runner PRIVATE pthread)
endif()

# ── Output binary name ──
set_target_properties(test_runner PROPERTIES OUTPUT_NAME "test_runner")

message(STATUS "ModESP host tests configured")
message(STATUS "  ETL:     ${ETL_INCLUDE}")
message(STATUS "  ROOT:    ${ROOT}")
message(STATUS "  MOCKS:   ${CMAKE_SOURCE_DIR}/mocks")
