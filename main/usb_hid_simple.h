/*
 * usb_hid_simple.h — ทดสอบเสียบเมาส์/คีย์บอร์ดแล้ว print ข้อมูลออกมา
 * ESP32-P4 + CherryUSB v1.6.1
 *
 * วิธีใช้:
 *   1) เพิ่ม usb_hid_simple.c เข้า CMakeLists ของ main
 *   2) ไม่ต้องเรียกอะไรเลย — ไฟล์นี้ override usbh_hid_run()/usbh_hid_stop()
 *      ซึ่งเป็น __WEAK ใน class/hid/usbh_hid.c (บรรทัด 546, 551)
 *   3) ตรวจว่าคอมโพเนนต์ cherryusb คอมไพล์ class/hid/usbh_hid.c เข้ามาด้วย:
 *        grep -rn "usbh_hid.c" managed_components/CMakeLists.txt
 *      ถ้าไม่มี ต้องเพิ่มเข้า SRCS ไม่งั้นจะไม่มี class driver มา match
 */
#ifndef USB_HID_SIMPLE_H
#define USB_HID_SIMPLE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* พิมพ์ raw report ทุกไบต์เป็น hex เสมอ (นอกเหนือจากที่ถอดความแล้ว)
 * มีประโยชน์ตอนเจออุปกรณ์ที่ไม่ใช่ boot protocol */
#ifndef USB_HID_DUMP_RAW
#define USB_HID_DUMP_RAW 1
#endif

/* บังคับใช้ Boot Protocol เมื่ออุปกรณ์รองรับ (bInterfaceSubClass == 1)
 * ทำให้ report เป็นรูปแบบตายตัว ไม่ต้อง parse report descriptor
 *   คีย์บอร์ด: 8 ไบต์ [modifier][reserved][keycode x6]
 *   เมาส์:     >=3 ไบต์ [buttons][dx][dy](+wheel)
 * ตั้ง 0 ถ้าอยากเห็น report ดิบตาม report descriptor จริง */
#ifndef USB_HID_FORCE_BOOT_PROTOCOL
#define USB_HID_FORCE_BOOT_PROTOCOL 1
#endif

/* ขนาด stack ของ task ที่อ่าน report (ต้องพอสำหรับ printf) */
#ifndef USB_HID_TASK_STACK
#define USB_HID_TASK_STACK 3584
#endif

#ifndef USB_HID_TASK_PRIO
#define USB_HID_TASK_PRIO 5
#endif

/* ⚠️ ต้องเรียกตัวนี้จาก app_main() ก่อน usbh_initialize()
 *
 * เหตุผล: ไฟล์นี้ทำงานด้วยการ override __WEAK symbol (usbh_hid_run/stop)
 * แต่ ESP-IDF ลิงก์คอมโพเนนต์เป็น static library (.a) — linker จะ "ดึง" object
 * ไฟล์ใดเข้ามาก็ต่อเมื่อมีใคร "อ้างถึง" สัญลักษณ์ในไฟล์นั้น
 *
 * ถ้าไม่มีใครเรียกอะไรใน usb_hid_simple.c เลย -> usb_hid_simple.o ถูกทิ้ง
 * -> symbol ที่ถูกใช้จริงคือ __WEAK ตัวเปล่าใน usbh_hid.c:546 ซึ่งไม่ทำอะไร
 * -> เสียบเมาส์แล้วเงียบสนิท
 *
 * (นี่คือเหตุผลที่ usb_msc_hardened.c ทำงานได้ เพราะ main.c เรียก
 *  usb_msc_hardened_init() อยู่แล้ว object จึงถูกดึงเข้ามา)
 *
 * ทางเลือกแทนการเรียกฟังก์ชันนี้ — บังคับที่ linker:
 *   target_link_libraries(${COMPONENT_LIB} INTERFACE "-u usbh_hid_run")
 */
void usb_hid_simple_init(void);

/* มีอุปกรณ์ HID เสียบอยู่กี่ตัว */
int usb_hid_device_count(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_HID_SIMPLE_H */
