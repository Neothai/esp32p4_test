#
# components/esp32_usb/CMakeLists.txt
#
# ⚠️ สำคัญมาก — ปัญหา weak symbol กับ ESP-IDF
#
# CherryUSB ประกาศ usbh_msc_run()/usbh_msc_stop()/usbh_hid_run()/usbh_hid_stop()
# เป็น __WEAK  เราจึง override ด้วยการนิยามทับในไฟล์ของเรา
#
# แต่ ESP-IDF ลิงก์คอมโพเนนต์เป็น static archive (.a)  linker จะดึง object file
# เข้ามา "ก็ต่อเมื่อ" มีสัญลักษณ์ในไฟล์นั้นถูกอ้างถึงแล้วเท่านั้น
# ถ้า esp32_usbh_msc.o มีแต่ตัว override  มันจะไม่ถูกดึงเข้า -> weak เดิมชนะเงียบ ๆ
#
# แก้สองชั้น (ใส่ทั้งคู่กันพลาด):
#   1) esp32_usbh_main.c เรียก priv_msc_start()/priv_hid_start() -> มีการอ้างถึงจริง
#   2) -u บังคับ linker ให้ undefined symbol เหล่านี้ถูกค้นหา
#

idf_component_register(
    SRCS
        "esp32_usbh_main.c"
        "esp32_usbh_msc.c"
        "esp32_usbh_hid.c"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "private"
    REQUIRES
        esp32_fatfs          # FatFS ฉบับ fork ของโปรเจกต์ (diskio_ops_t)
        cherryusb
    PRIV_REQUIRES
        esp_timer
        esp_driver_gpio
        esp_mm
        esp_hw_support
        driver
)

target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-u usbh_msc_run"
    "-u usbh_msc_stop"
    "-u usbh_hid_run"
    "-u usbh_hid_stop"
)

# คอมไพล์เข้มขึ้นเล็กน้อย — จับ bug ตั้งแต่ build
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers
)