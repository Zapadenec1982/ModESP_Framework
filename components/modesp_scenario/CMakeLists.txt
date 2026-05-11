# modesp_scenario — track-based scenario orchestration engine
#
# Rebuild from monolithic modesp_sequence (4415 LOC, 38 commits accretion) into
# а single cohesive component з internal subdirectory separation:
#   src/core/       engine, FSM, modr loader, runtime types
#   src/actions/    ActionRegistry + built-in actions і conditions
#   src/continuous/ ContinuousRegistry + 3 primitives (PID, hysteresis, ramp)
#   src/arbiter/    ResourceArbiter (ISA-88 §5.3, engine-owned concrete)
#   src/observers/  Mirror (direct call) + NVS observer (edge-triggered)
#
# Phase 0: Scaffold only — placeholder library that compiles to nothing.
# Phase 1+: Real sources land progressively.

idf_component_register(
    SRCS
        "src/placeholder.cpp"
    INCLUDE_DIRS
        "include"
        "${CMAKE_SOURCE_DIR}/generated"
    PRIV_INCLUDE_DIRS
        "private"
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
