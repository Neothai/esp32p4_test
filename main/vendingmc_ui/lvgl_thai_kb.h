#ifndef LVGL_THAI_KB_H
#define LVGL_THAI_KB_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KB_INPUT_ALL,       /* ภาษาไทย + อังกฤษ + สัญลักษณ์ (ค้นหาสินค้า, ข้อความทั่วไป) */
    KB_INPUT_NUMBER,    /* ตัวเลขจำนวนเต็มเท่านั้น 0-9 (สต็อกสินค้า, รหัส PIN) */
    KB_INPUT_DECIMAL,   /* ตัวเลข + ทศนิยม (ราคาสินค้า, น้ำหนัก) */
} kb_input_type_t;

/* สร้างคีย์บอร์ดพร้อมระบบสลับภาษา ไทย-อังกฤษ */
lv_obj_t * lvgl_thai_kb_create(lv_obj_t * parent, const lv_font_t * font);

/* ผูก Textarea เข้ากับคีย์บอร์ดอัตโนมัติ พร้อมกำหนดประเภท Input */
void lvgl_kb_bind_textarea(lv_obj_t * kb, lv_obj_t * ta, kb_input_type_t input_type);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_THAI_KB_H */