idf_component_register(
    SRCS "esp_btn.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_timer freertos
)