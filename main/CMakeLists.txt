file(GLOB_RECURSE APP_SOURCES 
    "vendingmc_ui/*.c"
    "vendingmc_ui/fonts/*.c"
    "*.c"             # รวมไฟล์ .c ที่อยู่ใน main/ ด้วย
)

idf_component_register(
    SRCS 
       SRCS ${APP_SOURCES}
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

#idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
#if(TARGET ${lvgl_lib})
#    target_include_directories(${lvgl_lib} PRIVATE 
#        "${IDF_PATH}/components/esp_driver_ppa/include"
#        "${IDF_PATH}/components/esp_mm/include"
#        "${IDF_PATH}/components/esp_hw_support/include"
#    )
#    target_link_libraries(${lvgl_lib} PRIVATE idf::esp_driver_ppa idf::esp_mm)
#endif()
