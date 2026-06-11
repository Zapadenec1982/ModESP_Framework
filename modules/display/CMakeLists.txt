# Display module — on-device screen menu generated from manifests
# Hardware-agnostic: renders into DisplayFrame via IDisplayRenderer
idf_component_register(
    SRCS "src/display_module.cpp" "src/menu_engine.cpp"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/generated"
    REQUIRES modesp_core
)
