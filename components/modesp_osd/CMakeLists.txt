# modesp_osd — portable AT7456E (MAX7456-compatible) OSD driver.
# Bit-bang on configurable pins; shared by Framework and Scanner projects.
idf_component_register(
    SRCS "src/at7456e.cpp"
         "src/amt630a.cpp"
    INCLUDE_DIRS "include"
    # esp_driver_gpio: ESP-IDF 6.0 moved driver/gpio.h out of the `driver` umbrella
    # (which now only PRIV_REQUIRES it). at7456e.cpp bit-bangs GPIO, so require it explicitly.
    REQUIRES driver esp_rom esp_driver_i2c esp_driver_gpio
)
