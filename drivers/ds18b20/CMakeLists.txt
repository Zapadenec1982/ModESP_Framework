# DS18B20 OneWire temperature sensor driver
idf_component_register(
    SRCS "src/ds18b20_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_hal
    PRIV_REQUIRES driver esp_timer
)
