# CAN remote-key bridge driver (optional via menuconfig).
# Needs CONFIG_MODESP_CAN_ENABLE (CanRemoteCache lives in modesp_can).
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/can_remote_driver.cpp"
    PRIV_REQUIRES modesp_can esp_timer
)
