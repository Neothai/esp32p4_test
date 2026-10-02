/*
 * esp32_usbh_msc.h — แฟลชไดรฟ์ / SSD ผ่าน USB Mass Storage + FatFS
 *
 * 🔒 thread-safe ทุกฟังก์ชัน
 * 🔀 อ่าน/เขียน "คนละไดรฟ์" พร้อมกันได้จริง (ไม่มี global lock บนเส้นทาง I/O)
 *    ต้องเปิด FF_FS_REENTRANT ใน ffconf.h ด้วย — ดู README §FatFS
 * 🔌 รองรับ hot plug เต็มรูปแบบ: เสียบ/ถอดระหว่างเขียนได้ ไม่ค้าง ไม่ panic
 */
#ifndef ESP32_USBH_MSC_H
#define ESP32_USBH_MSC_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp32_usb_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     present;        /* มีไดรฟ์เสียบอยู่ */
    bool     mounted;        /* FatFS mount สำเร็จ */
    uint8_t  pdrv;           /* หมายเลขไดรฟ์ของ FatFS */
    char     path[4];        /* "0:" "1:" ... ใช้กับ f_open ได้เลย */
    char     devname[CONFIG_USBHOST_DEV_NAMELEN];  /* /dev/sda */

    uint32_t block_count;
    uint16_t block_size;
    uint64_t capacity_bytes;

    uint16_t vid, pid;
    char     product[ESP32_USBH_STR_LEN];

    /* FatFS */
    uint32_t cluster_bytes;  /* ขนาด cluster — ตัวกำหนดความเร็วหลัก */
    uint64_t free_bytes;
    uint64_t total_bytes;
} esp32_usbh_msc_info_t;

typedef struct {
    uint32_t rd_calls, wr_calls;
    uint32_t rd_sectors, wr_sectors;
    uint32_t rd_bounce, wr_bounce;   /* จำนวนครั้งที่ต้องผ่าน bounce buffer */
    uint64_t rd_us, wr_us;
    uint32_t retries;
    uint32_t bot_resets;
    uint32_t io_errors;
} esp32_usbh_msc_stats_t;

/* ---------------- get ---------------- */

/** จำนวนไดรฟ์ที่เสียบอยู่ */
int esp32_usbh_msc_get_count(void);
/** จำนวนไดรฟ์ที่ mount สำเร็จ */
int esp32_usbh_msc_get_mounted_count(void);
/**
 * ข้อมูลไดรฟ์ช่องที่ index (0..ESP32_USBH_MSC_MAX_DRIVES-1)
 * ⚡ เร็วเสมอ — free_bytes/total_bytes เป็นค่าที่ "แคชไว้ตอน mount"
 *    ไม่ได้เรียก f_getfree() ใหม่ จึงเรียกถี่ ๆ จาก UI ได้
 */
esp_err_t esp32_usbh_msc_get_info(int index, esp32_usbh_msc_info_t *out);

/**
 * คำนวณ free_bytes/total_bytes ใหม่
 * 🐢 ช้ามาก: f_getfree() สแกน FAT ทั้งตาราง (FAT32 16 GB อ่าน ~16 MB)
 *    ห้ามเรียกจากเธรด UI — ให้เรียกจาก task ของตัวเองแล้วค่อยอัปเดตหน้าจอ
 */
esp_err_t esp32_usbh_msc_refresh_usage(int index);
/** หา index จาก path เช่น "0:" */
int esp32_usbh_msc_get_index_by_path(const char *path);
/** สถิติ I/O; index < 0 = รวมทุกไดรฟ์ */
esp_err_t esp32_usbh_msc_get_stats(int index, esp32_usbh_msc_stats_t *out);
/** ค่า sector/คำสั่งที่ใช้อยู่ */
uint32_t esp32_usbh_msc_get_max_sectors_per_cmd(void);

/* ---------------- set ---------------- */

/**
 * ปรับจำนวน sector สูงสุดต่อคำสั่ง SCSI ขณะรันไทม์
 * @note จะถูก clamp ให้อยู่ใน 1..min(127, เพดานจาก GHWCFG3) โดยอัตโนมัติ
 *       คืน ESP_ERR_INVALID_ARG ถ้าค่าที่ขอถูก clamp
 */
esp_err_t esp32_usbh_msc_set_max_sectors_per_cmd(uint32_t sectors);

/** เปิด/ปิด auto-mount เมื่อเสียบไดรฟ์ */
esp_err_t esp32_usbh_msc_set_auto_mount(bool enable);

/* ---------------- การทำงาน ---------------- */

esp_err_t esp32_usbh_msc_mount(int index);
esp_err_t esp32_usbh_msc_unmount(int index);

/**
 * อ่าน/เขียนระดับ LBA โดยตรง (ข้าม FatFS)
 * @note thread-safe และซอยคำสั่งตามเพดานฮาร์ดแวร์ให้อัตโนมัติ
 */
esp_err_t esp32_usbh_msc_read(int index, uint32_t lba, void *buf, uint32_t nsec);
esp_err_t esp32_usbh_msc_write(int index, uint32_t lba, const void *buf, uint32_t nsec);

/** รีเซ็ตตัวนับสถิติ; index < 0 = ทุกไดรฟ์ */
esp_err_t esp32_usbh_msc_reset_stats(int index);

/**
 * ล็อกไดรฟ์ไว้ใช้งานต่อเนื่อง (เช่นจะเขียนไฟล์ชุดใหญ่โดยไม่อยากให้ task อื่นแทรก)
 * @note ไม่จำเป็นสำหรับการใช้งานปกติ — FatFS กับไลบรารีนี้ thread-safe อยู่แล้ว
 */
bool esp32_usbh_msc_lock(int index, uint32_t timeout_ms);
void esp32_usbh_msc_unlock(int index);

#ifdef __cplusplus
}
#endif

#endif /* ESP32_USBH_MSC_H */
