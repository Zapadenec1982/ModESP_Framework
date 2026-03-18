# modesp_hal - Phase 4: Hardware Abstraction Layer
idf_component_register(
    SRCS "src/hal.cpp" "src/driver_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core driver
    PRIV_REQUIRES ds18b20 relay digital_input ntc pcf8574_relay pcf8574_input esp_adc esp_driver_i2c
)
