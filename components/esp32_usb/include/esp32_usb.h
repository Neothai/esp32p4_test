/*
 * esp32_usb.h — หัวไฟล์รวมของไลบรารี esp32_usb
 *
 *   #include "esp32_usb.h"   แล้วใช้ได้ทุกโมดูล
 *
 * กฎการตั้งชื่อ
 *   esp32_usbh_*   = ฝั่ง host   (h = host)
 *   esp32_usbd_*   = ฝั่ง device (d = device)  — สำรองไว้สำหรับอนาคต
 *
 *   esp32_usbh_init()      เปิดทุกโมดูล host ด้วยคำสั่งเดียว
 *   esp32_usbh_deinit()    ปิดทั้งหมด
 *   esp32_usbh_set_xxx()   ตั้งค่า
 *   esp32_usbh_get_xxx()   อ่านค่า
 *
 * โมดูลเฉพาะทางใช้ชื่อขยายต่อ เช่น
 *   esp32_usbh_msc_get_info(), esp32_usbh_hid_set_leds()
 */
#ifndef ESP32_USB_H
#define ESP32_USB_H

#include "esp32_usb_config.h"
#include "esp32_usbh_main.h"

#if ESP32_USBH_ENABLE_MSC
#include "esp32_usbh_msc.h"
#endif

#if ESP32_USBH_ENABLE_HID
#include "esp32_usbh_hid.h"
#endif

#define ESP32_USB_VERSION_MAJOR 1
#define ESP32_USB_VERSION_MINOR 0
#define ESP32_USB_VERSION_PATCH 0
#define ESP32_USB_VERSION_STR   "1.0.0"

#endif /* ESP32_USB_H */
