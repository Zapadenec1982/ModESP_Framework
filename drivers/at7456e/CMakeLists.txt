# AT7456E display driver — MAX7456-compatible OSD overlay on composite video,
# bit-bang SPI + At7456ePort (IDisplayPort adapter).
# ⚠ Pins currently come from this driver's Kconfig (see ./Kconfig) — no board
# declares an spi_displays section yet, so the driver compiles and registers
# but cannot be bound until board.json/HAL grow SPI support (roadmap Phase 3).
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/at7456e.cpp" "src/at7456e_port.cpp"
    PRIV_REQUIRES modesp_core esp_driver_gpio driver esp_rom
)
