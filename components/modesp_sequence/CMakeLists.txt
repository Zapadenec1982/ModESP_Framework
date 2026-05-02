# ModESP Sequence Engine — track-based time-dependent algorithm runtime
# See docs/sequence_engine/ for architecture details

# Step 1: header-only stub. Sources will be added incrementally per implementation step:
#   Step 5  — action_registry.cpp
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
        # Empty until Step 5; INTERFACE_LIBRARY workaround не used because
        # idf_component_register requires either SRCS or pure include dir
        # placeholder. Empty src list valid у ESP-IDF 5.x for header-only
        # transitional component.
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
