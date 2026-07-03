# Equipment demo module — thin product subclass of the framework EquipmentBase
# (components/modesp_equipment). EquipmentModule is header-only (pass-through
# arbitration template); the generated module_instances.h instantiates it in
# main, so this component only exports the header.
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES modesp_equipment
)
