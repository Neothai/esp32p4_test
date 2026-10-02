/*
 * esp32_usb_config.h — ค่าคงที่ส่วน PUBLIC
 * ผู้ใช้แก้ไขได้ทุกตัวในไฟล์นี้ (หรือ -D ตอน build / นิยามก่อน include)
 * ทุกค่ามีการ validate ด้วย _Static_assert — ถ้าใส่เกินขอบเขตจะ error ตอนคอมไพล์
 *
 * ค่าคงที่ส่วน PRIVATE อยู่ที่ private/esp32_usb_private.h — ห้ามแก้
 */
#ifndef ESP32_USB_CONFIG_H
#define ESP32_USB_CONFIG_H

#include "usbh_core.h"   /* ดึง usb_config.h ของ CherryUSB มาด้วย */

/* ==========================================================================
 *  1) CORE — usbh_main
 * ========================================================================== */

/** busid ที่ใช้ (ESP32-P4 มี 2 คอนโทรลเลอร์: 0 = HS, 1 = FS) */
#ifndef ESP32_USBH_BUSID
#define ESP32_USBH_BUSID 0
#endif
_Static_assert(ESP32_USBH_BUSID >= 0 && ESP32_USBH_BUSID < CONFIG_USBHOST_MAX_BUS,
               "ESP32_USBH_BUSID ต้องอยู่ในช่วง 0..CONFIG_USBHOST_MAX_BUS-1");

/** ขนาดคิวเหตุการณ์ที่ส่งจาก hub-thread/ISR ไปยัง event task */
#ifndef ESP32_USBH_EVENT_QUEUE_LEN
#define ESP32_USBH_EVENT_QUEUE_LEN 24
#endif
_Static_assert(ESP32_USBH_EVENT_QUEUE_LEN >= 8 && ESP32_USBH_EVENT_QUEUE_LEN <= 256,
               "ESP32_USBH_EVENT_QUEUE_LEN ต้องอยู่ในช่วง 8..256");

/** stack ของ event task (callback ของผู้ใช้ทำงานบนนี้) */
#ifndef ESP32_USBH_EVENT_TASK_STACK
#define ESP32_USBH_EVENT_TASK_STACK 4096
#endif
_Static_assert(ESP32_USBH_EVENT_TASK_STACK >= 2560 && ESP32_USBH_EVENT_TASK_STACK <= 32768,
               "ESP32_USBH_EVENT_TASK_STACK ต้องอยู่ในช่วง 2560..32768");

/** priority ของ event task — ต้องต่ำกว่าเธรด hub (24) */
#ifndef ESP32_USBH_EVENT_TASK_PRIO
#define ESP32_USBH_EVENT_TASK_PRIO 6
#endif
_Static_assert(ESP32_USBH_EVENT_TASK_PRIO >= 1 && ESP32_USBH_EVENT_TASK_PRIO <= 20,
               "ESP32_USBH_EVENT_TASK_PRIO ต้องอยู่ในช่วง 1..20 (ห้ามเกินเธรด hub)");

/** core ที่ปักหมุด task ของไลบรารี: 0, 1 หรือ -1 (ไม่ปัก) */
#ifndef ESP32_USBH_TASK_CORE
#define ESP32_USBH_TASK_CORE -1
#endif
_Static_assert(ESP32_USBH_TASK_CORE >= -1 && ESP32_USBH_TASK_CORE <= 1,
               "ESP32_USBH_TASK_CORE ต้องเป็น -1, 0 หรือ 1");

/** ความยาวสูงสุดของสตริง manufacturer / product / serial (รวม NUL) */
#ifndef ESP32_USBH_STR_LEN
#define ESP32_USBH_STR_LEN 40
#endif
_Static_assert(ESP32_USBH_STR_LEN >= 16 && ESP32_USBH_STR_LEN <= 128,
               "ESP32_USBH_STR_LEN ต้องอยู่ในช่วง 16..128");

/** ดึง string descriptor (ชื่อผู้ผลิต/ชื่อรุ่น/serial) ตอนอุปกรณ์เสียบหรือไม่
 *  ไลบรารีนี้ดึงเอง ไม่ได้ใช้ CONFIG_USBHOST_GET_STRING_DESC ของ CherryUSB
 *  เพราะของ CherryUSB จะทำให้ enumeration "ล้มเหลวทั้งอัน" ถ้าอุปกรณ์ STALL */
#ifndef ESP32_USBH_FETCH_STRINGS
#define ESP32_USBH_FETCH_STRINGS 1
#endif

/** จำนวนอุปกรณ์ที่เก็บข้อมูลไว้ให้ esp32_usbh_get_device_info() อ่าน */
#ifndef ESP32_USBH_MAX_DEVICES
#define ESP32_USBH_MAX_DEVICES 8
#endif
_Static_assert(ESP32_USBH_MAX_DEVICES >= 2 && ESP32_USBH_MAX_DEVICES <= 32,
               "ESP32_USBH_MAX_DEVICES ต้องอยู่ในช่วง 2..32");

/** CONNECTED แล้วไม่ CONFIGURED กี่ครั้งติดกัน ถึงจะรีสตาร์ท host
 *  (CherryUSB ไม่มี event แจ้ง enumerate fail — hub.c:652 แค่ log เฉย ๆ) */
#ifndef ESP32_USBH_ENUM_FAIL_LIMIT
#define ESP32_USBH_ENUM_FAIL_LIMIT 3
#endif
_Static_assert(ESP32_USBH_ENUM_FAIL_LIMIT >= 2 && ESP32_USBH_ENUM_FAIL_LIMIT <= 20,
               "ESP32_USBH_ENUM_FAIL_LIMIT ต้องอยู่ในช่วง 2..20");

/** ห้าม restart host ถี่กว่านี้ (ms) กันลูป restart ไม่รู้จบ */
#ifndef ESP32_USBH_RESTART_COOLDOWN_MS
#define ESP32_USBH_RESTART_COOLDOWN_MS 10000
#endif
_Static_assert(ESP32_USBH_RESTART_COOLDOWN_MS >= 1000 && ESP32_USBH_RESTART_COOLDOWN_MS <= 600000,
               "ESP32_USBH_RESTART_COOLDOWN_MS ต้องอยู่ในช่วง 1000..600000");

/** ถ้า usbh_deinitialize() ค้างเกินกี่ ms ให้ esp_restart()
 *  0 = ปิด (แต่เสี่ยงค้างถาวร เพราะ usbh_hub_deinitialize รอ hub_sem แบบไม่มี timeout)
 *  แนะนำ 15000 */
#ifndef ESP32_USBH_PANIC_REBOOT_MS
#define ESP32_USBH_PANIC_REBOOT_MS 0
#endif
_Static_assert(ESP32_USBH_PANIC_REBOOT_MS == 0 ||
               (ESP32_USBH_PANIC_REBOOT_MS >= 3000 && ESP32_USBH_PANIC_REBOOT_MS <= 120000),
               "ESP32_USBH_PANIC_REBOOT_MS ต้องเป็น 0 หรืออยู่ในช่วง 3000..120000");

/** GPIO ที่ควบคุม VBUS load switch, -1 = ไม่มี */
#ifndef ESP32_USBH_VBUS_GPIO
#define ESP32_USBH_VBUS_GPIO (-1)
#endif
_Static_assert(ESP32_USBH_VBUS_GPIO >= -1 && ESP32_USBH_VBUS_GPIO <= 56,
               "ESP32_USBH_VBUS_GPIO ต้องเป็น -1 หรือขา GPIO ที่ถูกต้อง");

/** ระดับลอจิกที่ทำให้ VBUS "เปิด" */
#ifndef ESP32_USBH_VBUS_ACTIVE_HIGH
#define ESP32_USBH_VBUS_ACTIVE_HIGH 1
#endif

/* ==========================================================================
 *  2) MSC (แฟลชไดรฟ์ / SSD)
 * ========================================================================== */

/** เปิดใช้โมดูล MSC */
#ifndef ESP32_USBH_ENABLE_MSC
#define ESP32_USBH_ENABLE_MSC 1
#endif

/** จำนวนไดรฟ์สูงสุดที่ mount พร้อมกันได้
 *  ต้อง <= CONFIG_USBHOST_MAX_MSC_CLASS และ <= FF_VOLUMES ของ FatFS */
#ifndef ESP32_USBH_MSC_MAX_DRIVES
#define ESP32_USBH_MSC_MAX_DRIVES CONFIG_USBHOST_MAX_MSC_CLASS
#endif
_Static_assert(ESP32_USBH_MSC_MAX_DRIVES >= 1 && ESP32_USBH_MSC_MAX_DRIVES <= 8,
               "ESP32_USBH_MSC_MAX_DRIVES ต้องอยู่ในช่วง 1..8");
_Static_assert(ESP32_USBH_MSC_MAX_DRIVES <= CONFIG_USBHOST_MAX_MSC_CLASS,
               "ESP32_USBH_MSC_MAX_DRIVES ต้องไม่เกิน CONFIG_USBHOST_MAX_MSC_CLASS ใน usb_config.h");

/** จำนวน sector สูงสุดต่อ 1 คำสั่ง SCSI
 *
 *  ⚠️ ขอบเขตฮาร์ดแวร์ ไม่ใช่ค่าปรับจูน
 *  ESP32-P4 GHWCFG3 = 0x03805eb5:
 *      XFERSIZEWIDTH = 5 -> max transfer = 2^(5+11)-1 = 65535 ไบต์
 *      PKTSIZEWIDTH  = 3 -> max packet   = 2^(3+4)-1  = 127 แพ็กเก็ต
 *  เพดานจริง = min(65535, 127*512) = 65024 ไบต์ = 127 sectors
 *
 *  แต่ CherryUSB v1.6.1 (usb_hc_dwc2.c:512) ฮาร์ดโค้ด pktcnt ไว้ที่ 0x3FF
 *  ทำให้ transfer 64 KB (128 แพ็กเก็ต) ล้นฟิลด์ PKTCNT 7 บิตเป็น 0
 *  -> channel ไม่เริ่มทำงาน -> timeout -> error -14
 *
 *  64 = แนะนำ (หาร 128/256 ลงตัว ไม่เกิดเศษ)
 *  127 = เพดานฮาร์ดแวร์ แต่หารไม่ลงตัว
 *  128 = ❌ พังแน่นอน */
#ifndef ESP32_USBH_MSC_MAX_SEC_PER_CMD
#define ESP32_USBH_MSC_MAX_SEC_PER_CMD 64
#endif
_Static_assert(ESP32_USBH_MSC_MAX_SEC_PER_CMD >= 1 && ESP32_USBH_MSC_MAX_SEC_PER_CMD <= 127,
               "ESP32_USBH_MSC_MAX_SEC_PER_CMD ต้องอยู่ในช่วง 1..127 "
               "(128 ขึ้นไปจะล้นฟิลด์ PKTCNT 7 บิตของ DWC2 บน ESP32-P4)");

/** ขนาด bounce buffer เป็น sector (ใช้เมื่อ buffer ของผู้เรียกไม่ aligned) */
#ifndef ESP32_USBH_MSC_BOUNCE_SECTORS
#define ESP32_USBH_MSC_BOUNCE_SECTORS 64
#endif
_Static_assert(ESP32_USBH_MSC_BOUNCE_SECTORS >= 8 && ESP32_USBH_MSC_BOUNCE_SECTORS <= 256,
               "ESP32_USBH_MSC_BOUNCE_SECTORS ต้องอยู่ในช่วง 8..256");

/** ลองซ้ำกี่ครั้งก่อนยอมแพ้ในแต่ละคำสั่ง */
#ifndef ESP32_USBH_MSC_IO_RETRY
#define ESP32_USBH_MSC_IO_RETRY 3
#endif
_Static_assert(ESP32_USBH_MSC_IO_RETRY >= 0 && ESP32_USBH_MSC_IO_RETRY <= 10,
               "ESP32_USBH_MSC_IO_RETRY ต้องอยู่ในช่วง 0..10");

/** ล้มเหลวติดกันกี่ครั้ง (พร้อม EP0 ตาย) ถึงจะรีสตาร์ท host */
#ifndef ESP32_USBH_MSC_FAIL_BEFORE_RESTART
#define ESP32_USBH_MSC_FAIL_BEFORE_RESTART 2
#endif
_Static_assert(ESP32_USBH_MSC_FAIL_BEFORE_RESTART >= 1 && ESP32_USBH_MSC_FAIL_BEFORE_RESTART <= 20,
               "ESP32_USBH_MSC_FAIL_BEFORE_RESTART ต้องอยู่ในช่วง 1..20");

/** mount FatFS อัตโนมัติเมื่อเสียบไดรฟ์ */
#ifndef ESP32_USBH_MSC_AUTO_MOUNT
#define ESP32_USBH_MSC_AUTO_MOUNT 1
#endif

/** stack ของ worker task ที่ทำ mount/unmount (ต้องพอสำหรับ FatFS) */
#ifndef ESP32_USBH_MSC_TASK_STACK
#define ESP32_USBH_MSC_TASK_STACK 6144
#endif
_Static_assert(ESP32_USBH_MSC_TASK_STACK >= 4096 && ESP32_USBH_MSC_TASK_STACK <= 32768,
               "ESP32_USBH_MSC_TASK_STACK ต้องอยู่ในช่วง 4096..32768");

#ifndef ESP32_USBH_MSC_TASK_PRIO
#define ESP32_USBH_MSC_TASK_PRIO 6
#endif
_Static_assert(ESP32_USBH_MSC_TASK_PRIO >= 1 && ESP32_USBH_MSC_TASK_PRIO <= 20,
               "ESP32_USBH_MSC_TASK_PRIO ต้องอยู่ในช่วง 1..20");

/* ==========================================================================
 *  3) HID (เมาส์ / คีย์บอร์ด / อื่น ๆ)
 * ========================================================================== */

/** เปิดใช้โมดูล HID */
#ifndef ESP32_USBH_ENABLE_HID
#define ESP32_USBH_ENABLE_HID 1
#endif

/** จำนวน HID interface สูงสุด (dongle combo 1 ตัว = 2 interface) */
#ifndef ESP32_USBH_HID_MAX_DEVICES
#define ESP32_USBH_HID_MAX_DEVICES CONFIG_USBHOST_MAX_HID_CLASS
#endif
_Static_assert(ESP32_USBH_HID_MAX_DEVICES >= 1 && ESP32_USBH_HID_MAX_DEVICES <= 8,
               "ESP32_USBH_HID_MAX_DEVICES ต้องอยู่ในช่วง 1..8");
_Static_assert(ESP32_USBH_HID_MAX_DEVICES <= CONFIG_USBHOST_MAX_HID_CLASS,
               "ESP32_USBH_HID_MAX_DEVICES ต้องไม่เกิน CONFIG_USBHOST_MAX_HID_CLASS");

/** ขนาด report buffer ต่ออุปกรณ์ (ไบต์) */
#ifndef ESP32_USBH_HID_REPORT_SIZE
#define ESP32_USBH_HID_REPORT_SIZE 64
#endif
_Static_assert(ESP32_USBH_HID_REPORT_SIZE >= 8 && ESP32_USBH_HID_REPORT_SIZE <= 256,
               "ESP32_USBH_HID_REPORT_SIZE ต้องอยู่ในช่วง 8..256");

/** บังคับ Boot Protocol เมื่ออุปกรณ์รองรับ (bInterfaceSubClass == 1)
 *  ทำให้ report เป็นรูปแบบตายตัว ไม่ต้อง parse report descriptor */
#ifndef ESP32_USBH_HID_FORCE_BOOT
#define ESP32_USBH_HID_FORCE_BOOT 1
#endif

/** stack / priority ของ task อ่าน report (หนึ่งตัวต่อหนึ่ง interface) */
#ifndef ESP32_USBH_HID_TASK_STACK
#define ESP32_USBH_HID_TASK_STACK 3584
#endif
_Static_assert(ESP32_USBH_HID_TASK_STACK >= 2560 && ESP32_USBH_HID_TASK_STACK <= 16384,
               "ESP32_USBH_HID_TASK_STACK ต้องอยู่ในช่วง 2560..16384");

#ifndef ESP32_USBH_HID_TASK_PRIO
#define ESP32_USBH_HID_TASK_PRIO 7
#endif
_Static_assert(ESP32_USBH_HID_TASK_PRIO >= 1 && ESP32_USBH_HID_TASK_PRIO <= 20,
               "ESP32_USBH_HID_TASK_PRIO ต้องอยู่ในช่วง 1..20");

/* ==========================================================================
 *  ตรวจความเข้ากันได้กับ usb_config.h ของ CherryUSB
 * ========================================================================== */

#if defined(CONFIG_IDF_TARGET_ESP32P4) && !defined(CONFIG_USB_DCACHE_ENABLE)
#  warning "แนะนำให้เปิด CONFIG_USB_DCACHE_ENABLE ใน usb_config.h บน ESP32-P4 " \
           "ไม่งั้นไลบรารีต้องทำ cache sync เอง (ช้ากว่าและพลาดง่ายกว่า)"
#endif

_Static_assert(CONFIG_USBHOST_MAX_EXTHUBS >= 1,
               "CONFIG_USBHOST_MAX_EXTHUBS ต้อง >= 1 ถึงจะใช้ฮับได้; "
               "ฮับคาสเคด (เช่น CH334 2 ตัวในกล่องเดียว) ต้อง >= 2");

#endif /* ESP32_USB_CONFIG_H */
