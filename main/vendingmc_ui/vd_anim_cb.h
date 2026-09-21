#ifndef VD_ANIM_CB_H
#define VD_ANIM_CB_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void _vd_anim_set_translate_x(void *var, int32_t v);
void _vd_anim_set_translate_y(void *var, int32_t v);
void _vd_anim_set_circle_size(void *var, int32_t v);
void _vd_anim_set_x(void *var, int32_t v);
void _vd_anim_set_opa(void *var, int32_t v);
void _vd_anim_set_bg_opa(void *var, int32_t v);
void _vd_anim_set_border_opa(void *var, int32_t v);
void _vd_anim_set_text_opa(void *var, int32_t v);
void _vd_anim_set_height(void *var, int32_t v);
void _vd_anim_set_width(void *var, int32_t v);
void _vd_loading_bar_anim_cb(void * var, int32_t t);
void _vd_anim_draw_check_cb(void *var, int32_t t);

#ifdef __cplusplus
}
#endif

#endif /* VD_ANIM_CB_H */