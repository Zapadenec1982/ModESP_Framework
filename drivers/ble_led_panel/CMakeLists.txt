# iPixel/LED_BLE 16x64 BLE LED-matrix panel actuator (NimBLE central-connect).
# Needs CONFIG_MODESP_BLE_CENTRAL (connect logic + BlePanel live in modesp_ble).
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/ble_led_panel_driver.cpp"
    PRIV_REQUIRES modesp_ble esp_timer
)
