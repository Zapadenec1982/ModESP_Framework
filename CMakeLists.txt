# ModESP v4 — Modular ESP32 Firmware Framework
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "drivers"
    "modules"
)

# ── Board selection ───────────────────────────────────────────────────────────
# The firmware name, board.json copy, and code generation below all need the board
# BEFORE project()/Kconfig confgen runs, so we mirror confgen's OWN resolution order.
# This matters because `idf.py set-target <t>` renames sdkconfig away before its
# configure pass — and the following `idf.py build` won't re-configure — so this pass
# must already pick the right board, in one shot:
#   1. sdkconfig exists → it is the resolved config; read CONFIG_MODESP_BOARD_DIR.
#   2. no sdkconfig yet  → read the same defaults confgen will apply:
#                          sdkconfig.defaults then sdkconfig.defaults.<target>
#                          (target overlay wins), deriving the board dir from the
#                          selected choice symbol (CONFIG_MODESP_BOARD_<X>=y → <x>),
#                          matching main/Kconfig.projbuild.
# Silently defaulting to "dev" when sdkconfig was merely absent was a footgun: it
# shipped a dev-GPIO binary onto an S3 board. The post-project guard below backstops
# any drift between this bootstrap and the authoritative Kconfig value.
set(_BOARD_DIR "")

if(EXISTS "${CMAKE_SOURCE_DIR}/sdkconfig")
    file(STRINGS "${CMAKE_SOURCE_DIR}/sdkconfig" _sdkconfig_lines
         REGEX "^CONFIG_MODESP_BOARD_DIR=")
    foreach(_line ${_sdkconfig_lines})
        if(_line MATCHES "^CONFIG_MODESP_BOARD_DIR=\"(.+)\"$")
            set(_BOARD_DIR "${CMAKE_MATCH_1}")
        endif()
    endforeach()
else()
    # No sdkconfig yet (fresh checkout, `idf.py set-target`, or someone deleted it to
    # force a defaults re-apply). Resolve from the defaults overlay exactly as confgen
    # will, rather than guessing the wrong board.
    set(_IDF_TARGET "")
    if(DEFINED IDF_TARGET)
        set(_IDF_TARGET "${IDF_TARGET}")
    elseif(DEFINED ENV{IDF_TARGET})
        set(_IDF_TARGET "$ENV{IDF_TARGET}")
    endif()

    set(_defaults_files "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
    if(_IDF_TARGET)
        list(APPEND _defaults_files
             "${CMAKE_SOURCE_DIR}/sdkconfig.defaults.${_IDF_TARGET}")
    endif()

    foreach(_defaults_file ${_defaults_files})
        if(EXISTS "${_defaults_file}")
            file(STRINGS "${_defaults_file}" _board_lines
                 REGEX "^CONFIG_MODESP_BOARD_[A-Z0-9_]+=y$")
            foreach(_line ${_board_lines})
                if(_line MATCHES "^CONFIG_MODESP_BOARD_([A-Z0-9_]+)=y$")
                    string(TOLOWER "${CMAKE_MATCH_1}" _BOARD_DIR)
                endif()
            endforeach()
        endif()
    endforeach()

    if(_BOARD_DIR)
        message(STATUS "ModESP board: resolved '${_BOARD_DIR}' from sdkconfig.defaults"
                       " (no sdkconfig yet, target='${_IDF_TARGET}')")
    endif()
endif()

if(NOT _BOARD_DIR)
    set(_BOARD_DIR "dev")
    message(WARNING
        "ModESP: could not determine the target board from sdkconfig or "
        "sdkconfig.defaults — falling back to '${_BOARD_DIR}'. If that is not "
        "intended, run 'idf.py set-target <target>' to regenerate sdkconfig from "
        "the defaults, then rebuild.")
endif()

set(_BOARD_PATH "${CMAKE_SOURCE_DIR}/boards/${_BOARD_DIR}")
if(NOT EXISTS "${_BOARD_PATH}/board.json")
    message(FATAL_ERROR "Board '${_BOARD_DIR}' not found in boards/")
endif()
file(COPY "${_BOARD_PATH}/board.json" DESTINATION "${CMAKE_SOURCE_DIR}/data")
file(COPY "${_BOARD_PATH}/bindings.json" DESTINATION "${CMAKE_SOURCE_DIR}/data")
message(STATUS "ModESP board: ${_BOARD_DIR}")

# Re-run CMake configure (which re-copies the two files above into data/) whenever
# the active board's board.json/bindings.json change. Without this, editing them does
# NOT refresh data/ — file(COPY) only runs at configure time and these are not tracked
# as configure inputs — so the LittleFS data image keeps the STALE hardware map and the
# device boots the old config until a manual `idf.py reconfigure`/fullclean.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_BOARD_PATH}/board.json"
    "${_BOARD_PATH}/bindings.json")

# Re-run CMake configure whenever ANY single-source-of-truth input changes.
# Without this, editing a manifest/project.json after the first configure silently
# builds with STALE generated artifacts (ui.json, state_meta.h, datalogger_*.h,
# drivers.cmake, Kconfig) — breaking the manifest-driven contract.
# The GLOBs (with CONFIGURE_DEPENDS) retrigger on manifest ADD/REMOVE; listing the
# matched files in CMAKE_CONFIGURE_DEPENDS retriggers on CONTENT edits.
file(GLOB _modesp_module_manifests CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/modules/*/manifest.json")
file(GLOB _modesp_driver_manifests CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/drivers/*/manifest.json")
file(GLOB _modesp_board_manifests CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/boards/*/board.json")
file(GLOB _modesp_i18n_inputs CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/modules/*/i18n/*.json"
     "${CMAKE_SOURCE_DIR}/data/i18n/*.json")
file(GLOB _modesp_schemas CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/tools/schemas/*.schema.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/project.json"
    "${CMAKE_SOURCE_DIR}/tools/generate_ui.py"
    "${CMAKE_SOURCE_DIR}/tools/compile_scenario.py"
    ${_modesp_module_manifests}
    ${_modesp_driver_manifests}
    ${_modesp_board_manifests}
    ${_modesp_i18n_inputs}
    ${_modesp_schemas})

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

    # Compile recipe scenarios (.modr) from manifests — same single-source-of-truth
    # contract as generate_ui.py. Without this, editing a recipe's "scenario" block
    # would silently flash a stale .modr. --project filters out test recipes.
    execute_process(
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/compile_scenario.py"
            --modules-dir "${CMAKE_SOURCE_DIR}/modules"
            --output-dir  "${CMAKE_SOURCE_DIR}/data/scenarios"
            --project     "${CMAKE_SOURCE_DIR}/project.json"
            --strict
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE SCENARIO_RESULT
    )
    if(NOT SCENARIO_RESULT EQUAL 0)
        message(FATAL_ERROR "compile_scenario.py failed (exit code ${SCENARIO_RESULT})")
    endif()
else()
    message(FATAL_ERROR "Python3 not found — cannot generate UI/scenarios. Install Python 3.")
endif()

# Читаємо назву плати з board.json для ідентифікації прошивки
file(READ "${CMAKE_SOURCE_DIR}/data/board.json" _BOARD_JSON_CONTENT)
string(JSON _BOARD_NAME GET "${_BOARD_JSON_CONTENT}" "board")
message(STATUS "ModESP board name: ${_BOARD_NAME}")

# ── Trim the component graph to the product (single source of truth: project.json) ──
# generated/modules.cmake (written by generate_ui.py above) defines PRODUCT_MODULES.
# With COMPONENTS set, a module folder that is NOT in project.json no longer compiles;
# framework components enter the graph transitively via REQUIRES. Cloud/BLE components
# are listed explicitly because main links them via target_link_libraries(idf::...),
# which does not participate in component dependency discovery.
include("${CMAKE_SOURCE_DIR}/generated/modules.cmake")
set(COMPONENTS main ${PRODUCT_MODULES} modesp_mqtt modesp_aws modesp_ble)

# ── Firmware version (semver) ──
# Used by esp_app_get_description()->version in heartbeat & OTA version matching.
# Single source of truth: project.json "version" — bump it there before a release.
file(READ "${CMAKE_SOURCE_DIR}/project.json" _PROJECT_JSON_CONTENT)
string(JSON PROJECT_VER GET "${_PROJECT_JSON_CONTENT}" "version")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(modesp_${_BOARD_NAME})

# ── Board-selection guard ─────────────────────────────────────────────────────
# CONFIG_MODESP_BOARD_DIR is the authoritative board, resolved by Kconfig/confgen
# during project() above (it reflects sdkconfig plus any sdkconfig.defaults.<target>
# overlay). Everything board-specific — the firmware name, data/board.json, and the
# generated headers — was emitted earlier from the PRE-project _BOARD_DIR bootstrap.
# If the two disagree, those artifacts were built for the wrong board (e.g. a stale or
# just-created sdkconfig), so fail loudly instead of silently shipping them. By now
# confgen has written a correct sdkconfig, so a single re-run resolves the mismatch.
if(DEFINED CONFIG_MODESP_BOARD_DIR AND
   NOT "${CONFIG_MODESP_BOARD_DIR}" STREQUAL "${_BOARD_DIR}")
    message(FATAL_ERROR
        "ModESP board mismatch: build artifacts (firmware 'modesp_${_BOARD_NAME}', "
        "data/board.json, generated headers) were produced for board '${_BOARD_DIR}', "
        "but Kconfig resolved CONFIG_MODESP_BOARD_DIR='${CONFIG_MODESP_BOARD_DIR}'. "
        "sdkconfig is now up to date — re-run 'idf.py build' so the resolved board is used.")
endif()

# Generate LittleFS image from data/ directory and flash to "data" partition.
# DEPENDS on the active board's config copies (refreshed by file(COPY) above on
# every reconfigure) so data.bin is regenerated when board.json/bindings.json
# change — otherwise the image keeps a stale hardware map and the device boots the
# old config until a manual `Remove-Item build/data.bin` / fullclean.
littlefs_create_partition_image(data data FLASH_IN_PROJECT
    DEPENDS "${CMAKE_SOURCE_DIR}/data/board.json"
            "${CMAKE_SOURCE_DIR}/data/bindings.json")
