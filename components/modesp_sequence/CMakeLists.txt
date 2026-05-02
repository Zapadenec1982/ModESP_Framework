# ModESP Sequence Engine — track-based time-dependent algorithm runtime
# See docs/sequence_engine/ for architecture details

# Step 1: header-only INTERFACE library. Sources added у наступних steps:
#   Step 5  — action_registry.cpp
#   Step 6  — continuous_registry.cpp
#   Step 7  — builtin_actions.cpp
#   Step 8  — modr_loader.cpp
#   Step 10 — resource_arbiter.cpp
#   Step 11 — sequence_track.cpp
#   Step 12 — sequence_instance.cpp
#   Step 14 — sequence_engine.cpp
#   Step 15 — nvs_token.cpp
#
# Per ESP-IDF build-system docs:
#   "Special components which contain no source files... can have a one-line
#    CMakeLists.txt file which calls idf_component_register() with no
#    arguments specified."
# Header-only extension: omit SRCS, declare INCLUDE_DIRS + REQUIRES only.
# This makes the component an INTERFACE library — headers exposed без compilation.

idf_component_register(
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
