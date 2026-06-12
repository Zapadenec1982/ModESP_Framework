# PCF8574 I2C relay actuator driver (optional via menuconfig)
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/pcf8574_relay_driver.cpp"
)
