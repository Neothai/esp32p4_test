#ifndef VD_SDCARD_H
#define VD_SDCARD_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ff.h"
#include "diskio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define _VD_SD_MMC_CLK  GPIO_NUM_43
#define _VD_SD_MMC_CMD  GPIO_NUM_44
#define _VD_SD_MMC_D0   GPIO_NUM_39
#define _VD_SD_MMC_D1   GPIO_NUM_40
#define _VD_SD_MMC_D2   GPIO_NUM_41
#define _VD_SD_MMC_D3   GPIO_NUM_42

#define _VD_SD_PROFILE_TRY_COUNT 3
#define _VD_SD_MOUNT_POINT  "0:"

typedef struct _vd_sd_profile_t {
  const char *name;
  uint32_t freq_khz;
  float io_voltage;
  uint32_t slot_flags;
  uint8_t bus_width;
} _vd_sd_profile_t;

/* Callback เมื่อพบไฟล์ที่ตรงกับนามสกุลที่กรอง */
typedef bool (*vd_sd_file_cb_t)(const char *file_path, const char *file_name, size_t file_size, void *user_data);

/* --- ฟังก์ชัน Mount / Unmount แบบบรรทัดเดียว --- */

/**
 * @brief Init ฮาร์ดแวร์ SDMMC และ Mount SD Card ด้วย Pin ค่าเริ่มต้นของ ESP32-P4
 * @param mount_point จุด Mount เช่น "/sdcard" หรือ "0:"
 * @return ESP_OK หากสำเร็จ
 */
esp_err_t vd_sd_mount(void);

/**
 * @brief Unmount และ Deinit ฮาร์ดแวร์ SD Card แบบบรรทัดเดียว
 */
esp_err_t vd_sd_unmount(void);

/* --- ฟังก์ชันจัดการไฟล์ --- */

/**
 * @brief อ่านไฟล์ทั้งก้อนเข้า Buffer อัตโนมัติ (คืนค่า Buffer ที่ทำ Aligned 64 ไบต์และ Null-terminated)
 * @param path พาทของไฟล์ เช่น "0:/image.jpg"
 * @param out_buf พอยน์เตอร์รับที่อยู่ Buffer (ต้อง free หรือ heap_caps_free หลังใช้งาน)
 * @param out_size พอยน์เตอร์รับขนาดไฟล์จริง (ไบต์)
 * @param prefer_psram true หากต้องการจองแรมใน PSRAM, false สำหรับ Internal RAM
 */
esp_err_t vd_sd_read_file(const char *path, uint8_t **out_buf, size_t *out_size, bool prefer_psram);

/**
 * @brief เขียน Buffer ลงไฟล์ในคำสั่งเดียว (สร้างใหม่หรือเขียนทับเสมอ)
 */
esp_err_t vd_sd_write_file(const char *path, const void *data, size_t size);

/**
 * @brief เขียนข้อมูลต่อท้ายไฟล์เดิม (Append)
 */
esp_err_t vd_sd_append_file(const char *path, const void *data, size_t size);

/**
 * @brief ตรวจสอบว่ามีไฟล์อยู่จริงหรือไม่
 */
bool vd_sd_file_exists(const char *path);

/**
 * @brief ลบไฟล์
 */
esp_err_t vd_sd_delete(const char *path);

/**
 * @brief เปลี่ยนชื่อไฟล์ หรือย้ายตำแหน่งไฟล์ (Move) ภายใน Drive
 */
esp_err_t vd_sd_move(const char *src_path, const char *dst_path);

/**
 * @brief คัดลอกไฟล์ (Copy) แบบสตรีมบัฟเฟอร์
 */
esp_err_t vd_sd_copy(const char *src_path, const char *dst_path);

/**
 * @brief สแกนค้นหาไฟล์ในโฟลเดอร์ พร้อมกรองนามสกุล
 * @param dir_path โฟลเดอร์ที่ต้องการค้น เช่น "0:/images"
 * @param ext_filter นามสกุลที่ต้องการ เช่น ".jpg", ".png" (ส่ง NULL หรือ "" หากต้องการทุกไฟล์)
 * @param cb Callback ที่จะถูกเรียกทุกครั้งที่เจอไฟล์
 * @param user_data ข้อมูลส่งผ่านไปยัง Callback
 */
esp_err_t vd_sd_scan_dir(const char *dir_path, const char *ext_filter, vd_sd_file_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* VD_SDCARD_H */