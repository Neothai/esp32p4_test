#ifndef VD_AVI_PLAYER_H
#define VD_AVI_PLAYER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"
#include "esp32_avidec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vd_avi_player_t vd_avi_player_t;

/**
 * @brief สร้าง Widget ตัวเล่นวิดีโอ AVI (ทำงานบนพื้นฐาน lv_image)
 * @param parent อ็อบเจกต์แม่ใน LVGL
 * @return lv_obj_t* อ็อบเจกต์ Widget สำหรับนำไปจัดตำแหน่ง/ปรับขนาด
 */
lv_obj_t *vd_avi_create(lv_obj_t *parent);

/**
 * @brief โหลดไฟล์วิดีโอ AVI จาก SD Card หรือ Flash Storage
 * @param obj อ็อบเจกต์ที่สร้างจาก vd_avi_create()
 * @param path พาทไฟล์ เช่น "0:/videos/clip.avi"
 */
esp_err_t vd_avi_set_src(lv_obj_t *obj, const char *path);

/**
 * @brief เริ่มเล่น หรือเล่นวิดีโอต่อ
 */
esp_err_t vd_avi_play(lv_obj_t *obj);

/**
 * @brief หยุดเล่นวิดีโอชั่วคราว
 */
esp_err_t vd_avi_pause(lv_obj_t *obj);

/**
 * @brief หยุดเล่นและเลื่อนตำแหน่งกลับไปจุดเริ่มต้น
 */
esp_err_t vd_avi_stop(lv_obj_t *obj);

/**
 * @brief ค้นหาตำแหน่งเวลาของวิดีโอ (วินาที)
 */
esp_err_t vd_avi_seek(lv_obj_t *obj, uint64_t sec);

/**
 * @brief กรอกลับไปจุดเริ่มต้น (0 วินาที)
 */
esp_err_t vd_avi_seek_start(lv_obj_t *obj);

/**
 * @brief ลบตัวเล่นวิดีโอ คืนแรม และทำลาย Task เบื้องหลังทั้งหมด
 */
esp_err_t vd_avi_delete(lv_obj_t *obj);

/**
 * @brief ผูกฟังก์ชันรับข้อมูลเสียง (MP3/PCM) เพื่อส่งต่อไปยัง Audio Codec (เช่น ES8311)
 */
esp_err_t vd_avi_set_audio_cb(lv_obj_t *obj, esp32_avidec_audio_cb_t cb, void *user_ctx);

/**
 * @brief ดึงข้อมูลโครงสร้างควบคุมเบื้องหลัง (กรณีต้องการปรับแต่งขั้นสูง)
 */
vd_avi_player_t *vd_avi_get_player(lv_obj_t *obj);

/**
 * @brief อ่านเวลาปัจจุบันของวิดีโอ (หน่วย: วินาที)
 */
uint32_t vd_avi_get_current_sec(lv_obj_t *obj);

/**
 * @brief อ่านความยาวทั้งหมดของวิดีโอ (หน่วย: วินาที)
 */
uint32_t vd_avi_get_total_sec(lv_obj_t *obj);

/**
 * @brief ตรวจสอบว่าวิดีโอกำลังเล่นอยู่หรือไม่
 */
bool vd_avi_is_playing(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* VD_AVI_PLAYER_H */