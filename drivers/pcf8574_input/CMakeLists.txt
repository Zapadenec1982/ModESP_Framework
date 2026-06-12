# PCF8574 I2C digital input sensor driver (optional via menuconfig)
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/pcf8574_input_driver.cpp"
)
