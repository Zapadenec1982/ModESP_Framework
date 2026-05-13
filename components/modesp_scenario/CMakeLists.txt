# modesp_scenario — track-based scenario orchestration engine
#
# Rebuild from monolithic modesp_sequence (4415 LOC, 38 commits accretion) into
# а single cohesive component з internal subdirectory separation:
#   src/core/       engine, instance FSM, track FSM, modr loader
#   src/actions/    ActionRegistry + built-in actions і conditions
#   src/continuous/ ContinuousRegistry + 3 primitives (PID, hysteresis, ramp)
#   src/arbiter/    ResourceArbiter (ISA-88 §5.3, engine-owned concrete)
#   src/observers/  Mirror (direct call) + NVS observer (Phase 2)
#
# Phase 1: core lift, namespace rename, registry injection, mirror direct call.

idf_component_register(
    SRCS
        # core
        "src/core/engine.cpp"
        "src/core/instance.cpp"
        "src/core/track.cpp"
        "src/core/modr_loader.cpp"
        # actions
        "src/actions/action_registry.cpp"
        "src/actions/builtin_actions.cpp"
        # continuous
        "src/continuous/continuous_registry.cpp"
        "src/continuous/continuous_primitives.cpp"
        # arbiter
        "src/arbiter/resource_arbiter.cpp"
        # observers
        "src/observers/mirror.cpp"
        "src/observers/nvs_observer.cpp"
        "src/observers/nvs_token.cpp"
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
