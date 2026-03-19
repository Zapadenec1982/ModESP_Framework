# Equipment Manager — universal HAL owner with manifest-driven roles
# EquipmentBase = framework (generic driver binding, sensor reading, state publishing)
# EquipmentModule = template (pass-through arbitration, override in product)
idf_component_register(
    SRCS "src/equipment_base.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core modesp_hal
)
