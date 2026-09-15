idf_component_register(
    SRCS 
        "main.c"
        "esp32_wifi.c"
        "es8311.c"
    INCLUDE_DIRS 
        "."
    REQUIRES
        esp_system
        esp_timer
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        esp_hosted
        esp_lcd
        esp_driver_i2c
        esp_mm
        esp_wifi_remote
        esp_driver_gpio
        esp_driver_sdmmc
        esp_lvgl_port
        esp_lcd_touch
        esp_lcd_touch_gt911
        esp_driver_jpeg
        esp_ringbuf
        nvs_flash
        lwip
        mbedtls
        freertos 
        esp_http_client
        esp32_mp3dec
        esp_btn
        sdmmc 
        esp32_fatfs
        esp32_avidec
        lvgl
)

# Patch บังคับ Link SDMMC ให้ esp_hosted บน ESP-IDF v6
#if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
#    idf_component_get_property(esp_hosted_lib espressif__esp_hosted COMPONENT_LIB)
#    idf_component_get_property(sdmmc_driver_lib esp_driver_sdmmc COMPONENT_LIB)
#    if(TARGET ${esp_hosted_lib})
#        get_target_property(lib_type ${esp_hosted_lib} TYPE)
#        if(lib_type STREQUAL "INTERFACE_LIBRARY")
#            target_link_libraries(${esp_hosted_lib} INTERFACE ${sdmmc_driver_lib})
#        else()
#            target_link_libraries(${esp_hosted_lib} PRIVATE ${sdmmc_driver_lib})
#        endif()
#    endif()
#endif()