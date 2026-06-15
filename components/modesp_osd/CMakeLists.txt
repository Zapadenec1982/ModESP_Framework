# modesp_osd — portable AT7456E (MAX7456-compatible) OSD driver.
# Bit-bang on configurable pins; shared by Framework and Scanner projects.
idf_component_register(
    SRCS "src/at7456e.cpp"
         "src/amt630a.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_rom esp_driver_i2c
)
