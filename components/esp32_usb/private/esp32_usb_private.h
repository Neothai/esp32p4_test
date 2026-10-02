/*
 * esp32_usb_private.h — ค่าคงที่และของภายในไลบรารี
 * ห้ามแก้ และห้าม include จากโค้ดผู้ใช้
 */
#ifndef ESP32_USB_PRIVATE_H
#define ESP32_USB_PRIVATE_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"

#include "usbh_core.h"
#include "esp32_usb_config.h"
#include "esp32_usbh_main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- ค่าคงที่ภายใน ห้ามแก้ ---------------- */

/* เพดานฮาร์ดแวร์ของ DWC2 บน ESP32-P4 — คำนวณจาก GHWCFG3 ตอนรันไทม์อีกที */
#define PRIV_DWC2_GHWCFG3_OFFSET    0x4C
#define PRIV_HARD_MAX_SEC_PER_CMD   127

/* timeout มาตรฐาน */
#define PRIV_EP0_MUTEX_WAIT_MS      3000
#define PRIV_MOUNT_RETRY_DELAY_MS   120
#define PRIV_STOP_JOIN_TIMEOUT_MS   800
#define PRIV_STOP_JOIN_STEP_MS      5
#define PRIV_HID_URB_TIMEOUT_MS     1000
#define PRIV_ENUM_SETTLE_MS         50

/* BOT (Bulk-Only Transport) */
#define PRIV_BOT_RESET_REQUEST      0xFF
#define PRIV_BOT_CLEAR_HALT_RETRY   2

/* ค่าที่ FatFS ใช้ */
#define PRIV_DEFAULT_BLOCK_SIZE     512

#define PRIV_TAG_MAIN               "esp32_usbh"
#define PRIV_TAG_MSC                "esp32_usbh_msc"
#define PRIV_TAG_HID                "esp32_usbh_hid"

#define PRIV_MSC_SPINUP_DELAY_MS    100
#define PRIV_SCSI_INIT_RETRY        6
#define PRIV_SCSI_INIT_DELAY_MS     300

/* ---------------- helper สร้าง task ตาม ESP32_USBH_TASK_CORE ---------------- */

BaseType_t priv_task_create(TaskFunction_t fn, const char *name, uint32_t stack,
                            void *arg, UBaseType_t prio, TaskHandle_t *out);

/* ---------------- EP0 / control-transfer serialisation ----------------
 *
 * usbh_control_transfer() ล็อก hport->mutex ให้ (ต่ออุปกรณ์)
 * แต่ ep0_request_buffer[busid] ใน usbh_core.c:21 เป็น buffer "ต่อบัส"
 * ที่ทุกอุปกรณ์ใช้ร่วมกัน -> ถ้าเราอ่าน string descriptor จาก task ของเรา
 * ขณะที่เธรด hub กำลัง enumerate อุปกรณ์อื่น ข้อมูลจะปนกัน
 *
 * ทุก control transfer ที่ไลบรารีนี้เป็นคนเริ่ม ต้องถือ mutex ตัวนี้
 */
bool priv_ep0_lock(uint32_t timeout_ms);
void priv_ep0_unlock(void);

/* ---------------- ส่งเหตุการณ์เข้า event task ---------------- */

/* เรียกได้จากทุกบริบทยกเว้น ISR */
bool priv_event_post(const esp32_usbh_event_t *ev);
/* เรียกจาก ISR เท่านั้น */
bool priv_event_post_isr(const esp32_usbh_event_t *ev, BaseType_t *hpw);

/* ค่า ESP32_USBH_MSC_AUTO_MOUNT ปัจจุบัน (เปลี่ยนได้ตอนรันไทม์) */
bool priv_auto_mount_enabled(void);

/* ---------------- แจ้ง core ว่าควรรีสตาร์ท host ---------------- */
void priv_request_host_restart(const char *reason);

/* ---------------- บันทึก/ลบข้อมูลอุปกรณ์ลงตาราง ---------------- */
/* out != NULL -> คืนสำเนาข้อมูลที่บันทึก (รวมสตริงที่เพิ่งดึงมา)
 * ใช้เพื่อไม่ต้องยิง control transfer ซ้ำในโมดูลลูก */
void priv_devtable_add(struct usbh_hubport *hport, uint8_t intf,
                       esp32_usbh_class_t cls, const char *devname,
                       esp32_usbh_devinfo_t *out);
/* intf == PRIV_DEVTABLE_ALL_INTF -> ลบทุก interface ที่ตำแหน่ง (hub, port) นั้น */
#define PRIV_DEVTABLE_ALL_INTF 0xFF
void priv_devtable_remove(struct usbh_hubport *hport, uint8_t intf);
void priv_devtable_clear(void);

/* ดึงสตริงจากอุปกรณ์ (ทนต่อ STALL — คืนสตริงว่างแทนที่จะล้มเหลว) */
void priv_fetch_strings(struct usbh_hubport *hport,
                        char *mfr, char *prod, char *serial, size_t cap);

/* ---------------- lifecycle ของแต่ละโมดูล (เรียกจาก usbh_main) ----------------
 *
 * สำคัญ: การเรียกฟังก์ชันเหล่านี้คือสิ่งที่ "บังคับให้ linker ดึง object file"
 * ของแต่ละโมดูลเข้ามา ถ้าไม่เรียก weak symbol usbh_msc_run()/usbh_hid_run()
 * ใน CherryUSB จะไม่ถูก override เลย (ESP-IDF ลิงก์คอมโพเนนต์เป็น .a)
 */
#if ESP32_USBH_ENABLE_MSC
esp_err_t priv_msc_start(void);
void      priv_msc_stop(void);
void      priv_msc_force_unmount_all(void);
#endif

#if ESP32_USBH_ENABLE_HID
esp_err_t priv_hid_start(void);
void      priv_hid_stop(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ESP32_USB_PRIVATE_H */
