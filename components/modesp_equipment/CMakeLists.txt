# modesp_equipment — framework Equipment layer.
# EquipmentBase: generic driver binding by manifest roles, sensor reading with
# filtering, actuator arbitration seam, state publishing. Products subclass it
# (class MyEquipment : public EquipmentBase) and override apply_arbitration().
# The demo subclass lives in modules/equipment (EquipmentModule, header-only).
idf_component_register(
    SRCS "src/equipment_base.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core modesp_hal
)
