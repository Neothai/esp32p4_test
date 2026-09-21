#include "vd_anim_cb.h"

void _vd_anim_set_translate_x(void *var, int32_t v) {
  lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0);
}

void _vd_anim_set_translate_y(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

void _vd_anim_set_circle_size(void *var, int32_t v) {
  lv_obj_t *obj = (lv_obj_t *)var;
  lv_obj_set_size(obj, v, v);
}

void _vd_anim_set_x(void *var, int32_t v) {
  lv_obj_set_x((lv_obj_t *)var, v);
}

void _vd_anim_set_opa(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

void _vd_anim_set_bg_opa(void *var, int32_t v) {
  lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

void _vd_anim_set_border_opa(void *var, int32_t v) {
  lv_obj_set_style_border_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

void _vd_anim_set_text_opa(void *var, int32_t v) {
  lv_obj_set_style_text_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

void _vd_anim_set_height(void *var, int32_t v) {
  lv_obj_set_height((lv_obj_t *)var, v);
}

void _vd_anim_set_width(void *var, int32_t v) {
  lv_obj_set_width((lv_obj_t *)var, v);
}

void _vd_loading_bar_anim_cb(void *var, int32_t t) {
  lv_obj_t *indicator = (lv_obj_t *)var;
  lv_obj_t *track = lv_obj_get_parent(indicator);
  int32_t w = lv_obj_get_width(track);

  // คำนวณ Cubic Easing (0 - 1200ms)
  int64_t t3 = (int64_t)t * t * t;
  int64_t inv_t = 1200 - t;
  int64_t inv_t3 = inv_t * inv_t * inv_t;

  // คำนวณตำแหน่งหัว (head) และหาง (tail)
  int32_t tail_x = (int32_t)((t3 * w) / 1728000000LL);
  int32_t head_x = (int32_t)(((1728000000LL - inv_t3) * w) / 1728000000LL);

  int32_t bar_w = head_x - tail_x;
  if (bar_w < 0)  bar_w = 0;

  // อัปเดตพิกัดและความกว้างให้แท่งแสงใน LVGL
  lv_obj_set_x(indicator, tail_x);
  lv_obj_set_width(indicator, bar_w);
}

/* แอนิเมชันวาดเส้น Checkmark เชื่อมมุมเนียนสนิทบน Canvas */
void _vd_anim_draw_check_cb(void *var, int32_t t) {
    lv_obj_t *canvas = (lv_obj_t *)var;
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_TRANSP);

    const int32_t ax = 16, ay = 32; // จุดเริ่มต้น A
    const int32_t bx = 28, by = 44; // จุดหักมุม B
    const int32_t cx = 48, cy = 18; // จุดปลาย C
    const int32_t line_w = 6;

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    if (t <= 35) {
        int32_t cur_x = ax + ((bx - ax) * t) / 35;
        int32_t cur_y = ay + ((by - ay) * t) / 35;

        lv_draw_line_dsc_t line1;
        lv_draw_line_dsc_init(&line1);
        line1.color = lv_color_hex(0x10B981);
        line1.width = line_w;
        line1.round_start = true;
        line1.round_end = true;
        line1.p1 = (lv_point_precise_t){ax, ay};
        line1.p2 = (lv_point_precise_t){cur_x, cur_y};
        lv_draw_line(&layer, &line1);
    } else {
        lv_draw_line_dsc_t line1;
        lv_draw_line_dsc_init(&line1);
        line1.color = lv_color_hex(0x10B981);
        line1.width = line_w;
        line1.round_start = true;
        line1.round_end = true;
        line1.p1 = (lv_point_precise_t){ax, ay};
        line1.p2 = (lv_point_precise_t){bx, by};
        lv_draw_line(&layer, &line1);

        int32_t t2 = t - 35;
        int32_t cur_x = bx + ((cx - bx) * t2) / 65;
        int32_t cur_y = by + ((cy - by) * t2) / 65;

        lv_draw_line_dsc_t line2;
        lv_draw_line_dsc_init(&line2);
        line2.color = lv_color_hex(0x10B981);
        line2.width = line_w;
        line2.round_start = false;
        line2.round_end = true;
        line2.p1 = (lv_point_precise_t){bx, by};
        line2.p2 = (lv_point_precise_t){cur_x, cur_y};
        lv_draw_line(&layer, &line2);
    }

    lv_canvas_finish_layer(canvas, &layer);
}