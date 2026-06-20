# Xiaomi LYWSD03MMC BLE-observer temp/hum/battery driver (optional via menuconfig).
# Needs CONFIG_MODESP_BLE_CENTRAL (BleCentral cache lives in modesp_ble).
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/ble_xiaomi_th_driver.cpp"
    PRIV_REQUIRES modesp_ble esp_timer
)
