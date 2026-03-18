# NTC thermistor ADC sensor driver
idf_component_register(
    SRCS "src/ntc_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_hal
    PRIV_REQUIRES driver esp_adc
)
