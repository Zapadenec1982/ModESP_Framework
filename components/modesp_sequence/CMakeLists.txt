# ModESP Sequence Engine — track-based time-dependent algorithm runtime
# See docs/sequence_engine/ for architecture details

# Step 5+ — компонент з C++ sources. Sources added incrementally per implementation:
#   Step 5  ✓ action_registry.cpp
#   Step 6  — continuous_registry.cpp
#   Step 7  — builtin_actions.cpp
#   Step 8  — modr_loader.cpp
#   Step 10 — resource_arbiter.cpp
#   Step 11 — sequence_track.cpp
#   Step 12 — sequence_instance.cpp
#   Step 14 — sequence_engine.cpp
#   Step 15 — nvs_token.cpp

idf_component_register(
    SRCS
        "src/action_registry.cpp"
        "src/continuous_registry.cpp"
        "src/builtin_actions.cpp"
        "src/modr_loader.cpp"
    INCLUDE_DIRS
        "include"
        "${CMAKE_SOURCE_DIR}/generated"
    REQUIRES
        modesp_core
        marcel-cd__etlcpp
    PRIV_REQUIRES
        modesp_services
        esp_timer
        esp_system
        freertos
        nvs_flash
)
