#include "vd_payment_ui.h"

static void _vd_payment_notify_close(vd_payment_notify_t *notify);

static vd_err_t _vd_payment_notify_delete(vd_payment_notify_t **notify_ptr) {
    if (!notify_ptr || !*notify_ptr) return VD_OK;
    vd_payment_notify_t *notify = *notify_ptr;

    // 1. ยกเลิกแอนิเมชันทั้งหมดที่อาจทำงานค้างอยู่
    if (notify->main)        lv_anim_delete(notify->main, NULL);
    if (notify->top_bar)     lv_anim_delete(notify->top_bar, NULL);
    if (notify->icon_circle) lv_anim_delete(notify->icon_circle, NULL);
    if (notify->check_canvas)lv_anim_delete(notify->check_canvas, NULL);
    if (notify->title_label) lv_anim_delete(notify->title_label, NULL);
    if (notify->sub_label)   lv_anim_delete(notify->sub_label, NULL);
    if (notify->txn_pill)    lv_anim_delete(notify->txn_pill, NULL);
    if (notify->btn_confirm) lv_anim_delete(notify->btn_confirm, NULL);

    // 2. ลบ Widget Tree ของม่าน
    if (notify->main) {
        lv_obj_delete_async(notify->main);
        notify->main = NULL;
    }

    // 3. ปล่อยแรมบัฟเฟอร์ของ Canvas
    if (notify->canvas_buf) {
        heap_caps_free(notify->canvas_buf);
        notify->canvas_buf = NULL;
    }

    free(notify);
    *notify_ptr = NULL; // เคลียร์พอยน์เตอร์ต้นทางตัดปัญหา Dangling Pointer เด็ดขาด

    LV_LOG_USER("vd_payment_notify deleted successfully!");
    return VD_OK;
}

static void _vd_notify_exit_anim_completed_cb(lv_anim_t *a) {
    vd_payment_notify_t *notify = (vd_payment_notify_t *)lv_anim_get_user_data(a);
    if (!notify) return;

    if (notify->close_cb) {
        notify->close_cb(notify->user_data);
    }
    _vd_payment_notify_delete(&notify);
}

/* แอนิเมชันม้วนเก็บความสูงขึ้น (Roll-Up) */
static void _vd_payment_notify_close(vd_payment_notify_t *notify) {
    if (!notify || !notify->main) return;

    // ปิดการรับสัมผัสปุ่มทันที
    if (notify->btn_confirm) lv_obj_set_clickable(notify->btn_confirm, false);

    // 1. ดีดวัตถุลูกลอยขึ้น 20px พร้อม Fade-out
    lv_obj_t *items[] = {notify->icon_circle, notify->title_label, notify->sub_label, 
                         notify->txn_pill, notify->btn_confirm};

    for (int i = 0; i < 5; i++) {
        if (!items[i]) continue;
        lv_anim_t a_pos, a_opa;
        lv_anim_init(&a_pos); lv_anim_init(&a_opa);

        lv_anim_set_var(&a_pos, items[i]);
        lv_anim_set_values(&a_pos, 0, -20);
        lv_anim_set_duration(&a_pos, 220);
        lv_anim_set_path_cb(&a_pos, lv_anim_path_ease_in);
        lv_anim_set_exec_cb(&a_pos, (lv_anim_exec_xcb_t)_vd_anim_set_translate_y);
        lv_anim_start(&a_pos);

        lv_anim_set_var(&a_opa, items[i]);
        lv_anim_set_values(&a_opa, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_duration(&a_opa, 220);
        lv_anim_set_exec_cb(&a_opa, (lv_anim_exec_xcb_t)_vd_anim_set_opa);
        lv_anim_start(&a_opa);
    }

    // 2. ม่านรูดหดความสูงกลับขึ้นไปด้านบน 0 -> -target_height
    lv_anim_t a_h;
    lv_anim_init(&a_h);
    lv_anim_set_var(&a_h, notify->main);
    lv_anim_set_values(&a_h, 0, -notify->target_height);
    lv_anim_set_duration(&a_h, 280);
    lv_anim_set_delay(&a_h, 100);
    lv_anim_set_path_cb(&a_h, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a_h, (lv_anim_exec_xcb_t)_vd_anim_set_translate_y);
    lv_anim_set_user_data(&a_h, notify);
    lv_anim_set_completed_cb(&a_h, _vd_notify_exit_anim_completed_cb);
    lv_anim_start(&a_h);
}

static void _vd_notify_btn_click_cb(lv_event_t *e) {
    vd_payment_notify_t *notify = (vd_payment_notify_t *)lv_event_get_user_data(e);
    if (!notify) return;
    _vd_payment_notify_close(notify);
}

static void _vd_notify_timeout_completed_cb(lv_anim_t *a) {
    vd_payment_notify_t *notify = (vd_payment_notify_t *)lv_anim_get_user_data(a);
    if (!notify) return;
    _vd_payment_notify_close(notify);
}

static vd_payment_notify_t * _vd_payment_notify_create(lv_obj_t *parent, bool is_fullscreen) {
    if (!parent) return NULL;

    vd_payment_notify_t *notify = (vd_payment_notify_t *)calloc(1, sizeof(vd_payment_notify_t));
    if (!notify) {
        LV_LOG_ERROR("Alloc vd_payment_notify_t failed");
        return NULL;
    }

    notify->parent = parent;
    notify->is_fullscreen = is_fullscreen;
    notify->timeout_ms = 5000; // ค่าเริ่มต้น 5 วินาที

    lv_obj_update_layout(parent);
    int32_t p_w = lv_obj_get_width(parent);
    int32_t p_h = lv_obj_get_height(parent);
    if (p_w <= 0) p_w = 1024;
    if (p_h <= 0) p_h = 600;

    notify->target_height = p_h;

    // 1. [Curtain Layer] พื้นหลังม่านสีเขียว
    notify->main = lv_obj_create(parent);
    lv_obj_set_size(notify->main, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(notify->main, 0, 0);
    lv_obj_set_style_translate_y(notify->main, -notify->target_height, 0);
    lv_obj_set_style_bg_color(notify->main, lv_color_hex(0x0ea674), 0);
    lv_obj_set_style_bg_opa(notify->main, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(notify->main, 0, 0);
    lv_obj_set_style_pad_all(notify->main, 0, 0);
    lv_obj_set_style_radius(notify->main, is_fullscreen ? 0 : 24, 0);
    //lv_obj_set_style_clip_corner(notify->main, true, 0);
    lv_obj_set_scrollable(notify->main, false);
    //lv_obj_null_on_delete(&notify->main);

    // 2. [Top Timeout Bar]
    notify->top_bar_track = lv_obj_create(notify->main);
    lv_obj_set_size(notify->top_bar_track, lv_pct(100), 6);
    lv_obj_align(notify->top_bar_track, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(notify->top_bar_track, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(notify->top_bar_track, LV_OPA_20, 0);
    lv_obj_set_style_border_width(notify->top_bar_track, 0, 0);
    lv_obj_set_style_pad_all(notify->top_bar_track, 0, 0);
    lv_obj_set_scrollable(notify->top_bar_track, false);

    notify->top_bar = lv_obj_create(notify->top_bar_track);
    lv_obj_set_size(notify->top_bar, p_w, lv_pct(100));
    lv_obj_align(notify->top_bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(notify->top_bar, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(notify->top_bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(notify->top_bar, 0, 0);
    lv_obj_set_style_pad_all(notify->top_bar, 0, 0);
    lv_obj_set_scrollable(notify->top_bar, false);

    // 3. [Content Box] คอนเทนเนอร์จัดเรียงแนวตั้ง
    notify->content_box = lv_obj_create(notify->main);
    lv_obj_set_size(notify->content_box, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_center(notify->content_box);
    lv_obj_set_style_bg_opa(notify->content_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(notify->content_box, 0, 0);
    lv_obj_set_style_pad_all(notify->content_box, 0, 0);
    lv_obj_set_scrollable(notify->content_box, false);
    lv_obj_set_flex_flow(notify->content_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(notify->content_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(notify->content_box, 12, 0);

    // 4. [Icon Circle + Canvas]
    notify->icon_circle = lv_obj_create(notify->content_box);
    lv_obj_set_size(notify->icon_circle, 0, 0); // ขนาดเริ่มต้น 0 รอแอนิเมชัน
    lv_obj_set_style_radius(notify->icon_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(notify->icon_circle, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(notify->icon_circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(notify->icon_circle, 0, 0);
    lv_obj_set_style_shadow_width(notify->icon_circle, 24, 0);
    lv_obj_set_style_shadow_color(notify->icon_circle, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(notify->icon_circle, LV_OPA_20, 0);
    lv_obj_set_scrollable(notify->icon_circle, false);

    // จัดสรร Buffer 64x64 สำหรับ Canvas
    size_t canvas_buf_size = VD_NOTIFY_CHECK_W * VD_NOTIFY_CHECK_H * 4;
    notify->canvas_buf = (uint8_t *)heap_caps_aligned_alloc(64, canvas_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    notify->check_canvas = lv_canvas_create(notify->icon_circle);
    lv_canvas_set_buffer(notify->check_canvas, notify->canvas_buf, 
                         VD_NOTIFY_CHECK_W, VD_NOTIFY_CHECK_H, LV_COLOR_FORMAT_ARGB8888);
    lv_canvas_fill_bg(notify->check_canvas, lv_color_white(), LV_OPA_TRANSP);
    lv_obj_center(notify->check_canvas);

    // 5. [Labels]
    notify->title_label = lv_label_create(notify->content_box);
    lv_obj_set_style_text_font(notify->title_label, &anuphan_bold_26, 0);
    lv_obj_set_style_text_color(notify->title_label, lv_color_white(), 0);
    lv_label_set_text(notify->title_label, "ชำระเงินสำเร็จ");

    notify->sub_label = lv_label_create(notify->content_box);
    lv_obj_set_style_text_font(notify->sub_label, &anuphan_med_18, 0);
    lv_obj_set_style_text_color(notify->sub_label, lv_color_hex(0xD1FAE5), 0);
    lv_label_set_text(notify->sub_label, "กรุณารับสินค้าที่ช่องรับด้านล่าง");

    // 6. [Transaction Info Pill]
    notify->txn_pill = lv_obj_create(notify->content_box);
    lv_obj_set_size(notify->txn_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(notify->txn_pill, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(notify->txn_pill, LV_OPA_10, 0);
    lv_obj_set_style_border_color(notify->txn_pill, lv_color_white(), 0);
    lv_obj_set_style_border_opa(notify->txn_pill, LV_OPA_30, 0);
    lv_obj_set_style_border_width(notify->txn_pill, 1, 0);
    lv_obj_set_style_radius(notify->txn_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(notify->txn_pill, 18, 0);
    lv_obj_set_style_pad_ver(notify->txn_pill, 6, 0);
    lv_obj_set_scrollable(notify->txn_pill, false);

    notify->txn_label = lv_label_create(notify->txn_pill);
    lv_obj_set_style_text_font(notify->txn_label, &anuphan_16, 0);
    lv_obj_set_style_text_color(notify->txn_label, lv_color_white(), 0);
    lv_label_set_text(notify->txn_label, "รายการเสร็จสมบูรณ์");
    lv_obj_center(notify->txn_label);

    // 7. [Pill Button] ปุ่มตกลง
    notify->btn_confirm = lv_button_create(notify->content_box);
    lv_obj_set_size(notify->btn_confirm, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(notify->btn_confirm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(notify->btn_confirm, lv_color_white(), 0);
    lv_obj_set_style_border_width(notify->btn_confirm, 2, 0);
    lv_obj_set_style_radius(notify->btn_confirm, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(notify->btn_confirm, 45, 0);
    lv_obj_set_style_pad_ver(notify->btn_confirm, 8, 0);
    lv_obj_add_event_cb(notify->btn_confirm, _vd_notify_btn_click_cb, LV_EVENT_CLICKED, notify);

    notify->btn_label = lv_label_create(notify->btn_confirm);
    lv_obj_set_style_text_font(notify->btn_label, &anuphan_bold_18, 0);
    lv_obj_set_style_text_color(notify->btn_label, lv_color_white(), 0);
    lv_label_set_text(notify->btn_label, "ตกลง");
    lv_obj_center(notify->btn_label);

    // ซ่อนคอนเทนต์เริ่มต้นไว้ รอให้ม่านคลุมก่อน
    lv_obj_set_style_opa(notify->content_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_translate_y(notify->content_box, 20, 0);

    return notify;
}

/* ฟังก์ชันเริ่มแอนิเมชันเปิดม่านและดีดวัตถุเข้ามา */
static void _vd_payment_notify_show(vd_payment_notify_t *notify) {
    if (!notify || !notify->main) return;

    // 1. ม่านรูดเปิดลงมา (Roll Down)
    lv_anim_t a_curtain;
    lv_anim_init(&a_curtain);
    lv_anim_set_var(&a_curtain, notify->main);
    lv_anim_set_values(&a_curtain, -notify->target_height, 0);
    lv_anim_set_duration(&a_curtain, 300);
    lv_anim_set_path_cb(&a_curtain, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a_curtain, (lv_anim_exec_xcb_t)_vd_anim_set_translate_y);
    lv_anim_start(&a_curtain);

    // 2. แถบเวลานับถอยหลังหดความกว้าง
    lv_obj_update_layout(notify->top_bar_track);
    int32_t p_w = lv_obj_get_width(notify->top_bar_track);
    if (p_w <= 0) p_w = 1024;

    lv_anim_t a_time;
    lv_anim_init(&a_time);
    lv_anim_set_var(&a_time, notify->top_bar);
    lv_anim_set_values(&a_time, p_w, 0);
    lv_anim_set_duration(&a_time, notify->timeout_ms);
    lv_anim_set_delay(&a_time, 250);
    lv_anim_set_path_cb(&a_time, lv_anim_path_linear);
    lv_anim_set_exec_cb(&a_time, (lv_anim_exec_xcb_t)_vd_anim_set_width);
    lv_anim_set_user_data(&a_time, notify);
    lv_anim_set_completed_cb(&a_time, _vd_notify_timeout_completed_cb);
    lv_anim_start(&a_time);

    // 3. ไอคอนวงกลมเด้งสปริง (Size: 0 -> 80px)
    lv_anim_t a_icon;
    lv_anim_init(&a_icon);
    lv_anim_set_var(&a_icon, notify->icon_circle);
    lv_anim_set_values(&a_icon, 0, 80);
    lv_anim_set_duration(&a_icon, 350);
    lv_anim_set_delay(&a_icon, 200);
    lv_anim_set_path_cb(&a_icon, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&a_icon, (lv_anim_exec_xcb_t)_vd_anim_set_circle_size);
    lv_anim_start(&a_icon);

    // 4. วาดเส้น Checkmark ตามหลัง
    lv_anim_t a_draw;
    lv_anim_init(&a_draw);
    lv_anim_set_var(&a_draw, notify->check_canvas);
    lv_anim_set_values(&a_draw, 0, 100);
    lv_anim_set_duration(&a_draw, 350);
    lv_anim_set_delay(&a_draw, 400);
    lv_anim_set_path_cb(&a_draw, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a_draw, (lv_anim_exec_xcb_t)_vd_anim_draw_check_cb);
    lv_anim_start(&a_draw);

    // 5. ข้อความและปุ่มสไลด์ขึ้นมาทีละชิ้น (Stagger Slide-Up)
    lv_obj_t *items[] = {notify->title_label, notify->sub_label, notify->txn_pill, notify->btn_confirm};
    uint32_t delays[] = {260, 320, 380, 440};

    for (int i = 0; i < 4; i++) {
        // สั่งกล่องแม่ Slide Up และ Fade-in พร้อมกันในคำสั่งเดียว (ลดภาระ CPU ไป 6 แอนิเมชัน)
    lv_anim_t a_box_pos, a_box_opa;
    lv_anim_init(&a_box_pos); lv_anim_init(&a_box_opa);

    lv_anim_set_var(&a_box_pos, notify->content_box);
    lv_anim_set_values(&a_box_pos, 20, 0);
    lv_anim_set_duration(&a_box_pos, 280);
    lv_anim_set_delay(&a_box_pos, 200);
    lv_anim_set_path_cb(&a_box_pos, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a_box_pos, (lv_anim_exec_xcb_t)_vd_anim_set_translate_y);
    lv_anim_start(&a_box_pos);

    lv_anim_set_var(&a_box_opa, notify->content_box);
    lv_anim_set_values(&a_box_opa, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a_box_opa, 280);
    lv_anim_set_delay(&a_box_opa, 200);
    lv_anim_set_exec_cb(&a_box_opa, (lv_anim_exec_xcb_t)_vd_anim_set_opa);
    lv_anim_start(&a_box_opa);
    }
}

static void _vd_payment_notify_set_text(vd_payment_notify_t *notify, const char *title, const char *subtitle) {
    if (!notify) return;
    if (title && notify->title_label) lv_label_set_text(notify->title_label, title);
    if (subtitle && notify->sub_label) lv_label_set_text(notify->sub_label, subtitle);
}

static void _vd_payment_notify_set_transaction_info(vd_payment_notify_t *notify, const char *txn_id, const char *amount_str) {
    if (!notify || !notify->txn_label) return;
    if (txn_id && amount_str) {
        lv_label_set_text_fmt(notify->txn_label, "#%s  |  ชำระแล้ว %s", txn_id, amount_str);
    } else if (amount_str) {
        lv_label_set_text_fmt(notify->txn_label, "ชำระแล้ว %s", amount_str);
    }
}

static void _vd_payment_notify_set_button_text(vd_payment_notify_t *notify, const char *btn_txt) {
    if (!notify || !notify->btn_label || !btn_txt) return;
    lv_label_set_text(notify->btn_label, btn_txt);
}

static void _vd_payment_notify_set_timeout(vd_payment_notify_t *notify, uint32_t timeout_ms) {
    if (!notify) return;
    notify->timeout_ms = timeout_ms;
}

static void _vd_payment_notify_set_close_cb(vd_payment_notify_t *notify, vd_payment_notify_cb_t cb, void *user_data) {
    if (!notify) return;
    notify->close_cb = cb;
    notify->user_data = user_data;
}

static lv_obj_t * _vd_payment_notify_get_main(vd_payment_notify_t *notify) {
    return notify ? notify->main : NULL;
}

static uint32_t _vd_payment_notify_get_timeout(vd_payment_notify_t *notify) {
    return notify ? notify->timeout_ms : 0;
}

static void _vd_payment_opt_close_btn_cb(lv_event_t *e) {
  vd_payment_opt_dialog_t *dialog = (vd_payment_opt_dialog_t*)lv_event_get_user_data(e);
  if(!dialog) return;

  lv_obj_set_hidden       (dialog->main, true);
  lv_obj_set_style_bg_opa (dialog->parent, LV_OPA_TRANSP, 0);
  lv_obj_set_clickable    (dialog->parent, false);

  lv_image_cache_drop(lv_image_get_src(dialog->qr_img));
  lv_image_cache_drop(lv_image_get_src(dialog->promptpay_logo));

  vd_payment_opt_dialog_delete(dialog, false);
  if(dialog->on_delete_cb) dialog->on_delete_cb(dialog);
  free(dialog);
}

void _vd_reset_qr_to_loading_status(vd_payment_opt_dialog_t *dialog){
  lv_obj_set_hidden (dialog->qr_img, true);
  lv_obj_set_hidden (dialog->qr_timeout_txt, true);
  lv_label_set_text (dialog->payment_timer_txt, "--:--");
  lv_obj_align_to   (dialog->payment_timer_txt, dialog->payment_timer_bar, LV_ALIGN_OUT_TOP_RIGHT, 0, 0);
  lv_bar_set_value  (dialog->payment_timer_bar, lv_bar_get_max_value(dialog->payment_timer_bar), LV_ANIM_ON);
}

void _vd_hidden_all_dialog_child(vd_payment_opt_dialog_t *dialog){
  lv_obj_set_hidden (dialog->title, true);
  lv_obj_set_hidden (dialog->total_amount, true);
  lv_obj_set_hidden (dialog->payment_details_table, true);
  lv_obj_set_hidden (dialog->payment_info_bg, true);
  lv_obj_set_hidden (dialog->promptpay_logo, true);
  lv_obj_set_hidden (dialog->qr_bg, true);
  lv_obj_set_hidden (dialog->close_ico, true);
}

static void _vd_payment_label_fadeout_complete_cb(lv_anim_t * a){
  vd_payment_opt_dialog_t *dialog = (vd_payment_opt_dialog_t*)lv_anim_get_user_data(a);

  lv_obj_set_style_text_font  (dialog->title, &anuphan_16, 0);
  lv_obj_set_style_text_color (dialog->title, lv_color_hex(0x5a687d), 0);
  lv_obj_set_style_text_opa   (dialog->title, LV_OPA_100, 0);
  lv_obj_align                (dialog->title, LV_ALIGN_TOP_LEFT, 30, 35);
  lv_label_set_text           (dialog->title, "ยอดที่ต้องชำระสุทธิ");

  lv_obj_set_hidden           (dialog->total_amount, false);
  lv_obj_set_hidden           (dialog->payment_details_table, false);
  lv_obj_set_hidden           (dialog->qr_bg, false);  
  lv_obj_set_hidden           (dialog->payment_info_bg, false);

  _vd_reset_qr_to_loading_status(dialog);

  if(dialog->selected_payment_method == QR) {
    lv_table_set_cell_value   (dialog->payment_details_table, 2, 1, "QR พร้อมเพย์");
    lv_obj_set_hidden         (dialog->qr_loading_bar->track, false);
    lv_obj_set_hidden         (dialog->qr_loading_txt, false);
    lv_obj_set_hidden         (dialog->promptpay_logo, false);
  } else {
    lv_table_set_cell_value   (dialog->payment_details_table, 2, 1, "เงินสด");
  }
}

static void _vd_table_draw_task_cb(lv_event_t *e) {
  lv_draw_task_t *task = lv_event_get_draw_task(e);
  lv_draw_dsc_base_t *base = lv_draw_task_get_draw_dsc(task);

  // ดักเฉพาะส่วนวาดไอเทมในตาราง
  if (base->part == LV_PART_ITEMS) {
    uint32_t col = base->id2; // id2 คือ Index ของ Column

    if (lv_draw_task_get_type(task) == LV_DRAW_TASK_TYPE_LABEL) {
      lv_draw_label_dsc_t *label_dsc = lv_draw_task_get_label_dsc(task);

      if (col == 1) {
        // คอลัมน์ขวา: ชิดขวา + สีเข้ม
        label_dsc->align = LV_TEXT_ALIGN_RIGHT;
        label_dsc->color = lv_color_hex(0x1E293B);
      } else {
        // คอลัมน์ซ้าย: ชิดซ้าย + สีเทา Muted
        label_dsc->align = LV_TEXT_ALIGN_LEFT;
        label_dsc->color = lv_color_hex(0x64748B);
      }
    }
  }
}

static void _vd_qr_time_countdown_cb(lv_timer_t *timer){
  vd_payment_opt_dialog_t *dialog = (vd_payment_opt_dialog_t*)lv_timer_get_user_data(timer);
  if(!dialog) return;

  // 1. ลดเวลาลงทีละ 1,000 ms (1 วินาที) ต่อรอบ
  if(dialog->qr_timeout_ms >= 1000) {
    dialog->qr_timeout_ms -= 1000;
  } else {
    dialog->qr_timeout_ms = 0;
    lv_timer_pause(timer); // หยุดนับเมื่อเวลาหมด

    LV_LOG_WARN("!!!! QR timeout!");

    lv_obj_set_hidden(dialog->qr_img, true);
    lv_obj_set_hidden(dialog->qr_timeout_txt, false);
    lv_label_set_text(dialog->payment_timer_txt, "--:--");
    lv_obj_align_to  (dialog->payment_timer_txt, dialog->payment_timer_bar, LV_ALIGN_OUT_TOP_RIGHT, 0, 0);
    lv_obj_set_hidden(dialog->payment_req_new_qr_btn, false);

    return;
  }

  vd_time_t time = {};
  vd_ms_to_time(dialog->qr_timeout_ms, &time);
  lv_label_set_text_fmt(dialog->payment_timer_txt, "%02u:%02u", time.m, time.s);
  lv_bar_set_value     (dialog->payment_timer_bar, (int32_t)(dialog->qr_timeout_ms / 1000), LV_ANIM_ON);
}

static void _vd_renew_qr_cb(lv_event_t *e){
  vd_payment_opt_dialog_t *dialog = (vd_payment_opt_dialog_t*)lv_event_get_user_data(e);
  if(!dialog) return;

  lv_obj_set_hidden(dialog->payment_req_new_qr_btn, true);
  lv_obj_set_hidden(dialog->qr_timeout_txt, true);
  lv_obj_set_hidden(dialog->qr_loading_txt, false);  
  lv_obj_set_hidden(dialog->qr_loading_bar->track, false);

  lv_image_cache_drop(lv_image_get_src(dialog->qr_img));

  if(dialog->request_new_qr_cb) dialog->request_new_qr_cb(dialog);
  else LV_LOG_USER("request_new_qr_cb is NULL!");
}

vd_payment_opt_dialog_t *vd_payment_opt_dialog_create(lv_obj_t *parent, lv_event_cb_t qr_cb, lv_event_cb_t cash_cb, uint32_t total_items){
  if(!parent) return NULL;

  vd_payment_opt_dialog_t *paym_dialog = (vd_payment_opt_dialog_t*)calloc(1, sizeof(vd_payment_opt_dialog_t));
  if(!paym_dialog) {
    LV_LOG_USER("Create new vd_payment_opt_dialog_t fail.");
    return NULL;
  }

  paym_dialog->parent      = parent;
  paym_dialog->total_items = total_items;

  lv_obj_set_style_bg_color     (paym_dialog->parent, lv_color_black(), 0);
  lv_obj_set_style_bg_opa       (paym_dialog->parent, LV_OPA_50, 0);
  lv_obj_set_clickable          (paym_dialog->parent, true);

  paym_dialog->main = lv_obj_create(parent);

  lv_obj_set_size               (paym_dialog->main, lv_pct(60), lv_pct(60));
  lv_obj_update_layout          (paym_dialog->main);
  lv_obj_center                 (paym_dialog->main);
  lv_obj_set_style_border_opa   (paym_dialog->main, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius       (paym_dialog->main, 20, 0);
  lv_obj_set_style_border_width (paym_dialog->main, 0, 0);
  lv_obj_set_style_radius       (paym_dialog->main, 20, 0);
  lv_obj_set_style_pad_all      (paym_dialog->main, 0, 0);
  lv_obj_set_clickable          (paym_dialog->main, false);
  lv_obj_set_style_clip_corner  (paym_dialog->main, true, 0);
  //lv_obj_null_on_delete         (&paym_dialog->main);

  paym_dialog->title = lv_label_create(paym_dialog->main);

  lv_obj_set_style_text_font    (paym_dialog->title, &anuphan_bold_22, 0);
  lv_label_set_text             (paym_dialog->title, "โปรดเลือกวิธีชำระเงิน");
  lv_obj_align                  (paym_dialog->title, LV_ALIGN_TOP_LEFT, 30, 30);

  paym_dialog->close_ico = lv_label_create(paym_dialog->main);

  lv_label_set_text             (paym_dialog->close_ico, LV_SYMBOL_CLOSE);
  lv_obj_align                  (paym_dialog->close_ico, LV_ALIGN_TOP_RIGHT, -20, 20);
  lv_obj_set_style_text_color   (paym_dialog->close_ico, lv_color_make(180, 180, 180), 0);
  lv_obj_set_style_text_font    (paym_dialog->close_ico, &lv_font_montserrat_22, 0);
  lv_obj_set_clickable          (paym_dialog->close_ico, true);
  lv_obj_set_floating           (paym_dialog->close_ico, true);
  lv_obj_set_ext_click_area     (paym_dialog->close_ico, 15);
  lv_obj_add_event_cb           (paym_dialog->close_ico, _vd_payment_opt_close_btn_cb, LV_EVENT_CLICKED, paym_dialog);

  paym_dialog->qr_pay_card = vd_payment_opt_card_create(paym_dialog->main, &qr120_120, qr_cb, paym_dialog);

  lv_obj_align                  (paym_dialog->qr_pay_card->main, LV_ALIGN_LEFT_MID, 25, 25);
  lv_label_set_text             (paym_dialog->qr_pay_card->title, "QR พร้อมเพย์");
  lv_label_set_text             (paym_dialog->qr_pay_card->subtitle, "สแกนด้วยแอพธนาคาร");

  paym_dialog->cash_pay_card = vd_payment_opt_card_create(paym_dialog->main, &cash167_120, cash_cb, paym_dialog);

  lv_obj_align                  (paym_dialog->cash_pay_card->main, LV_ALIGN_RIGHT_MID, -25, 25);
  lv_label_set_text             (paym_dialog->cash_pay_card->title, "จ่ายเงินสด");
  lv_label_set_text             (paym_dialog->cash_pay_card->subtitle, "ผ่านเครื่องรับธนบัตร และเหรียญ");

  // ======================================== สร้างส่วนของหน้ารอชำระเงิน (loading) ทิ้งไว้และซ่อนบาง obj ไว้ ======================================== //

  paym_dialog->total_amount = lv_label_create(paym_dialog->main);

  lv_obj_set_style_text_font    (paym_dialog->total_amount, &anuphan_bold_40_number, 0);
  lv_obj_align                  (paym_dialog->total_amount, LV_ALIGN_TOP_LEFT, 30, 65);
  lv_obj_set_hidden             (paym_dialog->total_amount, true); // ตอนนี้ยังไม่ได้ใช้ เลยซ่อนไว้ก่อน แต่สร้างไว้ให้พร้อมใช้งาน
  lv_label_set_text             (paym_dialog->total_amount, "฿0");

  paym_dialog->payment_details_table = lv_table_create(paym_dialog->main);

  lv_obj_set_scrollable         (paym_dialog->payment_details_table, false);
  lv_obj_set_clickable          (paym_dialog->payment_details_table, false);
  lv_obj_set_size               (paym_dialog->payment_details_table, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align                  (paym_dialog->payment_details_table, LV_ALIGN_LEFT_MID, 30, 0);
  lv_obj_set_hidden             (paym_dialog->payment_details_table, true); // ตอนนี้ยังไม่ได้ใช้ เลยซ่อนไว้ก่อน แต่สร้างไว้ให้พร้อมใช้งาน
  lv_obj_set_style_bg_color     (paym_dialog->payment_details_table, lv_color_hex(0xF8FAFC), 0);
  lv_obj_set_style_bg_opa       (paym_dialog->payment_details_table, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color (paym_dialog->payment_details_table, lv_color_hex(0xEDF2F7), 0);
  lv_obj_set_style_border_width (paym_dialog->payment_details_table, 1, 0);
  lv_obj_set_style_radius       (paym_dialog->payment_details_table, 12, 0);
  lv_obj_set_style_pad_all      (paym_dialog->payment_details_table, 10, 0);
  lv_obj_add_event_cb           (paym_dialog->payment_details_table, _vd_table_draw_task_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);

  lv_obj_set_style_border_width (paym_dialog->payment_details_table, 0, LV_PART_ITEMS);
  lv_obj_set_style_bg_opa       (paym_dialog->payment_details_table, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_text_font    (paym_dialog->payment_details_table, &anuphan_14, LV_PART_ITEMS);
  lv_obj_set_style_pad_ver      (paym_dialog->payment_details_table, 5, LV_PART_ITEMS); // ระยะห่างแนวตั้งระหว่างแถว
  lv_obj_set_style_pad_hor      (paym_dialog->payment_details_table, 4, LV_PART_ITEMS);

  lv_table_set_column_count     (paym_dialog->payment_details_table, 2);
  lv_table_set_column_width     (paym_dialog->payment_details_table, 0, 140); // คอลัมน์ซ้าย
  lv_table_set_column_width     (paym_dialog->payment_details_table, 1, 90);

  lv_table_set_cell_value       (paym_dialog->payment_details_table, 0, 0, "หมายเลขรายการ");
  lv_table_set_cell_value       (paym_dialog->payment_details_table, 0, 1, "2609200004");

  lv_table_set_cell_value       (paym_dialog->payment_details_table, 1, 0, "รายการทั้งหมด");
  lv_table_set_cell_value_fmt   (paym_dialog->payment_details_table, 1, 1, "%d รายการ", (int)paym_dialog->total_items);

  lv_table_set_cell_value       (paym_dialog->payment_details_table, 2, 0, "ช่องทางชำระเงิน");
  lv_table_set_cell_value       (paym_dialog->payment_details_table, 2, 1, "QR พร้อมเพย์");

  paym_dialog->promptpay_logo = lv_image_create(paym_dialog->main);
  
  lv_image_set_src              (paym_dialog->promptpay_logo, &prompt_pay200_67);
  lv_obj_align                  (paym_dialog->promptpay_logo, LV_ALIGN_TOP_MID, 140, 15);
  lv_obj_set_hidden             (paym_dialog->promptpay_logo, true);

  paym_dialog->qr_bg = lv_obj_create(paym_dialog->main);

  lv_obj_set_size               (paym_dialog->qr_bg, 245, 245);
  lv_obj_set_scrollable         (paym_dialog->qr_bg, false);
  lv_obj_align_to               (paym_dialog->qr_bg, paym_dialog->promptpay_logo, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
  lv_obj_set_hidden             (paym_dialog->qr_bg, true);

  paym_dialog->qr_img = lv_image_create(paym_dialog->qr_bg);

  lv_obj_set_align              (paym_dialog->qr_img, LV_ALIGN_CENTER);
  lv_obj_set_hidden             (paym_dialog->qr_img, true);

  paym_dialog->qr_loading_txt = lv_label_create(paym_dialog->qr_bg);

  lv_obj_align                  (paym_dialog->qr_loading_txt, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_text_font    (paym_dialog->qr_loading_txt, &anuphan_med_18, 0);
  lv_obj_set_style_text_color   (paym_dialog->qr_loading_txt, lv_color_hex(0x3c4553), 0);
  lv_label_set_text             (paym_dialog->qr_loading_txt, "กำลังโหลด...");
  lv_obj_set_hidden             (paym_dialog->qr_loading_txt, true);

  paym_dialog->qr_loading_bar = vd_loading_bar_create(paym_dialog->qr_bg, 180, 10, lv_color_hex(0x5B6EF5), lv_color_hex(0xE2E8F0));

  lv_obj_align                  (paym_dialog->qr_loading_bar->track, LV_ALIGN_CENTER, 0, 10);
  lv_obj_set_hidden             (paym_dialog->qr_loading_bar->track, true);

  paym_dialog->payment_info_bg = lv_obj_create(paym_dialog->main);

  lv_obj_set_size               (paym_dialog->payment_info_bg, 255, 80);
  lv_obj_align                  (paym_dialog->payment_info_bg, LV_ALIGN_LEFT_MID, 30, 110);
  lv_obj_set_hidden             (paym_dialog->payment_info_bg, true);
  lv_obj_set_scrollable         (paym_dialog->payment_info_bg, false);
  lv_obj_set_style_pad_all      (paym_dialog->payment_info_bg, 8, 0);
  lv_obj_set_style_bg_opa       (paym_dialog->payment_info_bg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa   (paym_dialog->payment_info_bg, LV_OPA_TRANSP, 0);

  paym_dialog->payment_timer_bar = lv_bar_create(paym_dialog->payment_info_bg);

  lv_obj_set_size               (paym_dialog->payment_timer_bar, 240, 8);
  lv_obj_set_style_bg_color     (paym_dialog->payment_timer_bar, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
  lv_obj_set_style_bg_opa       (paym_dialog->payment_timer_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius       (paym_dialog->payment_timer_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color     (paym_dialog->payment_timer_bar, lv_color_hex(0x5B6EF5), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa       (paym_dialog->payment_timer_bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius       (paym_dialog->payment_timer_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_anim_duration(paym_dialog->payment_timer_bar, 200, 0);

  lv_bar_set_range              (paym_dialog->payment_timer_bar, 0, 100);
  lv_bar_set_value              (paym_dialog->payment_timer_bar, 100, LV_ANIM_ON);
  lv_obj_align                  (paym_dialog->payment_timer_bar, LV_ALIGN_BOTTOM_MID, 0, -15);

  paym_dialog->payment_info_txt = lv_label_create(paym_dialog->payment_info_bg);

  lv_obj_set_style_text_font    (paym_dialog->payment_info_txt, &anuphan_16, 0);
  lv_label_set_text             (paym_dialog->payment_info_txt, "โปรดชำระก่อน");
  lv_obj_align_to               (paym_dialog->payment_info_txt, paym_dialog->payment_timer_bar, LV_ALIGN_OUT_TOP_LEFT, 0, -5);
  
  paym_dialog->payment_timer_txt = lv_label_create(paym_dialog->payment_info_bg);

  lv_obj_set_style_text_font    (paym_dialog->payment_timer_txt, &anuphan_18, 0);
  lv_label_set_text             (paym_dialog->payment_timer_txt, "--:--");
  lv_obj_align_to               (paym_dialog->payment_timer_txt, paym_dialog->payment_timer_bar, LV_ALIGN_OUT_TOP_RIGHT, 0, 0);

  paym_dialog->qr_timeout_timer = lv_timer_create(_vd_qr_time_countdown_cb, 1000, paym_dialog);
  
  lv_timer_pause                (paym_dialog->qr_timeout_timer);

  paym_dialog->payment_req_new_qr_btn = lv_button_create(paym_dialog->qr_bg);

  lv_obj_center                 (paym_dialog->payment_req_new_qr_btn);
  lv_obj_set_hidden             (paym_dialog->payment_req_new_qr_btn, true);
  lv_obj_set_style_bg_color     (paym_dialog->payment_req_new_qr_btn, lv_color_make(0x5B, 0x6E, 0xF5), 0);
  lv_obj_add_event_cb           (paym_dialog->payment_req_new_qr_btn, _vd_renew_qr_cb, LV_EVENT_CLICKED, paym_dialog);

  paym_dialog->payment_req_new_qr_txt = lv_label_create(paym_dialog->payment_req_new_qr_btn);

  lv_obj_center                 (paym_dialog->payment_req_new_qr_txt);
  lv_obj_set_style_text_font    (paym_dialog->payment_req_new_qr_txt, &anuphan_med_18, 0);
  lv_obj_set_style_text_color   (paym_dialog->payment_req_new_qr_txt, lv_color_white(), 0);
  lv_label_set_text             (paym_dialog->payment_req_new_qr_txt, "สร้าง QR ใหม่");

  paym_dialog->qr_timeout_txt = lv_label_create(paym_dialog->qr_bg);

  lv_obj_set_align              (paym_dialog->qr_timeout_txt, LV_ALIGN_TOP_MID);
  lv_obj_set_style_text_font    (paym_dialog->qr_timeout_txt, &anuphan_16, 0);
  lv_obj_set_style_text_color   (paym_dialog->qr_timeout_txt, lv_color_hex(0xcf1d10), 0);
  lv_label_set_text             (paym_dialog->qr_timeout_txt, "QR หมดอายุ โปรดสร้างใหม่");
  lv_obj_set_hidden             (paym_dialog->qr_timeout_txt, true);

  return paym_dialog;
}

vd_err_t vd_payment_opt_dialog_delete(vd_payment_opt_dialog_t *dialog, bool free_now){
  if(!dialog) return VD_OK;

  lv_obj_delete_async       (dialog->main);

  vd_payment_opt_card_delete(dialog->qr_pay_card);
  vd_payment_opt_card_delete(dialog->cash_pay_card);
  vd_loading_bar_delete     (dialog->qr_loading_bar);

  lv_timer_pause            (dialog->qr_timeout_timer);
  lv_timer_delete           (dialog->qr_timeout_timer);

  if(free_now) free(dialog);

  return VD_OK;
}

vd_err_t vd_payment_opt_scroll_to_loading_page(vd_payment_opt_dialog_t *dialog, lv_event_t *e, vd_payment_type_t selected_method){
  if(!dialog || (selected_method != QR && selected_method != CASH)) return VD_ERR_PARAM_INVALID;

  dialog->selected_payment_method = selected_method;

  lv_obj_t *main_obj = lv_event_get_target_obj(e);

  lv_obj_set_clickable(dialog->qr_pay_card->main, false);
  lv_obj_update_layout(dialog->qr_pay_card->main);

  lv_obj_set_clickable(dialog->cash_pay_card->main, false);
  lv_obj_update_layout(dialog->cash_pay_card->main);

  int32_t shift_distance = 600;

  lv_anim_t border_anim;
  lv_anim_init                  (&border_anim);
  lv_anim_set_var               (&border_anim, main_obj);
  lv_anim_set_values            (&border_anim, 0, 255);
  lv_anim_set_duration          (&border_anim, 300);
  lv_anim_set_path_cb           (&border_anim, lv_anim_path_ease_out);
  lv_anim_set_exec_cb           (&border_anim, _vd_anim_set_border_opa);

  lv_obj_set_style_border_color (main_obj, lv_color_hex(0x5B6EF5), 0);
  lv_obj_set_style_border_width (main_obj, 2, 0);

  lv_anim_t qrp_frame_scroll_anim;
  lv_anim_init                  (&qrp_frame_scroll_anim);
  lv_anim_set_var               (&qrp_frame_scroll_anim, dialog->qr_pay_card->main);
  lv_anim_set_values            (&qrp_frame_scroll_anim, 0, -shift_distance);
  lv_anim_set_duration          (&qrp_frame_scroll_anim, 250);
  lv_anim_set_delay             (&qrp_frame_scroll_anim, 150);
  lv_anim_set_path_cb           (&qrp_frame_scroll_anim, lv_anim_path_ease_in);
  lv_anim_set_exec_cb           (&qrp_frame_scroll_anim, _vd_anim_set_translate_x);

  lv_anim_t cashp_frame_scroll_anim;
  lv_anim_init                  (&cashp_frame_scroll_anim);
  lv_anim_set_var               (&cashp_frame_scroll_anim, dialog->cash_pay_card->main);
  lv_anim_set_values            (&cashp_frame_scroll_anim, 0, -shift_distance);
  lv_anim_set_duration          (&cashp_frame_scroll_anim, 250);
  lv_anim_set_delay             (&cashp_frame_scroll_anim, 150);
  lv_anim_set_path_cb           (&cashp_frame_scroll_anim, lv_anim_path_ease_in);
  lv_anim_set_exec_cb           (&cashp_frame_scroll_anim, _vd_anim_set_translate_x);

  lv_anim_t paym_label_fadeout_anim;
  lv_anim_init                  (&paym_label_fadeout_anim);
  lv_anim_set_var               (&paym_label_fadeout_anim, dialog->title);
  lv_anim_set_values            (&paym_label_fadeout_anim, 255, 0);
  lv_anim_set_duration          (&paym_label_fadeout_anim, 200);
  lv_anim_set_delay             (&paym_label_fadeout_anim, 200);
  lv_anim_set_user_data         (&paym_label_fadeout_anim, dialog);
  lv_anim_set_path_cb           (&paym_label_fadeout_anim, lv_anim_path_ease_out);
  lv_anim_set_exec_cb           (&paym_label_fadeout_anim, _vd_anim_set_text_opa);
  lv_anim_set_completed_cb      (&paym_label_fadeout_anim, _vd_payment_label_fadeout_complete_cb);

  lv_anim_start                 (&border_anim);
  lv_anim_start                 (&qrp_frame_scroll_anim);
  lv_anim_start                 (&cashp_frame_scroll_anim);
  lv_anim_start                 (&paym_label_fadeout_anim);

  return VD_OK;
}

vd_err_t vd_payment_opt_set_total_amounts(vd_payment_opt_dialog_t *dialog, float amounts){
  if(!dialog) return VD_ERR_PARAM_INVALID;

  char str[32];
  vd_format_price_raw(amounts, str, true);
  lv_label_set_text_fmt(dialog->total_amount, "฿%s", str);

  return VD_OK;
}

vd_err_t vd_payment_opt_set_qr_data(vd_payment_opt_dialog_t *dialog, void *img_src, uint32_t timeout){
  if(!dialog || !timeout) return VD_ERR_PARAM_INVALID;

  lv_image_set_src     (dialog->qr_img, img_src);
  lv_image_set_scale   (dialog->qr_img, 190);

  dialog->qr_timeout_ms = timeout;

  vd_time_t time = {};
  vd_ms_to_time        (timeout, &time);
  lv_label_set_text_fmt(dialog->payment_timer_txt, "%02u:%02u", time.m, time.s);
  lv_obj_align_to      (dialog->payment_timer_txt, dialog->payment_timer_bar, LV_ALIGN_OUT_TOP_RIGHT, 0, 0);

  uint32_t sec = timeout / 1000;
  lv_bar_set_range     (dialog->payment_timer_bar, 0, sec);
  lv_bar_set_value     (dialog->payment_timer_bar, sec, LV_ANIM_ON);

  return VD_OK;
}

vd_err_t vd_payment_opt_send_loading_ok_signal(vd_payment_opt_dialog_t *dialog){
  if(!dialog) return VD_ERR_PARAM_INVALID;

  lv_obj_set_hidden(dialog->qr_loading_txt, true);  
  lv_obj_set_hidden(dialog->qr_loading_bar->track, true);
  lv_obj_set_hidden(dialog->qr_img, false);

  lv_timer_resume  (dialog->qr_timeout_timer);

  return VD_OK;
}

vd_err_t vd_payment_opt_set_request_new_qr_cb(vd_payment_opt_dialog_t *dialog, vd_request_new_qr_cb_t cb){
  if(!dialog || !cb) return VD_ERR_PARAM_INVALID;

  dialog->request_new_qr_cb = cb;

  return VD_OK;
}

vd_err_t vd_payment_opt_set_on_delete_cb(vd_payment_opt_dialog_t *dialog, vd_payment_dialog_delete_cb_t cb){
  if(!dialog || !cb) return VD_ERR_PARAM_INVALID;

  dialog->on_delete_cb = cb;

  return VD_OK;
}

static void on_notify_closed_cb(void *user_data) {
  vd_payment_opt_dialog_t *dialog = (vd_payment_opt_dialog_t*)user_data;
  lv_obj_send_event(dialog->close_ico, LV_EVENT_CLICKED, dialog);
}

vd_err_t vd_payment_opt_send_payment_ok_signal(vd_payment_opt_dialog_t *dialog){
  if(!dialog) return VD_ERR_PARAM_INVALID;

  _vd_hidden_all_dialog_child(dialog);

  dialog->payment_notify = _vd_payment_notify_create(dialog->main, true);

  _vd_payment_notify_set_text             (dialog->payment_notify, "ชำระเงินสำเร็จ", "กรุณารับสินค้าที่ช่องรับด้านล่าง");
  _vd_payment_notify_set_transaction_info (dialog->payment_notify, "TXN-20260920-04", lv_label_get_text(dialog->total_amount));
  _vd_payment_notify_set_timeout          (dialog->payment_notify, 5000); // แสดงผล 5 วินาที
  _vd_payment_notify_set_close_cb         (dialog->payment_notify, on_notify_closed_cb, dialog);
  _vd_payment_notify_show                 (dialog->payment_notify);

  return VD_OK;
}

vd_payment_opt_card_t *vd_payment_opt_card_create(lv_obj_t *parent, const void* img_src, lv_event_cb_t cb, void *user_data){
  if(!parent) return NULL;

  vd_payment_opt_card_t *paym_opt_card = (vd_payment_opt_card_t*)calloc(1, sizeof(vd_payment_opt_card_t));
  if(!paym_opt_card){
    LV_LOG_USER("Create new vd_payment_opt_card_t fail.");
    return NULL;
  }

  paym_opt_card->main = lv_obj_create(parent);

  lv_obj_set_size             (paym_opt_card->main, lv_pct(45), lv_pct(65));
  lv_obj_set_style_bg_color   (paym_opt_card->main, lv_color_hex(0xF4F6FF), 0); // พื้นฟ้าพาสเทลสะอาด
  lv_obj_set_style_bg_opa     (paym_opt_card->main, LV_OPA_COVER, 0);
  lv_obj_set_style_border_opa (paym_opt_card->main, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius     (paym_opt_card->main, 16, 0);
  lv_obj_set_style_pad_all    (paym_opt_card->main, 10, 0);
  lv_obj_set_scrollable       (paym_opt_card->main, false);
  lv_obj_set_clickable        (paym_opt_card->main, true);
  lv_obj_set_overflow_visible (paym_opt_card->main, true);
  lv_obj_set_ext_draw_size    (paym_opt_card->main, 20);
  lv_obj_add_event_cb         (paym_opt_card->main, cb, LV_EVENT_CLICKED, user_data);
  //lv_obj_null_on_delete       (&paym_opt_card->main);

  paym_opt_card->badge = vd_badge_create(paym_opt_card->main, "---", &anuphan_14, lv_color_hex(0x0A7A57), lv_color_white());
  
  lv_obj_align                (paym_opt_card->badge->main, LV_ALIGN_TOP_LEFT, 8, -23);
  lv_obj_set_floating         (paym_opt_card->badge->main, true);

  paym_opt_card->img = lv_image_create(paym_opt_card->main);

  lv_image_set_src            (paym_opt_card->img, img_src);
  lv_image_set_scale          (paym_opt_card->img, 150);
  lv_obj_align                (paym_opt_card->img, LV_ALIGN_CENTER, 0, -40);

  paym_opt_card->title = lv_label_create(paym_opt_card->main);

  lv_obj_set_style_text_font  (paym_opt_card->title, &anuphan_bold_20, 0);
  lv_obj_set_style_text_color (paym_opt_card->title, lv_color_hex(0x1E293B), 0);
  lv_label_set_text           (paym_opt_card->title, "---");
  lv_obj_align                (paym_opt_card->title, LV_ALIGN_CENTER, 0, 20);

  paym_opt_card->subtitle = lv_label_create(paym_opt_card->main);

  lv_obj_set_style_text_font  (paym_opt_card->subtitle, &anuphan_16, 0);
  lv_obj_set_style_text_color (paym_opt_card->subtitle, lv_color_hex(0x64748B), 0);
  lv_label_set_text           (paym_opt_card->subtitle, "---");
  lv_obj_align                (paym_opt_card->subtitle, LV_ALIGN_CENTER, 0, 45);  

  return paym_opt_card;
}

vd_err_t vd_payment_opt_card_delete(vd_payment_opt_card_t *card) {
  if(!card) return VD_OK;

  lv_image_cache_drop(lv_image_get_src(card->img));

  lv_obj_delete_async(card->main);
  vd_badge_delete    (card->badge);

  free(card);
  card = NULL;

  return VD_OK;
}

vd_badge_t *vd_badge_create(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t bg_color, lv_color_t text_color) {
  if(!parent || !text) return NULL;

  vd_badge_t *badge = (vd_badge_t*)calloc(1, sizeof(vd_badge_t));
  if(!badge){
    LV_LOG_USER("Create new vd_badge_t fail.");
    return NULL;
  }
  
  badge->main = lv_obj_create(parent);

  lv_obj_set_size             (badge->main, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color   (badge->main, bg_color, 0);
  lv_obj_set_style_bg_opa     (badge->main, LV_OPA_COVER, 0);
  lv_obj_set_style_border_opa (badge->main, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius     (badge->main, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor    (badge->main, 8, 0);
  lv_obj_set_style_pad_ver    (badge->main, 3, 0);
  lv_obj_set_scrollable       (badge->main, false);
  //lv_obj_null_on_delete       (&badge->main);

  badge->label = lv_label_create(badge->main);

  if (font) 
    lv_obj_set_style_text_font(badge->label, font, 0);
  lv_obj_set_style_text_color (badge->label, text_color, 0);
  lv_label_set_text           (badge->label, text);
  lv_obj_set_align            (badge->label, LV_ALIGN_CENTER);

  return badge;
}

vd_err_t vd_badge_delete(vd_badge_t *badge) {
  if(!badge) return VD_OK;

  lv_obj_delete_async(badge->main);

  free(badge);
  badge = NULL;

  return VD_OK;
}

vd_err_t vd_payment_opt_card_set_badge(vd_badge_t *badge, const char *txt, bool en){
  if(!badge) return VD_ERR_PARAM_INVALID;

  lv_obj_set_hidden(badge->main, !en);
  lv_label_set_text(badge->label, txt);

  return VD_OK;
}

vd_loading_bar_t *vd_loading_bar_create(lv_obj_t *parent, int32_t w, int32_t h, lv_color_t bar_color, lv_color_t bg_color) {
  if(!parent || !w || !h) return NULL;
  
  vd_loading_bar_t *loading_bar = (vd_loading_bar_t*)calloc(1, sizeof(vd_loading_bar_t));
  if(!loading_bar){
    LV_LOG_USER("Create new vd_loading_bar_t fail.");
    return NULL;
  }

  loading_bar->track = lv_obj_create(parent);

  lv_obj_set_size             (loading_bar->track, w, h);
  lv_obj_set_style_bg_color   (loading_bar->track, bg_color, 0);
  lv_obj_set_style_bg_opa     (loading_bar->track, LV_OPA_COVER, 0);
  lv_obj_set_style_radius     (loading_bar->track, LV_RADIUS_CIRCLE, 0); // ทำขอบมนทรง Pill
  lv_obj_set_style_clip_corner(loading_bar->track, true, 0);        // บังคับตัดขอบมนแท่งแสงด้านใน
  lv_obj_set_style_border_opa (loading_bar->track, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all    (loading_bar->track, 0, 0);
  lv_obj_set_scrollable       (loading_bar->track, false);
  //lv_obj_null_on_delete       (&loading_bar->track);

  loading_bar->indicator = lv_obj_create(loading_bar->track);
    
  lv_obj_set_size             (loading_bar->indicator, 0, lv_pct(100)); // สูงเต็ม 100% ของ track
  lv_obj_set_style_bg_color   (loading_bar->indicator, bar_color, 0);
  lv_obj_set_style_bg_opa     (loading_bar->indicator, LV_OPA_COVER, 0);
  lv_obj_set_style_radius     (loading_bar->indicator, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_opa (loading_bar->indicator, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all    (loading_bar->indicator, 0, 0);
  lv_obj_set_scrollable       (loading_bar->indicator, false);

  lv_anim_t a;
  lv_anim_init                (&a);
  lv_anim_set_var             (&a, loading_bar->indicator);
  lv_anim_set_values          (&a, 0, 1200); // ส่งค่าเวลา t (0 ถึง 1200ms)
  lv_anim_set_duration        (&a, 1200);
  lv_anim_set_repeat_count    (&a, LV_ANIM_REPEAT_INFINITE); // เล่นวนไม่รู้จบ
  lv_anim_set_path_cb         (&a, lv_anim_path_linear);          // Linear time (เพราะเราคำนวณ Easing เองใน cb)
  lv_anim_set_exec_cb         (&a, (lv_anim_exec_xcb_t)_vd_loading_bar_anim_cb);
  lv_anim_start               (&a);

  return loading_bar;
}

vd_err_t vd_loading_bar_delete(vd_loading_bar_t *bar){
  if(!bar) return VD_OK;

  lv_obj_delete_async(bar->track);

  free(bar);
  bar = NULL;

  return VD_OK;
}