# HLK-LD2410B mmWave presence radar driver (optional via menuconfig)
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/ld2410b_driver.cpp"
    PRIV_REQUIRES driver esp_driver_uart
)
