# MAX98357A I²S alarm sounder (optional via menuconfig)
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/max98357a_driver.cpp"
    PRIV_REQUIRES driver esp_driver_i2s
)
