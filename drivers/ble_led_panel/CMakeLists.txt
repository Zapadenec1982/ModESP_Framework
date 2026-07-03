# iPixel/LED_BLE 16x64 BLE LED-matrix panel (NimBLE central-connect).
# Needs CONFIG_MODESP_BLE_CENTRAL (the generic central link lives in modesp_ble).
# Owns the iPixel wire format: GATT UUIDs (bt), the text-frame CRC (esp_rom) and the
# glyph font (generated/panel_font_data.h).
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/ble_led_panel_driver.cpp"
    PRIV_REQUIRES modesp_ble esp_timer bt esp_rom
)

# Glyph font is generated once (tools/gen_osd_font.py --target panel) into generated/.
# Guard: when the driver is disabled the component has no sources (COMPONENT_LIB is an
# INTERFACE lib) and PRIVATE include dirs would be rejected.
if(CONFIG_MODESP_DRIVER_BLE_LED_PANEL)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/generated")
endif()
