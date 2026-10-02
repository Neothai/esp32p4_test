/*
 * esp32_usb_private.h — ค่าคงที่และของภายในไลบรารี
 * ⚠️ ห้ามแก้ และห้าม include จากโค้ดผู้ใช้
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
#define PRIV_STOP_JOIN_TIMEOUT_MS   2000  /* ต้องมากกว่า ALIVE_CHECK + DRAIN รวมกัน */
#define PRIV_STOP_JOIN_STEP_MS      5
/* เดิมเป็น timeout ของ usbh_submit_urb แบบ blocking — เลิกใช้แล้ว
 * (ทำให้เกิด usbh_kill_urb -> chan->urb = NULL -> ISR deref NULL)
 * ตอนนี้เป็นแค่คาบ "ตื่นมาเช็กว่ายังมีชีวิต" ของ task เท่านั้น */
/* timeout ของ interrupt-IN — ตั้งยาวมากโดยเจตนา
 * ทางออก errout_timeout ของ CherryUSB เรียก usbh_kill_urb() หลังเคลียร์
 * urb->timeout = 0 ซึ่งทำให้ไปจบที่ dwc2_chan_free() = ต้นเหตุ NULL deref
 * เราจึง "ห้าม" ให้มันหมดเวลาเอง ใช้ usbh_kill_urb() จาก usbh_hid_stop() แทน */
#define PRIV_HID_URB_TIMEOUT_MS     250
#define PRIV_HID_SPLIT_MIN_POLL_MS  8     /* คาบขั้นต่ำเมื่ออุปกรณ์ FS/LS อยู่หลัง hub */
#define PRIV_HID_DESC_MAX           256   /* report descriptor ของเมาส์/คีย์บอร์ดไม่เคยเกินนี้ */
#define PRIV_HID_TIMEOUT_LOG_EVERY  20

/* ตำแหน่งบิตของแต่ละฟิลด์ในเมาส์ report (ได้จากการแกะ report descriptor) */
typedef struct {
    bool     valid;
    uint8_t  report_id;     /* 0 = report นี้ไม่มี ID นำหน้า */
    uint16_t total_bits;
    uint16_t btn_off; uint8_t btn_cnt;
    uint16_t x_off;   uint8_t x_size;
    uint16_t y_off;   uint8_t y_size;
    uint16_t w_off;   uint8_t w_size;
} priv_mouse_map_t;
#define PRIV_ENUM_SETTLE_MS         50

/* SCSI bring-up หลังเสียบ (TEST UNIT READY / REQUEST SENSE / READ CAPACITY) */
#define PRIV_MSC_SPINUP_DELAY_MS    100   /* รอให้ไดรฟ์ตื่นก่อนคุย SCSI ครั้งแรก */
#define PRIV_SCSI_INIT_RETRY        6     /* จำนวนรอบที่ลอง usbh_msc_scsi_init() */
#define PRIV_SCSI_INIT_DELAY_MS     300   /* หน่วงระหว่างรอบ */

/* BOT (Bulk-Only Transport) */
#define PRIV_BOT_RESET_REQUEST      0xFF
#define PRIV_BOT_CLEAR_HALT_RETRY   2

/* ค่าที่ FatFS ใช้ */
#define PRIV_DEFAULT_BLOCK_SIZE     512

#define PRIV_TAG_MAIN               "esp32_usbh"
#define PRIV_TAG_MSC                "esp32_usbh_msc"
#define PRIV_TAG_HID                "esp32_usbh_hid"

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
 * ⚠️ สำคัญ: การเรียกฟังก์ชันเหล่านี้คือสิ่งที่ "บังคับให้ linker ดึง object file"
 * ของแต่ละโมดูลเข้ามา ถ้าไม่เรียก weak symbol usbh_msc_run()/usbh_hid_run()
 * ใน CherryUSB จะไม่ถูก override เลย (ESP-IDF ลิงก์คอมโพเนนต์เป็น .a)
 */
/* ---------------- ตัวช่วยจองหน่วยความจำ (esp32_usbh_main.c) ---------------- */

/** แรมภายในที่เหลือ (ไบต์) */
size_t priv_internal_free(void);

/**
 * จองก้อนที่ "ไม่ใช่ DMA"
 * ถ้าแรมภายในจะเหลือต่ำกว่า ESP32_USBH_INTERNAL_RESERVE_KB -> ไปจองบน PSRAM
 * @param what ข้อความสำหรับ log เช่น "FATFS"
 */
void *priv_alloc(size_t size, const char *what);

/**
 * จองบัฟเฟอร์ DMA โดยลดขนาดลงทีละครึ่งถ้าแรมภายในไม่พอ
 * @param want     ขนาดที่อยากได้ (ไบต์)
 * @param min      ขนาดต่ำสุดที่ยังยอมรับได้
 * @param out_size ขนาดที่จองได้จริง
 * @return พอยน์เตอร์ หรือ NULL ถ้าแม้แต่ขนาดต่ำสุดก็ไม่ไหว
 */
void *priv_alloc_dma(size_t want, size_t min, size_t *out_size, const char *what);

void priv_mem_free(void *p);

/** พิมพ์สรุปแรมลง log */
void priv_mem_report(const char *stage);

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
