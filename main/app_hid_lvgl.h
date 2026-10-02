/*
 * app_hid_lvgl.h — ต่อเมาส์ / คีย์บอร์ด USB เข้ากับ LVGL 9
 *
 * ใช้คู่กับ components/esp32_usb (CherryUSB) + esp_lvgl_port
 *
 * ────────────────────────────────────────────────────────────────
 *  วิธีใช้ย่อ ๆ
 * ────────────────────────────────────────────────────────────────
 *   // 1) หลัง lvgl_port_init() และได้ lv_display_t * มาแล้ว
 *   lvgl_port_lock(0);
 *   app_hid_lvgl_init(disp);
 *   lvgl_port_unlock();
 *
 *   // 2) ใน event callback ของ USB (รันบน event task ของไลบรารี)
 *   static void on_usb(const esp32_usbh_event_t *ev, void *ctx) {
 *       app_hid_lvgl_feed(ev);          // ← บรรทัดเดียวจบ
 *       ...
 *   }
 *
 *   // 3) widget ที่อยากให้คีย์บอร์ดคุมได้ ให้ใส่เข้า group
 *   lv_group_add_obj(app_hid_lvgl_group(), my_textarea);
 *
 * ⚠️ app_hid_lvgl_feed() ถูกเรียกจาก "event task" ไม่ใช่เธรด LVGL
 *    ข้างในจึงแตะได้แค่ตัวแปรที่ป้องกันด้วย spinlock เท่านั้น
 *    ห้ามเรียก lv_* ใด ๆ เด็ดขาด
 */
#ifndef APP_HID_LVGL_H
#define APP_HID_LVGL_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"
#include "esp32_usb.h"

#ifdef __cplusplus
extern "C" {
#endif

/** สร้าง indev ของเมาส์ (pointer) + คีย์บอร์ด (keypad) + เคอร์เซอร์
 *  @note ต้องเรียกบนเธรด LVGL (ครอบด้วย lvgl_port_lock) */
esp_err_t app_hid_lvgl_init(lv_display_t *disp);

/** ป้อน event จากไลบรารี USB — เรียกจาก event callback ได้เลยทุก event
 *  ตัวที่ไม่เกี่ยวกับ HID จะถูกเมินเอง */
void app_hid_lvgl_feed(const esp32_usbh_event_t *ev);

/** group ที่ indev คีย์บอร์ดผูกอยู่ — เอา widget ใส่เองได้ */
lv_group_t *app_hid_lvgl_group(void);

/** object ที่จะให้ล้อเมาส์เลื่อน (เช่น file_list_box)
 *  NULL = ใช้หน้าจอปัจจุบัน */
void app_hid_lvgl_set_scroll_target(lv_obj_t *obj);

/** ซ่อน/โชว์เคอร์เซอร์เอง (ปกติไลบรารีจัดการให้ตอนเสียบ/ถอดอยู่แล้ว) */
void app_hid_lvgl_show_cursor(bool show);

/** ความไว 1..10 (ค่าเริ่มต้น 4) */
void app_hid_lvgl_set_sensitivity(uint8_t level);

/** มีเมาส์ / คีย์บอร์ดเสียบอยู่ไหม */
bool app_hid_lvgl_mouse_present(void);
bool app_hid_lvgl_keyboard_present(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_HID_LVGL_H */
