# Display module — on-device screen menu generated from manifests
# Hardware-agnostic core (MenuEngine + DisplayModule); concrete renderers optional.
#
# NOTE: REQUIRES cannot depend on Kconfig — ESP-IDF resolves component
# dependencies in an early pass before sdkconfig is available. So modesp_osd
# is required unconditionally; at7456e_port.cpp is an empty TU unless
# CONFIG_MODESP_DISPLAY_AT7456E is set (guarded inside the source). The linker
# drops the unused driver via -ffunction-sections/gc-sections when disabled.
idf_component_register(
    SRCS "src/display_module.cpp"
         "src/menu_engine.cpp"
         "src/char_grid.cpp"
         "src/notification_queue.cpp"
         "src/at7456e_port.cpp"
         "src/amt630a_port.cpp"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/generated"
    REQUIRES modesp_core modesp_osd
)
