#ifndef VD_FONT_MANAGER_H
#define VD_FONT_MANAGER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_font_t *vd_font_reg_12;
extern lv_font_t *vd_font_reg_14;
extern lv_font_t *vd_font_reg_16;
extern lv_font_t *vd_font_reg_18;
extern lv_font_t *vd_font_reg_20;
extern lv_font_t *vd_font_reg_22;
extern lv_font_t *vd_font_reg_24;
extern lv_font_t *vd_font_reg_26;
extern lv_font_t *vd_font_reg_28;

extern lv_font_t *vd_font_med_12;
extern lv_font_t *vd_font_med_14;
extern lv_font_t *vd_font_med_16;
extern lv_font_t *vd_font_med_18;
extern lv_font_t *vd_font_med_20;
extern lv_font_t *vd_font_med_22;
extern lv_font_t *vd_font_med_24;
extern lv_font_t *vd_font_med_26;
extern lv_font_t *vd_font_med_28;

extern lv_font_t *vd_font_semi_12;
extern lv_font_t *vd_font_semi_14;
extern lv_font_t *vd_font_semi_16;
extern lv_font_t *vd_font_semi_18;
extern lv_font_t *vd_font_semi_20;
extern lv_font_t *vd_font_semi_22;
extern lv_font_t *vd_font_semi_24;
extern lv_font_t *vd_font_semi_26;
extern lv_font_t *vd_font_semi_28;

extern lv_font_t *vd_font_bold_12;
extern lv_font_t *vd_font_bold_14;
extern lv_font_t *vd_font_bold_16;
extern lv_font_t *vd_font_bold_18;
extern lv_font_t *vd_font_bold_20;
extern lv_font_t *vd_font_bold_22;
extern lv_font_t *vd_font_bold_24;
extern lv_font_t *vd_font_bold_26;
extern lv_font_t *vd_font_bold_28;

#define anuphan_12 (*vd_font_reg_12)
#define anuphan_14 (*vd_font_reg_14)
#define anuphan_16 (*vd_font_reg_16)
#define anuphan_18 (*vd_font_reg_18)
#define anuphan_20 (*vd_font_reg_20)
#define anuphan_22 (*vd_font_reg_22)

#define anuphan_med_14 (*vd_font_med_14)
#define anuphan_med_18 (*vd_font_med_18)
#define anuphan_med_22 (*vd_font_med_22)

#define anuphan_semi_bold_16 (*vd_font_semi_16)
#define anuphan_semi_bold_18 (*vd_font_semi_18)

#define anuphan_bold_16 (*vd_font_bold_16)
#define anuphan_bold_18 (*vd_font_bold_18)
#define anuphan_bold_20 (*vd_font_bold_20)
#define anuphan_bold_22 (*vd_font_bold_22)
#define anuphan_bold_24 (*vd_font_bold_24)
#define anuphan_bold_26 (*vd_font_bold_26)

typedef bool (*_vd_org_tiny_ttf_dsc_cb)(const lv_font_t *, lv_font_glyph_dsc_t *, uint32_t, uint32_t);

void vd_font_manager_init(const void *ttf_reg_data, const void *ttf_med_data, const void *ttf_semi_data, const void *ttf_bold_data,
                          size_t ttf_reg_size, size_t ttf_med_size, size_t ttf_semi_size, size_t ttf_bold_size);

/**
 * @brief สร้างฟอนต์ TinyTTF อิสระพร้อมผูก Hook สระไทย
 */
lv_font_t *vd_ttf_create(const void *data, size_t size, int32_t font_size);

/**
 * @brief เปลี่ยนขนาดฟอนต์ TinyTTF
 */
void vd_ttf_set_size(lv_font_t *font, int32_t font_size);

#ifdef __cplusplus
}
#endif

#endif // VD_FONT_MANAGER_H
