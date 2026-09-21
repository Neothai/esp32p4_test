#include "vd_toast_notify.h"
#include <stdlib.h>

LV_FONT_DECLARE(anuphan_16);

static void _anim_set_opa(void * var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void _anim_set_translate_y(void * var, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

/* คืนหน่วยความจำ struct เมื่อ Widget ถูกทำลาย */
static void _toast_delete_event_cb(lv_event_t * e) {
    vd_toast_notify_t * toast = (vd_toast_notify_t *)lv_event_get_user_data(e);
    if(toast) {
        lv_anim_delete(toast->main, NULL);
        free(toast);
        toast = NULL;
    }
}

/* เมื่อแอนิเมชันขาออก (Fade Out) เล่นจบ ให้สั่งลบ Widget */
static void _toast_fade_out_completed_cb(lv_anim_t * a) {
    lv_obj_t * main = (lv_obj_t *)a->var;
    if(main) {
        lv_obj_delete_async(main);
    }
}

/* =========================================================================
 * ฟังก์ชันใหม่: เมื่อแอนิเมชันขาเข้า (เด้ง) เล่นจบ -> ค่อยเริ่มจับเวลาขาออก
 * ========================================================================= */
static void _toast_in_completed_cb(lv_anim_t * a) {
    vd_toast_notify_t * toast = (vd_toast_notify_t *)lv_anim_get_user_data(a);
    if(!toast || !toast->main) return;

    // 1. แอนิเมชันขาออก: ค่อยๆ จางหายไป (Fade Out)
    lv_anim_t a_opa_out;
    lv_anim_init(&a_opa_out);
    lv_anim_set_var(&a_opa_out, toast->main);
    lv_anim_set_values(&a_opa_out, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a_opa_out, 120);
    lv_anim_set_delay(&a_opa_out, toast->timeout_ms); // ค้างไว้ตามเวลาที่กำหนดก่อนเริ่มจาง
    lv_anim_set_exec_cb(&a_opa_out, _anim_set_opa);
    lv_anim_set_path_cb(&a_opa_out, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a_opa_out, _toast_fade_out_completed_cb);
    lv_anim_start(&a_opa_out);

    // 2. แอนิเมชันขาออก: ทิ้งตัวจมลงด้านล่าง
    lv_anim_t a_y_out;
    lv_anim_init(&a_y_out);
    lv_anim_set_var(&a_y_out, toast->main);
    lv_anim_set_values(&a_y_out, 0, 40);
    lv_anim_set_duration(&a_y_out, 120);
    lv_anim_set_delay(&a_y_out, toast->timeout_ms);
    lv_anim_set_exec_cb(&a_y_out, _anim_set_translate_y);
    lv_anim_set_path_cb(&a_y_out, lv_anim_path_ease_in);
    lv_anim_start(&a_y_out);
}

vd_toast_notify_t *vd_toast_notify_create(lv_obj_t *parent, const char *txt, uint32_t timeout) {
    if(!txt) return NULL;

    if(!parent) {
        parent = lv_layer_top();
    }

    vd_toast_notify_t *toast = (vd_toast_notify_t *)calloc(1, sizeof(vd_toast_notify_t));
    if(!toast) return NULL;

    toast->timeout_ms = (timeout == 0) ? 2000 : timeout;

    // 1. กล่องแคปซูลพื้นหลัง
    toast->main = lv_obj_create(parent);
    lv_obj_set_size(toast->main, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(toast->main, 700, 0);
    lv_obj_set_style_bg_color(toast->main, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_bg_opa(toast->main, LV_OPA_90, 0);
    lv_obj_set_style_radius(toast->main, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(toast->main, 0, 0);
    lv_obj_set_style_pad_hor(toast->main, 24, 0);
    lv_obj_set_style_pad_ver(toast->main, 10, 0);
    lv_obj_set_style_shadow_width(toast->main, 20, 0);
    lv_obj_set_style_shadow_opa(toast->main, (lv_opa_t)LV_OPA_20, 0);
    lv_obj_set_style_shadow_offset_y(toast->main, 4, 0);
    lv_obj_set_scrollable(toast->main, false);
    lv_obj_set_clickable(toast->main, false);
    lv_obj_set_floating(toast->main, true);

    lv_obj_align(toast->main, LV_ALIGN_BOTTOM_MID, 0, -60);

    // 2. ข้อความแจ้งเตือน
    toast->label = lv_label_create(toast->main);
    lv_obj_set_style_text_font(toast->label, &anuphan_16, 0);
    lv_obj_set_style_text_color(toast->label, lv_color_white(), 0);
    lv_obj_set_style_text_align(toast->label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(toast->label, txt);
    lv_obj_align(toast->label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(toast->main, _toast_delete_event_cb, LV_EVENT_DELETE, toast);

    // 3. ซ่อนตัวและดันพิกัดเริ่มต้นลงไปข้างล่าง 50px
    lv_obj_set_style_opa(toast->main, LV_OPA_TRANSP, 0);
    lv_obj_set_style_translate_y(toast->main, 50, 0); 

    // --- แอนิเมชันขาเข้า: Fade In เร็วๆ ใน 70ms ---
    lv_anim_t a_opa_in;
    lv_anim_init(&a_opa_in);
    lv_anim_set_var(&a_opa_in, toast->main);
    lv_anim_set_values(&a_opa_in, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a_opa_in, 70);
    lv_anim_set_exec_cb(&a_opa_in, _anim_set_opa);
    lv_anim_set_path_cb(&a_opa_in, lv_anim_path_ease_out);
    lv_anim_start(&a_opa_in);

    // --- แอนิเมชันขาเข้า: พุ่งขึ้นมา + สปริง Overshoot ใน 200ms ---
    lv_anim_t a_y_in;
    lv_anim_init(&a_y_in);
    lv_anim_set_var(&a_y_in, toast->main);
    lv_anim_set_values(&a_y_in, 70, 0);
    lv_anim_set_duration(&a_y_in, 200);
    lv_anim_set_exec_cb(&a_y_in, _anim_set_translate_y);
    lv_anim_set_path_cb(&a_y_in, lv_anim_path_overshoot);
    
    // ผูก Callback: เมื่อเด้งขึ้นมาเข้าที่เรียบร้อยแล้ว ให้เริ่มนับเวลาเพื่อถอยออก
    lv_anim_set_user_data(&a_y_in, toast);
    lv_anim_set_completed_cb(&a_y_in, _toast_in_completed_cb);
    lv_anim_start(&a_y_in);

    return toast;
}