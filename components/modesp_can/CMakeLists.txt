# modesp_can — CAN (TWAI) transport service (single TWAI node owner, ADR-004).
# Optional: when CONFIG_MODESP_CAN_ENABLE is off, contributes zero sources
# (empty component) so non-CAN builds carry no CAN code/flash. Headers stay
# available (can_messages.h is publishable unconditionally; can_codec.h is pure).

set(can_srcs "")
if(CONFIG_MODESP_CAN_ENABLE)
    list(APPEND can_srcs "src/can_service.cpp" "src/can_remote_cache.cpp")
endif()

idf_component_register(
    SRCS ${can_srcs}
    INCLUDE_DIRS "include"
    REQUIRES modesp_core modesp_hal
    PRIV_REQUIRES esp_driver_twai esp_timer nvs_flash
)

# can_registry.h (експорт-ключі + події + CRC реєстру) — з generated/, як
# mqtt_topics.h у modesp_mqtt: те саме єдине джерело манифестів.
if(CONFIG_MODESP_CAN_ENABLE)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/generated")
endif()
