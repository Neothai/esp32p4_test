#ifndef VD_TOAST_NOTIFY_H
#define VD_TOAST_NOTIFY_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vd_toast_notify_t {
    lv_obj_t *main;
    lv_obj_t *label;
    uint32_t timeout_ms;
} vd_toast_notify_t;

/**
 * @brief แสดง Toast ข้อความแจ้งเตือนสไตล์ Android (ลอยอยู่ด้านล่างกึ่งกลางจอ)
 * @param parent วิดเจ็ตหลัก (หากส่ง NULL ระบบจะนำไปไว้บน lv_layer_top() ลอยเหนือทุกหน้าต่างอัตโนมัติ)
 * @param txt ข้อความที่ต้องการแจ้งเตือน
 * @param timeout ระยะเวลาที่ต้องการให้แสดง (ms) ก่อนจะ Fade หายไปเอง (หากใส่ 0 จะตั้งค่าเริ่มต้นเป็น 2000ms)
 * @return vd_toast_notify_t* พอยน์เตอร์โครงสร้าง Toast
 */
vd_toast_notify_t *vd_toast_notify_create(lv_obj_t *parent, const char *txt, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* VD_TOAST_NOTIFY_H */