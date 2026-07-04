# nRF52832 tilt/orientation BLE-observer driver (optional via menuconfig).
# Needs CONFIG_MODESP_BLE_CENTRAL (transport mfg-decoder pool lives in modesp_ble).
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/ble_nrf_tilt_driver.cpp"
    PRIV_REQUIRES modesp_ble esp_timer
)
