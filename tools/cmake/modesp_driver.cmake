# ─────────────────────────────────────────────────────────────────────
# modesp_driver_component() — register an OPTIONAL driver component.
#
# Compiles SRCS only when the driver's Kconfig option is enabled:
#   CONFIG_MODESP_DRIVER_<NAME>   (NAME = component/folder name, uppercased)
# When disabled, the component still registers (headers available) but with no
# sources, so it contributes no code and the binary shrinks. The option is
# generated automatically by tools/generate_ui.py (menu "ModESP Drivers" in
# components/modesp_hal/Kconfig) — a new driver gets a toggle with zero manual
# Kconfig edits.
#
# IMPORTANT: this is a macro(), NOT a function() — idf_component_register sets
# variables in the COMPONENT scope, which a function() would swallow (the
# component would be reported "not registered"). Also: REQUIRES must NOT depend
# on CONFIG_* (ESP-IDF expands requirements before sdkconfig is loaded), so the
# driver is always REQUIRED by modesp_hal; only its SRCS are gated.
#
# Usage in drivers/<name>/CMakeLists.txt:
#   include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
#   modesp_driver_component(
#       SRCS "src/<name>_driver.cpp"
#       PRIV_REQUIRES driver esp_timer   # optional extra deps
#   )
# (REQUIRES modesp_hal is added automatically.)
# ─────────────────────────────────────────────────────────────────────
macro(modesp_driver_component)
    cmake_parse_arguments(_MDC "" "" "SRCS;REQUIRES;PRIV_REQUIRES" ${ARGN})

    string(TOUPPER "${COMPONENT_NAME}" _mdc_upper)
    if(CONFIG_MODESP_DRIVER_${_mdc_upper})
        set(_mdc_srcs ${_MDC_SRCS})
    else()
        set(_mdc_srcs "")   # disabled → register empty (no code compiled)
    endif()

    idf_component_register(
        SRCS ${_mdc_srcs}
        INCLUDE_DIRS "include"
        REQUIRES modesp_hal ${_MDC_REQUIRES}
        PRIV_REQUIRES ${_MDC_PRIV_REQUIRES}
    )
endmacro()
