# ค้นหาไฟล์ .c ทุกไฟล์ในโฟลเดอร์ปัจจุบัน
file(GLOB COMPONENT_SRCS "diskio.c" "*.c")

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS "."
    REQUIRES 
        sdmmc
        esp_driver_sdspi
        esp_driver_gpio
        freertos
)