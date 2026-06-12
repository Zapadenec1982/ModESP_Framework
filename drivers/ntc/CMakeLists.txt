# NTC thermistor ADC sensor driver (optional via menuconfig)
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/ntc_driver.cpp"
    PRIV_REQUIRES driver esp_adc
)
