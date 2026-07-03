# DataLogger module — temperature & event logging to LittleFS
# No HAL dependency — works through SharedState.
# modesp_net: only for the ILogSource contract header (modesp/net/log_source.h);
# correct dependency direction (module -> framework), header-only when NET off.
idf_component_register(
    SRCS "src/datalogger_module.cpp"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/generated"
    REQUIRES modesp_core modesp_net esp_http_server esp_timer
)
