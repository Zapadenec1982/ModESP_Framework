# ModESP v4 — Modular ESP32 Firmware Framework
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "drivers"
    "modules"
)

# ── Board selection: читаємо з sdkconfig (menuconfig) ──
set(_BOARD_DIR "dev")
if(EXISTS "${CMAKE_SOURCE_DIR}/sdkconfig")
    file(STRINGS "${CMAKE_SOURCE_DIR}/sdkconfig" _sdkconfig_lines)
    foreach(_line ${_sdkconfig_lines})
        if(_line MATCHES "^CONFIG_MODESP_BOARD_DIR=\"(.+)\"$")
            set(_BOARD_DIR "${CMAKE_MATCH_1}")
        endif()
    endforeach()
endif()

set(_BOARD_PATH "${CMAKE_SOURCE_DIR}/boards/${_BOARD_DIR}")
if(NOT EXISTS "${_BOARD_PATH}/board.json")
    message(FATAL_ERROR "Board '${_BOARD_DIR}' not found in boards/")
endif()
file(COPY "${_BOARD_PATH}/board.json" DESTINATION "${CMAKE_SOURCE_DIR}/data")
file(COPY "${_BOARD_PATH}/bindings.json" DESTINATION "${CMAKE_SOURCE_DIR}/data")
message(STATUS "ModESP board: ${_BOARD_DIR}")

# ── Pre-build: generate UI schema and C++ headers from module manifests ──
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    execute_process(
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/generate_ui.py"
            --project     "${CMAKE_SOURCE_DIR}/project.json"
            --modules-dir "${CMAKE_SOURCE_DIR}/modules"
            --output-data "${CMAKE_SOURCE_DIR}/data"
            --output-gen  "${CMAKE_SOURCE_DIR}/generated"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE GEN_RESULT
    )
    if(NOT GEN_RESULT EQUAL 0)
        message(FATAL_ERROR "generate_ui.py failed (exit code ${GEN_RESULT})")
    endif()
else()
    message(WARNING "Python3 not found — skipping UI generation")
endif()

# Читаємо назву плати з board.json для ідентифікації прошивки
file(READ "${CMAKE_SOURCE_DIR}/data/board.json" _BOARD_JSON_CONTENT)
string(JSON _BOARD_NAME GET "${_BOARD_JSON_CONTENT}" "board")
message(STATUS "ModESP board name: ${_BOARD_NAME}")

# ── Firmware version (semver) ──
# Used by esp_app_get_description()->version in heartbeat & OTA version matching.
# MUST be updated before each release build.
set(PROJECT_VER "1.0.1")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(modesp_${_BOARD_NAME})

# Generate LittleFS image from data/ directory and flash to "data" partition
littlefs_create_partition_image(data data FLASH_IN_PROJECT)
