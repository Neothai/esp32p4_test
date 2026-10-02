/*
 * app_fs_bridge.h — ตัวเชื่อม USB-MSC / SD / Flash เข้ากับหน้า File Explorer ของ vd_menu
 *
 * วิธีใช้สั้น ๆ
 *   1) app_fs_bridge_init();                       ← เรียกครั้งเดียวตอนบูต
 *   2) app_fs_bridge_set_refresh_cb(my_refresh);   ← ให้ UI รีเฟรชตอนเสียบ/ถอด USB
 *   3) ใน create_file_setting_page() ใช้ app_fs_get_browser_cbs() แทน mock เดิม
 *   4) ใน on_usb() เรียก app_fs_notify_usb_changed() เมื่อไดรฟ์ mount/unmount
 */
#ifndef APP_FS_BRIDGE_H
#define APP_FS_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "vendingmc_ui/vd_menu.h"   /* ← ปรับ path ให้ตรงกับโปรเจกต์ */

#ifdef __cplusplus
extern "C" {
#endif

/* ชนิดของ backend ที่อยู่เบื้องหลังไดรฟ์ */
typedef enum {
    APP_FS_BK_FATFS = 0,   /* ใช้ f_opendir/f_readdir  prefix = "0:" "1:" ... */
    APP_FS_BK_VFS,         /* ใช้ opendir/readdir ของ POSIX  prefix = "/flash" */
} app_fs_backend_t;

/** เรียกครั้งเดียวตอนบูต (หลัง esp32_usbh_init ก็ได้ ไม่สำคัญ) */
esp_err_t app_fs_bridge_init(void);

/**
 * ลงทะเบียนไดรฟ์ที่ "อยู่ถาวร" เช่น แฟลชภายใน หรือการ์ด SD
 * ไดรฟ์ USB ไม่ต้องลงทะเบียน ระบบดึงจาก esp32_usbh_msc_* ให้เอง
 *
 * @param id      ตัวระบุสั้น ๆ ห้ามมี '/' เช่น "flash", "sd"
 * @param name    ชื่อที่โชว์บนการ์ด เช่น "หน่วยความจำภายใน"
 * @param backend APP_FS_BK_FATFS หรือ APP_FS_BK_VFS
 * @param prefix  "0:" สำหรับ FatFS  หรือ "/flash" สำหรับ VFS
 */
esp_err_t app_fs_register_static_drive(const char *id, const char *name,
                                       app_fs_backend_t backend, const char *prefix);

/** ถอดไดรฟ์ถาวรออกจากรายการ (เช่น ถอดการ์ด SD) */
void app_fs_unregister_static_drive(const char *id);

/** ชุด callback พร้อมใช้ ส่งให้ vd_menu_page_files_create() ได้เลย */
const vd_file_browser_cbs_t *app_fs_get_browser_cbs(void);

/**
 * แปลง full_path ที่ vd_menu ส่งมา ("/usb0/รูป/a.png")
 * ให้เป็นพาธจริงของระบบไฟล์ ("1:/รูป/a.png")
 * @return true ถ้าแปลงได้ (ไดรฟ์ยังอยู่)
 */
bool app_fs_resolve(const char *full_path, char *out, size_t out_len,
                    app_fs_backend_t *out_backend);

/** แปลง full_path เป็นพาธสำหรับ lv_image_set_src() เช่น "S:1:/รูป/a.png" */
bool app_fs_resolve_lvgl(const char *full_path, char *out, size_t out_len);

/* ---------------- hot-plug ---------------- */

/** ฟังก์ชันที่จะถูกเรียกเมื่อรายการไดรฟ์เปลี่ยน (ถูกเรียกบนเธรด LVGL เสมอ) */
typedef void (*app_fs_refresh_cb_t)(void);

/** ผูกฟังก์ชันรีเฟรช UI */
void app_fs_bridge_set_refresh_cb(app_fs_refresh_cb_t cb);

/**
 * แจ้งว่ารายการไดรฟ์ USB เปลี่ยน — เรียกจาก on_usb() ได้เลย
 * ปลอดภัยต่อเธรด: ข้างในจะ lvgl_port_lock() + lv_async_call() ให้
 */
void app_fs_notify_usb_changed(void);

/** ทิ้งแคชรายการไฟล์ทั้งหมด (เรียกเองได้ถ้ามีการเขียนไฟล์จากนอกโมดูลนี้) */
void app_fs_invalidate_cache(void);

/* ---------------- การกระทำกับไฟล์ (ใช้กับเมนูกดค้าง) ---------------- */

esp_err_t app_fs_delete(const char *full_path, bool is_dir);
esp_err_t app_fs_rename(const char *full_path, const char *new_name);
esp_err_t app_fs_mkdir(const char *drive_id, const char *cur_path, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* APP_FS_BRIDGE_H */
