# MAX98357A audio driver — I2S Class-D mono amplifier sink (IAudioSink).
# Pins/sample-rate come from board.json i2s_buses; no pins in Kconfig.
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/max98357a_sink.cpp"
    PRIV_REQUIRES modesp_core esp_driver_i2s esp_driver_gpio driver
)
