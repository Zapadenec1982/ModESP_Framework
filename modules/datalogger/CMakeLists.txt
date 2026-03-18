# DataLogger module — temperature & event logging to LittleFS
# No HAL dependency — works through SharedState
idf_component_register(
    SRCS "src/datalogger_module.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core esp_http_server esp_timer
)
