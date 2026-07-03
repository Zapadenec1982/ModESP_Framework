# Display module — on-device screen menu generated from manifests.
# Hardware-agnostic: MenuEngine + DisplayModule over the IDisplayPort seam
# (modesp/hal/display_port.h). Concrete backends are DRIVERS (drivers/amt630a,
# drivers/at7456e) registered via MODESP_REGISTER_DISPLAY; DisplayModule
# resolves the active one from bindings.json in on_bind. No binding -> LogPort.
idf_component_register(
    SRCS "src/display_module.cpp"
         "src/menu_engine.cpp"
         "src/notification_queue.cpp"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/generated"
    # modesp_hal is PUBLIC: display_module.h/menu_engine.h include the
    # IDisplayPort seam (modesp/hal/display_port.h).
    REQUIRES modesp_core modesp_hal
)
