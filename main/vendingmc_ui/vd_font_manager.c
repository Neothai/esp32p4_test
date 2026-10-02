#include "vd_font_manager.h"

static _vd_org_tiny_ttf_dsc_cb _vd_org_font_cb = NULL;

/* ประกาศตัวแปร Global Font */
lv_font_t *vd_font_reg_12 = NULL;
lv_font_t *vd_font_reg_14 = NULL;
lv_font_t *vd_font_reg_16 = NULL;
lv_font_t *vd_font_reg_18 = NULL;
lv_font_t *vd_font_reg_20 = NULL;
lv_font_t *vd_font_reg_22 = NULL;
lv_font_t *vd_font_reg_24 = NULL;
lv_font_t *vd_font_reg_26 = NULL;
lv_font_t *vd_font_reg_28 = NULL;

lv_font_t *vd_font_med_12 = NULL;
lv_font_t *vd_font_med_14 = NULL;
lv_font_t *vd_font_med_16 = NULL;
lv_font_t *vd_font_med_18 = NULL;
lv_font_t *vd_font_med_20 = NULL;
lv_font_t *vd_font_med_22 = NULL;
lv_font_t *vd_font_med_24 = NULL;
lv_font_t *vd_font_med_26 = NULL;
lv_font_t *vd_font_med_28 = NULL;

lv_font_t *vd_font_semi_12 = NULL;
lv_font_t *vd_font_semi_14 = NULL;
lv_font_t *vd_font_semi_16 = NULL;
lv_font_t *vd_font_semi_18 = NULL;
lv_font_t *vd_font_semi_20 = NULL;
lv_font_t *vd_font_semi_22 = NULL;
lv_font_t *vd_font_semi_24 = NULL;
lv_font_t *vd_font_semi_26 = NULL;
lv_font_t *vd_font_semi_28 = NULL;

lv_font_t *vd_font_bold_12 = NULL;
lv_font_t *vd_font_bold_14 = NULL;
lv_font_t *vd_font_bold_16 = NULL;
lv_font_t *vd_font_bold_18 = NULL;
lv_font_t *vd_font_bold_20 = NULL;
lv_font_t *vd_font_bold_22 = NULL;
lv_font_t *vd_font_bold_24 = NULL;
lv_font_t *vd_font_bold_26 = NULL;
lv_font_t *vd_font_bold_28 = NULL;

/* Thai Unicode Constants */
#define THAI_MAI_HAN_AKAT     0x0E31
#define THAI_SARA_I           0x0E34
#define THAI_SARA_II          0x0E35
#define THAI_SARA_UE          0x0E36
#define THAI_SARA_UEE         0x0E37
#define THAI_MAI_TAIKHU       0x0E47
#define THAI_NIKHAHIT         0x0E4D
#define THAI_YAMAKKAN         0x0E4E

#define THAI_MAI_EK           0x0E48
#define THAI_MAI_THO          0x0E49
#define THAI_MAI_TRI          0x0E4A
#define THAI_MAI_CHATTAWA     0x0E4B
#define THAI_THANTHAKHAT      0x0E4C

#define THAI_SARA_U           0x0E38
#define THAI_SARA_UU          0x0E39
#define THAI_PHINTHU          0x0E3A

#define THAI_CHAR_LLVL        0x00
#define THAI_CHAR_LVL0        0x01
#define THAI_CHAR_LVL1        0x02
#define THAI_CHAR_LVL1_OR_2   0x03
#define THAI_CHAR_NONE        0x04

uint8_t _vd_get_thai_char_level(uint32_t unicode) {
    if (!(unicode >= 0x0E00 && unicode <= 0x0E7F))                 return THAI_CHAR_NONE;
    if (unicode >= THAI_SARA_U && unicode <= THAI_PHINTHU)         return THAI_CHAR_LLVL;
    if ((unicode >= THAI_MAI_HAN_AKAT && unicode <= THAI_SARA_UEE)
        || unicode == THAI_MAI_TAIKHU || unicode == THAI_NIKHAHIT
        || unicode == THAI_YAMAKKAN   || unicode == 0x0E46)        return THAI_CHAR_LVL1;
    if (unicode >= THAI_MAI_EK && unicode <= THAI_THANTHAKHAT)     return THAI_CHAR_LVL1_OR_2;

    return THAI_CHAR_LVL0;
}

static bool _vd_ttf_glyph_dsc_hook(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
                                   uint32_t unicode_letter, uint32_t unicode_letter_next) {
    if (!_vd_org_font_cb) return false;
    bool res = _vd_org_font_cb(font, dsc_out, unicode_letter, unicode_letter_next);
    if (!res) return false;

    uint8_t ctype = _vd_get_thai_char_level(unicode_letter);
    if (ctype == THAI_CHAR_NONE) return true;

    uint8_t next_ctype = _vd_get_thai_char_level(unicode_letter_next);

    if (ctype == THAI_CHAR_LVL1_OR_2) {
        dsc_out->adv_w = 0;
        dsc_out->ofs_y += (next_ctype != THAI_CHAR_LVL1_OR_2) ? dsc_out->box_h : 0;
    }

    return true;
}

lv_font_t *vd_ttf_create(const void *data, size_t size, int32_t font_size) {
    lv_font_t *font = lv_tiny_ttf_create_data(data, size, font_size);
    if (!font) return NULL;

    if (font->get_glyph_dsc != _vd_ttf_glyph_dsc_hook) {
        _vd_org_font_cb = font->get_glyph_dsc;
        font->get_glyph_dsc = _vd_ttf_glyph_dsc_hook;
    }

    return font;
}

void vd_ttf_set_size(lv_font_t *font, int32_t font_size) {
    if (font) {
        lv_tiny_ttf_set_size(font, font_size);
    }
}

void vd_font_manager_init(const void *ttf_reg_data, const void *ttf_med_data, const void *ttf_semi_data, const void *ttf_bold_data,
                          size_t ttf_reg_size, size_t ttf_med_size, size_t ttf_semi_size, size_t ttf_bold_size){

  // สร้าง Instance แต่ละขนาดจาก Buffer TTF เดียวกัน
  vd_font_reg_12 = vd_ttf_create(ttf_reg_data, ttf_reg_size, 12);
  vd_font_reg_14 = vd_ttf_create(ttf_reg_data, ttf_reg_size, 14);
  vd_font_reg_16 = vd_ttf_create(ttf_reg_data, ttf_reg_size, 16);
  vd_font_reg_18 = vd_ttf_create(ttf_reg_data, ttf_reg_size, 18);
  vd_font_reg_20 = vd_ttf_create(ttf_reg_data, ttf_reg_size, 20);
  vd_font_reg_22 = vd_ttf_create(ttf_reg_data, ttf_reg_size, 22);
  vd_font_reg_24 = vd_ttf_create(ttf_reg_data, ttf_reg_size, 24);
  vd_font_reg_26 = vd_ttf_create(ttf_reg_data, ttf_reg_size, 26);
  vd_font_reg_28 = vd_ttf_create(ttf_reg_data, ttf_reg_size, 28);

  vd_font_med_12 = vd_ttf_create(ttf_med_data, ttf_med_size, 12);
  vd_font_med_14 = vd_ttf_create(ttf_med_data, ttf_med_size, 14);
  vd_font_med_16 = vd_ttf_create(ttf_med_data, ttf_med_size, 16);
  vd_font_med_18 = vd_ttf_create(ttf_med_data, ttf_med_size, 18);
  vd_font_med_20 = vd_ttf_create(ttf_med_data, ttf_med_size, 20);
  vd_font_med_22 = vd_ttf_create(ttf_med_data, ttf_med_size, 22);
  vd_font_med_24 = vd_ttf_create(ttf_med_data, ttf_med_size, 24);
  vd_font_med_26 = vd_ttf_create(ttf_med_data, ttf_med_size, 26);
  vd_font_med_28 = vd_ttf_create(ttf_med_data, ttf_med_size, 28);

  vd_font_semi_12 = vd_ttf_create(ttf_semi_data, ttf_semi_size, 12);
  vd_font_semi_14 = vd_ttf_create(ttf_semi_data, ttf_semi_size, 14);
  vd_font_semi_16 = vd_ttf_create(ttf_semi_data, ttf_semi_size, 16);
  vd_font_semi_18 = vd_ttf_create(ttf_semi_data, ttf_semi_size, 18);
  vd_font_semi_20 = vd_ttf_create(ttf_semi_data, ttf_semi_size, 20);
  vd_font_semi_22 = vd_ttf_create(ttf_semi_data, ttf_semi_size, 22);
  vd_font_semi_24 = vd_ttf_create(ttf_semi_data, ttf_semi_size, 24);
  vd_font_semi_26 = vd_ttf_create(ttf_semi_data, ttf_semi_size, 26);
  vd_font_semi_28 = vd_ttf_create(ttf_semi_data, ttf_semi_size, 28);

  vd_font_bold_12 = vd_ttf_create(ttf_bold_data, ttf_bold_size, 12);
  vd_font_bold_14 = vd_ttf_create(ttf_bold_data, ttf_bold_size, 14);
  vd_font_bold_16 = vd_ttf_create(ttf_bold_data, ttf_bold_size, 16);
  vd_font_bold_18 = vd_ttf_create(ttf_bold_data, ttf_bold_size, 18);
  vd_font_bold_20 = vd_ttf_create(ttf_bold_data, ttf_bold_size, 20);
  vd_font_bold_22 = vd_ttf_create(ttf_bold_data, ttf_bold_size, 22);
  vd_font_bold_24 = vd_ttf_create(ttf_bold_data, ttf_bold_size, 24);
  vd_font_bold_26 = vd_ttf_create(ttf_bold_data, ttf_bold_size, 26);
  vd_font_bold_28 = vd_ttf_create(ttf_bold_data, ttf_bold_size, 28);
}
