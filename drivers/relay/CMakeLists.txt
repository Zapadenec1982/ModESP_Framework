# Relay actuator driver
idf_component_register(
    SRCS "src/relay_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_hal
    PRIV_REQUIRES driver
)
