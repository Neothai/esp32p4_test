idf_component_register(
    SRCS "avilib.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer freertos esp32_fatfs esp_psram
)