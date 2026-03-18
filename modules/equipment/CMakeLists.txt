# Equipment Manager — HAL abstraction layer with arbitration
idf_component_register(
    SRCS "src/equipment_module.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core modesp_hal
)
