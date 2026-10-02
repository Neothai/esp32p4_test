#ifndef VD_PAYMENT_UI_H
#define VD_PAYMENT_UI_H

#include <stdio.h>
#include <esp_heap_caps.h>
#include "lvgl.h"
#include "vd_home_page.h"
#include "vd_anim_cb.h"
#include "vd_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

// ขนาด Canvas วาดเครื่องหมายถูก
#define VD_NOTIFY_CHECK_W 64
#define VD_NOTIFY_CHECK_H 64

typedef void (*vd_payment_notify_cb_t)(void *user_data);

typedef struct vd_badge_t vd_badge_t;
typedef struct vd_loading_bar_t vd_loading_bar_t;
typedef struct vd_payment_opt_dialog_t vd_payment_opt_dialog_t;
typedef struct vd_payment_notify_t vd_payment_notify_t;

typedef void (*vd_request_new_qr_cb_t)(vd_payment_opt_dialog_t *dialog);
typedef void (*vd_payment_dialog_delete_cb_t)(vd_payment_opt_dialog_t *dialog);

typedef enum vd_payment_type_t {
  QR,
  CASH
} vd_payment_type_t;

typedef struct vd_payment_opt_card_t {
  lv_obj_t *main;
  vd_badge_t *badge;
  lv_obj_t *img;
  lv_obj_t *title;
  lv_obj_t *subtitle;
} vd_payment_opt_card_t;

typedef struct vd_payment_opt_dialog_t {
  lv_obj_t *parent;
  lv_obj_t *main;
  lv_obj_t *title;
  lv_obj_t *close_ico;

  vd_payment_dialog_delete_cb_t on_delete_cb;

  // สำหรับหน้าแรก
  vd_payment_opt_card_t *qr_pay_card;
  vd_payment_opt_card_t *cash_pay_card;

  // สำหรับหน้าโหลด QR หรือ เงินสด
  lv_obj_t *total_amount;
  lv_obj_t *payment_details_table;
  lv_obj_t *promptpay_logo;
  lv_obj_t *qr_bg;
  lv_obj_t *qr_img;
  lv_obj_t *qr_loading_txt;
  vd_loading_bar_t *qr_loading_bar;

  // ส่วนข้อมูลเสริมของการจ่ายเงิน หากเป็นตอนที่ qr กำลังโหลด payment_info_txt จะเป็น "รองรับการชำระทุกธนาคาร" แต่หากโหลดเสร็จแล้วจะเป็น "โปรดชำระภายใน ... วินาที"
  lv_obj_t *payment_info_bg;
  lv_obj_t *payment_info_txt;
  lv_obj_t *payment_timer_bar;
  lv_obj_t *payment_timer_txt;
  lv_obj_t *payment_req_new_qr_btn;
  lv_obj_t *payment_req_new_qr_txt;

  vd_payment_type_t selected_payment_method;

  uint32_t qr_timeout_ms;
  lv_timer_t *qr_timeout_timer;
  lv_obj_t *qr_timeout_txt;

  vd_request_new_qr_cb_t request_new_qr_cb;
  uint32_t total_items;

  vd_payment_notify_t *payment_notify;
} vd_payment_opt_dialog_t;

typedef struct vd_badge_t {
  lv_obj_t *main;
  lv_obj_t *label;
} vd_badge_t;

typedef struct vd_loading_bar_t {
  lv_obj_t *track;
  lv_obj_t *indicator;
} vd_loading_bar_t;

typedef struct vd_payment_notify_t {
    lv_obj_t *parent;
    lv_obj_t *main;               // ม่านบังตาสีเขียว (Roll-down / Roll-up)
    lv_obj_t *top_bar_track;      // รางวิ่งเวลานับถอยหลัง
    lv_obj_t *top_bar;            // แถบเวลานับถอยหลัง (หดความกว้างลง)
    lv_obj_t *content_box;        // กล่องรวมวัตถุกึ่งกลาง
    lv_obj_t *icon_circle;        // วงกลมสีขาว (Overshoot Bounce)
    lv_obj_t *check_canvas;       // Canvas วาดเส้นถูก
    uint8_t  *canvas_buf;         // บัฟเฟอร์ ARGB8888 ของ Canvas
    lv_obj_t *title_label;        // หัวข้อ "ชำระเงินสำเร็จ"
    lv_obj_t *sub_label;          // ข้อความรอง "กรุณารับสินค้าที่ช่องรับด้านล่าง"
    lv_obj_t *txn_pill;           // กล่องแคปซูลสรุปยอด
    lv_obj_t *txn_label;          // ข้อความในแคปซูล
    lv_obj_t *btn_confirm;        // ปุ่มกด "ตกลง"
    lv_obj_t *btn_label;          // ข้อความบนปุ่ม

    uint32_t timeout_ms;          // ระยะเวลาแสดงผลก่อนปิดอัตโนมัติ
    int32_t  target_height;       // ความสูงเป้าหมายของม่าน
    bool     is_fullscreen;       // โหมดเต็มจอ หรือ ในกรอบ Modal
    vd_payment_notify_cb_t close_cb; // Callback เมื่อปิดม่านสมบูรณ์
    void    *user_data;
} vd_payment_notify_t;

vd_payment_opt_dialog_t *vd_payment_opt_dialog_create(lv_obj_t *parent, lv_event_cb_t qr_cb, lv_event_cb_t cash_cb, uint32_t total_items);
vd_err_t                 vd_payment_opt_dialog_delete(vd_payment_opt_dialog_t *dialog, bool free_now);

vd_err_t                 vd_payment_opt_scroll_to_loading_page(vd_payment_opt_dialog_t *dialog, lv_event_t *e, vd_payment_type_t selected_method);
vd_err_t                 vd_payment_opt_set_total_amounts(vd_payment_opt_dialog_t *dialog, float amounts);
vd_err_t                 vd_payment_opt_set_qr_data(vd_payment_opt_dialog_t *dialog, void *img_src, uint32_t timeout);
vd_err_t                 vd_payment_opt_send_loading_ok_signal(vd_payment_opt_dialog_t *dialog);
vd_err_t                 vd_payment_opt_set_request_new_qr_cb(vd_payment_opt_dialog_t *dialog, vd_request_new_qr_cb_t cb);
vd_err_t                 vd_payment_opt_set_on_delete_cb(vd_payment_opt_dialog_t *dialog, vd_payment_dialog_delete_cb_t cb);
vd_err_t                 vd_payment_opt_send_payment_ok_signal(vd_payment_opt_dialog_t *dialog);

vd_payment_opt_card_t   *vd_payment_opt_card_create(lv_obj_t *parent, const void *img_src, lv_event_cb_t cb, void *user_data);
vd_err_t                 vd_payment_opt_card_delete(vd_payment_opt_card_t *card);
vd_err_t                 vd_payment_opt_card_set_badge(vd_badge_t *badge, const char *txt, bool en);

vd_badge_t              *vd_badge_create(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t bg_color, lv_color_t text_color);
vd_err_t                 vd_badge_delete(vd_badge_t *badge);

vd_loading_bar_t        *vd_loading_bar_create(lv_obj_t * parent, int32_t w, int32_t h, lv_color_t bar_color, lv_color_t bg_color);
vd_err_t                 vd_loading_bar_delete(vd_loading_bar_t *bar);

#ifdef __cplusplus
}
#endif

#endif /* VD_PAYMENT_UI_H */