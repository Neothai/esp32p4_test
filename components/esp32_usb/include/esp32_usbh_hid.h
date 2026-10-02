/*
 * esp32_usbh_hid.h — เมาส์ / คีย์บอร์ด / HID อื่น ๆ
 *
 * 🔒 thread-safe ทุกฟังก์ชัน
 * 🔔 report ถูกส่งออกเป็น event (ESP32_USBH_EV_HID_REPORT) ผ่าน event task
 *    จึงไม่เคยถูกเรียกจาก ISR — ผู้ใช้ประมวลผลหนักได้
 * 🔌 hot plug เต็มรูปแบบ
 */
#ifndef ESP32_USBH_HID_H
#define ESP32_USBH_HID_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp32_usb_config.h"
#include "esp32_usbh_main.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool                  present;
    esp32_usbh_hid_kind_t kind;
    bool                  boot_protocol;   /* ใช้ boot protocol อยู่หรือไม่ */

    uint8_t  interface;
    uint8_t  ep_addr;
    uint16_t ep_mps;
    uint8_t  bInterval;
    uint8_t  poll_ms;                      /* คาบที่ไลบรารีใช้จริง */
    esp32_usbh_speed_t speed;

    uint16_t vid, pid;
    char     product[ESP32_USBH_STR_LEN];
    char     devname[CONFIG_USBHOST_DEV_NAMELEN];   /* /dev/input0 */

    uint32_t report_count;                 /* จำนวน report ที่รับมาแล้ว */
    uint32_t error_count;
} esp32_usbh_hid_info_t;

/* ---------------- LED ของคีย์บอร์ด ---------------- */
#define ESP32_USBH_KBD_LED_NUMLOCK    0x01
#define ESP32_USBH_KBD_LED_CAPSLOCK   0x02
#define ESP32_USBH_KBD_LED_SCROLLLOCK 0x04

/* ---------------- get ---------------- */

int       esp32_usbh_hid_get_count(void);
esp_err_t esp32_usbh_hid_get_info(int index, esp32_usbh_hid_info_t *out);
/** หา index ตัวแรกที่เป็นชนิดที่ต้องการ; -1 = ไม่เจอ */
int       esp32_usbh_hid_get_index_by_kind(esp32_usbh_hid_kind_t kind);
/** สำเนา report ล่าสุด คืนจำนวนไบต์ หรือ <0 ถ้าไม่มี */
int       esp32_usbh_hid_get_last_report(int index, uint8_t *buf, size_t cap);
/** ดึง HID report descriptor ดิบ (สำหรับอุปกรณ์ที่ไม่ใช่ boot protocol) */
int       esp32_usbh_hid_get_report_descriptor(int index, uint8_t *buf, size_t cap);
/** แปลง HID usage id -> ตัวอักษร ASCII; คืน 0 ถ้าแปลงไม่ได้ */
char      esp32_usbh_hid_keycode_to_char(uint8_t keycode, bool shift);
/** ชื่อปุ่มพิเศษ เช่น "ENTER"; คืน NULL ถ้าไม่ใช่ปุ่มพิเศษ */
const char *esp32_usbh_hid_keycode_name(uint8_t keycode);

/* ---------------- set ---------------- */

/** ตั้งไฟ LED ของคีย์บอร์ด (บิตรวมของ ESP32_USBH_KBD_LED_*) */
esp_err_t esp32_usbh_hid_set_leds(int index, uint8_t led_bits);
/** สลับ boot / report protocol เอง */
esp_err_t esp32_usbh_hid_set_protocol(int index, bool boot);
/** ตั้งคาบ polling เอง (1..32 ms); 0 = กลับไปใช้ค่าจาก bInterval */
esp_err_t esp32_usbh_hid_set_poll_interval(int index, uint8_t ms);

#ifdef __cplusplus
}
#endif

#endif /* ESP32_USBH_HID_H */
