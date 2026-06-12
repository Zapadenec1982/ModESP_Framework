# modesp_hal - Phase 4: Hardware Abstraction Layer
#
# The driver list (generated/drivers.cmake) is produced from drivers/*/manifest.json
# so a new driver is wired in automatically — no manual edit here. ALL drivers are
# REQUIRED (ESP-IDF expands requirements before sdkconfig loads, so REQUIRES must
# not depend on CONFIG_*); a driver disabled in menuconfig simply compiles with no
# sources (see tools/cmake/modesp_driver.cmake) and contributes nothing.
#
# NOTE: use CMAKE_CURRENT_LIST_DIR (this component's dir), NOT CMAKE_SOURCE_DIR —
# during ESP-IDF's requirements-expansion phase CMAKE_SOURCE_DIR points at build/.
get_filename_component(_modesp_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${_modesp_root}/generated/drivers.cmake")   # MODESP_ALL_DRIVERS

idf_component_register(
    SRCS "src/hal.cpp" "src/driver_manager.cpp" "src/driver_registry.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core driver
    PRIV_REQUIRES ${MODESP_ALL_DRIVERS} esp_adc esp_driver_i2c
)

# Generated driver_register_all.h (modesp_register_all_drivers())
target_include_directories(${COMPONENT_LIB} PRIVATE "${_modesp_root}/generated")

# Consistency gate: the active board's bindings must not reference a driver that
# is disabled in menuconfig (otherwise it would silently no-op at runtime).
# This runs ONLY in the real build-config phase — ESP-IDF's requirements-expansion
# redefines idf_component_register to return() early, so code here never runs there
# (where CONFIG_* are undefined). MODESP_BOUND_DRIVERS comes from bindings.json.
include("${_modesp_root}/generated/required_drivers.cmake")
foreach(_drv ${MODESP_BOUND_DRIVERS})
    string(TOUPPER "${_drv}" _u)
    if(NOT CONFIG_MODESP_DRIVER_${_u})
        message(FATAL_ERROR
            "Board binds driver '${_drv}' but it is disabled in menuconfig "
            "(CONFIG_MODESP_DRIVER_${_u}=n). Enable it, or run: python tools/drivers_sync.py --fix")
    endif()
endforeach()
