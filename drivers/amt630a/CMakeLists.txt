# AMT630A display driver — I2C-controlled OSD/TFT video-SoC (external control
# over OEM firmware) + Amt630aPort (IDisplayPort adapter). Pins/bus/geometry
# come from board.json (i2c_buses + i2c_displays); no pins in Kconfig.
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/amt630a.cpp" "src/amt630a_port.cpp"
    PRIV_REQUIRES modesp_core esp_driver_i2c esp_driver_gpio driver esp_rom
)
