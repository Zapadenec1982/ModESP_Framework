# DigitalInput GPIO sensor driver
idf_component_register(
    SRCS "src/digital_input_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_hal
    PRIV_REQUIRES driver
)
