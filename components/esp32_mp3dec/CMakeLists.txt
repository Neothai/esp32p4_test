# ค้นหาไฟล์ .c ทั้งหมดในโฟลเดอร์ esp32_mp3dec และโฟลเดอร์ย่อยอัตโนมัติ
file(GLOB_RECURSE MP3DEC_SRCS "audio_eq.c" "esp32_mp3dec.c" "*.c")

idf_component_register(
    SRCS ${MP3DEC_SRCS}
    INCLUDE_DIRS "." "helix" "helix/pub" "helix/real"
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_timer freertos esp32_fatfs
)