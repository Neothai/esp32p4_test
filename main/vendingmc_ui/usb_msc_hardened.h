/*
 * usb_msc_hardened.h — USB MSC + FatFS layer ที่มี error recovery
 * สำหรับ ESP32-P4 + CherryUSB v1.6.1 + ESP-IDF (diskio_ops_t API)
 *
 * วิธีใช้:
 *   1) ลบ usbh_msc_run() / usbh_msc_stop() / usb_read_cb / usb_write_cb /
 *      usb_ioctl_cb / s_usb_ops / s_dma_bounce_buf / s_usb_drives ของเดิมใน main.c ทิ้ง
 *   2) เพิ่ม usb_msc_hardened.c เข้า CMakeLists
 *   3) เรียก usb_msc_hardened_init() ก่อน usbh_initialize()
 */
#ifndef USB_MSC_HARDENED_H
#define USB_MSC_HARDENED_H

#include <stdbool.h>
#include <stdint.h>
#include "usbh_core.h"
#include "usbh_msc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- ค่าที่ปรับได้ ---------------- */

/* จำนวน sector สูงสุดต่อหนึ่งคำสั่ง SCSI
 * - 64 sectors = 32 KB  ← ค่าปลอดภัย แนะนำให้เริ่มที่ค่านี้
 * - 128 = 64 KB (เท่าที่ RTL9210 ของคุณล้มเหลว)
 * - 256 = 128 KB (เร็วสุด แต่เสี่ยงกับอุปกรณ์ที่ NYET บ่อย)
 * ตั้งผ่าน -DUSB_MSC_MAX_SEC_PER_CMD=xx ใน CMakeLists ได้ */
#ifndef USB_MSC_MAX_SEC_PER_CMD
#define USB_MSC_MAX_SEC_PER_CMD 64
#endif

/* จำนวนครั้งที่ retry ต่อหนึ่ง disk_read/disk_write ก่อนยอมแพ้ */
#ifndef USB_MSC_IO_RETRY
#define USB_MSC_IO_RETRY 3
#endif

/* ล้มเหลวติดกันกี่ครั้ง ถึงจะสั่ง restart USB host ทั้งตัว */
#ifndef USB_MSC_FAIL_BEFORE_HOST_RESTART
#define USB_MSC_FAIL_BEFORE_HOST_RESTART 2
#endif

/* GPIO ที่ต่อกับ load switch ของ VBUS (active-high = จ่ายไฟ)
 * ตั้งเป็น -1 ถ้าบอร์ดไม่มี — แต่แนะนำอย่างยิ่งให้ใส่สำหรับงาน 24/7 */
#ifndef USB_MSC_VBUS_EN_GPIO
#define USB_MSC_VBUS_EN_GPIO (-1)
#endif

#ifndef USB_MSC_BUSID
#define USB_MSC_BUSID 0
#endif

/* เห็น DEVICE_CONNECTED กี่ครั้งติดกันโดยไม่เคยถึง DEVICE_CONFIGURED
 * ถึงจะถือว่า DWC2 ค้างแล้วสั่ง restart host
 * (= อาการที่ Test A เจอ: enumerate fail วนไม่หยุด) */
#ifndef USB_MSC_ENUM_FAIL_BEFORE_RESTART
#define USB_MSC_ENUM_FAIL_BEFORE_RESTART 3
#endif

/* ทำ cache sync เองหรือไม่
 *   1 = โมดูลนี้เรียก esp_cache_msync() เอง   <- ใช้เมื่อ usb_config.h ยังไม่เปิด DCACHE
 *   0 = ปล่อยให้ CherryUSB จัดการ            <- ใช้เมื่อเปิด CONFIG_USB_DCACHE_ENABLE
 *                                               + CONFIG_USB_ALIGN_SIZE 64 แล้ว
 * ⚠ อย่าเปิดทั้งสองทางพร้อมกันโดยไม่จำเป็น (ไม่ผิด แต่เสียเวลาเปล่า) */
#ifndef USB_MSC_DO_CACHE_SYNC
#  if defined(CONFIG_IDF_TARGET_ESP32P4) && !defined(CONFIG_USB_DCACHE_ENABLE)
#    define USB_MSC_DO_CACHE_SYNC 1
#  else
#    define USB_MSC_DO_CACHE_SYNC 0
#  endif
#endif

/* ตรวจความถูกต้องของข้อมูลตอน read benchmark (จับบั๊ก cache/DMA)
 * ปิดได้เมื่อมั่นใจแล้ว */
#ifndef USB_MSC_BENCH_VERIFY
#define USB_MSC_BENCH_VERIFY 1
#endif

/* ถ้า usbh_deinitialize() ค้างเกินกี่ ms ให้ esp_restart() เป็นทางออกสุดท้าย
 * 0 = ปิด. แนะนำ 15000 สำหรับเครื่องที่ต้องรันไม่มีคนดูแล */
#ifndef USB_MSC_PANIC_REBOOT_MS
#define USB_MSC_PANIC_REBOOT_MS 0
#endif

/* ---------------- API ---------------- */

/* เรียกครั้งเดียวก่อน usbh_initialize() */
int  usb_msc_hardened_init(void);

/* ต้องส่งตัวนี้เข้า usbh_initialize() แทน NULL ไม่งั้น enumeration watchdog ไม่ทำงาน
 *   usbh_initialize(0, (uintptr_t)ESP_USB_HS0_BASE, usb_msc_event_handler); */
void usb_msc_event_handler(uint8_t busid, uint8_t hub_index, uint8_t hub_port,
                           uint8_t intf, uint8_t event);

/* สถิติ — เรียกดูได้ทุกเมื่อ */
typedef struct {
    uint32_t rd_calls, wr_calls;
    uint32_t rd_sectors, wr_sectors;
    uint32_t rd_bounce, wr_bounce;
    uint64_t rd_us, wr_us;
    uint32_t hist_rd[9], hist_wr[9];   /* 1,2,4,8,16,32,64,128,256+ sectors */
    uint32_t io_errors;                /* จำนวน SCSI error ทั้งหมด */
    uint32_t bot_resets;               /* กู้สำเร็จด้วย BOT reset กี่ครั้ง */
    uint32_t host_restarts;            /* restart host stack กี่ครั้ง */
    uint32_t vbus_cycles;              /* power-cycle VBUS กี่ครั้ง */
} usb_msc_stats_t;

void usb_msc_stats_reset(void);
void usb_msc_stats_get(usb_msc_stats_t *out);
void usb_msc_stats_dump(const char *tag);

/* benchmark (เรียกจาก worker task เท่านั้น ห้ามเรียกจาก hub thread) */
void usb_storage_benchmark(const char *drive_path, uint32_t total_mb, uint32_t chunk_kb);

/* true = มีไดรฟ์ mount อยู่ */
bool usb_msc_is_mounted(void);

#ifdef __cplusplus
}
#endif
#endif /* USB_MSC_HARDENED_H */
