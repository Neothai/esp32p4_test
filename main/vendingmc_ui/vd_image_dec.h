#ifndef VD_IMAGE_DEC_H
#define VD_IMAGE_DEC_H

#include "lvgl.h"
#include "vd_home_page.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

#ifdef SOC_JPEG_DECODE_SUPPORTED
  #include "driver/jpeg_decode.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ประเภทฟอร์แมตรูปภาพ
 */
typedef enum {
    VD_IMG_TYPE_UNKNOWN = 0,
    VD_IMG_TYPE_JPG,
    VD_IMG_TYPE_PNG,
    VD_IMG_TYPE_WEBP,
    VD_IMG_TYPE_BMP,
    VD_IMG_TYPE_GIF,
    VD_IMG_TYPE_TIFF,
    VD_IMG_TYPE_ICO,
    VD_IMG_TYPE_SVG,
} vd_img_type_t;

/**
 * @brief ประเภทย่อยเฉพาะไฟล์ JPG / JPEG
 */
typedef enum {
    VD_JPG_MODE_NONE = 0,        // ไม่ใช่ไฟล์ JPG
    VD_JPG_MODE_BASELINE,        // Baseline DCT (SOF0: 0xC0) - ESP32-P4 HW Decode รองรับ
    VD_JPG_MODE_PROGRESSIVE,     // Progressive DCT (SOF2: 0xC2 / SOF10: 0xCA) - HW Decode ไม่รองรับ
    VD_JPG_MODE_EXTENDED_SEQ,    // Extended Sequential DCT (SOF1: 0xC1)
    VD_JPG_MODE_OTHER,           // Lossless, Hierarchical หรือโหมดอื่นๆ
} vd_jpg_mode_t;

/**
 * @brief ข้อมูลสรุปของรูปภาพที่ตรวจสอบได้
 */
typedef struct {
    vd_img_type_t type;          // ชนิดไฟล์
    vd_jpg_mode_t jpg_mode;      // โหมดเฉพาะของ JPG (ถ้าเป็นรูปอื่นจะเป็น VD_JPG_MODE_NONE)
    uint32_t      width;         // ความกว้าง (พิกเซล)
    uint32_t      height;        // ความสูง (พิกเซล)
    const char   *type_name;     // สตริงชื่อประเภท (เช่น "JPG (Baseline)", "PNG")
} vd_img_info_t;

/**
 * @brief ตรวจสอบชนิดไฟล์รูปภาพจาก Buffer ไบนารี
 * @param data พอยน์เตอร์ชี้ไปยังบัฟเฟอร์ข้อมูลรูปภาพ
 * @param len ขนาดของบัฟเฟอร์ (ไบต์)
 * @param out_info โครงสร้างสำหรับรับผลการวิเคราะห์ (ส่ง NULL ได้ถ้าต้องการแค่ Enum คืนค่า)
 * @return vd_img_type_t ชนิดของรูปภาพ
 */
vd_img_type_t vd_img_check_type(const uint8_t *data, size_t len, vd_img_info_t *out_info);

esp_err_t vd_img_jpg_dec(const uint8_t *jpg_in, size_t len, uint8_t **out_raw, size_t *out_size, uint32_t *out_w, uint32_t *out_h);

esp_err_t vd_img_jpg_to_lv_dsc(const uint8_t *raw_in, size_t size, vd_img_info_t *info, lv_image_dsc_t *img_dsc);

#ifdef __cplusplus
}
#endif

#endif /* VD_IMAGE_DEC_H */