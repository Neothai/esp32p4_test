#ifndef VD_HOME_PAGE_H
#define VD_HOME_PAGE_H

#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include <math.h>
#include "lvgl.h"
#include "lvgl_thai_kb.h"
#include "vd_anim_cb.h"
#include "vd_utils.h"
#include "misc/cache/instance/lv_image_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(anuphan_14);
LV_FONT_DECLARE(anuphan_16);
extern lv_font_t anuphan_18;
LV_FONT_DECLARE(anuphan_med_18);
LV_FONT_DECLARE(anuphan_bold_20);
LV_FONT_DECLARE(anuphan_22);
LV_FONT_DECLARE(anuphan_med_22);
LV_FONT_DECLARE(anuphan_bold_22);
LV_FONT_DECLARE(anuphan_bold_18);
LV_FONT_DECLARE(anuphan_bold_26);
LV_FONT_DECLARE(anuphan_bold_40_number);

LV_IMG_DECLARE(nvdm200_51);
LV_IMG_DECLARE(cart250_219);
LV_IMG_DECLARE(qr120_120);
LV_IMG_DECLARE(cash167_120);
LV_IMG_DECLARE(prompt_pay200_67);

/* =========================================================================
 * 1. Forward Declaration: บอกคอมไพเลอร์ล่วงหน้าว่ามี struct ชื่อนี้
 * ========================================================================= */
typedef struct vd_prod_card_t vd_prod_card_t;
typedef struct vd_home_page_t vd_home_page_t;

/* =========================================================================
 * 2. ประกาศ Function Pointer: ตอนนี้สามารถอ้างอิง vd_prod_card_t * ได้แล้ว
 * ========================================================================= */
typedef void (*vd_prod_card_clicked_cb_t)(vd_prod_card_t *card, lv_event_t *e);

typedef enum vd_err_t {
  VD_OK,
  VD_ERR_NO_MEM,
  VD_ERR_PARAM_INVALID,
} vd_err_t;

typedef struct vd_spinbox_t {
  lv_obj_t *main;
  lv_style_t btn_style;
  lv_obj_t *value_txt;
  lv_obj_t *down_btn;
  lv_obj_t *down_btn_sign;
  lv_obj_t *up_btn;
  lv_obj_t *up_btn_sign;
} vd_spinbox_t;

typedef struct vd_prod_card_t {
  vd_home_page_t *hp;
  lv_obj_t *main;
  lv_obj_t *prod_name;
  lv_obj_t *prod_full_price; // full price
  lv_obj_t *prod_disc_price; // discount price
  lv_obj_t *prod_img;

  float full_price;
  float disc_price;

  bool have_discount;
  vd_prod_card_clicked_cb_t clicked_cb;
} vd_prod_card_t;

typedef struct vd_cart_item_t {
  vd_home_page_t *hp;
  lv_obj_t *main_bg;
  lv_obj_t *bg_delete;
  lv_obj_t *trash_icn;
  
  lv_obj_t *main;
  lv_obj_t *prod_img_bg;
  lv_obj_t *prod_img;
  lv_obj_t *prod_name;
  lv_obj_t *prod_price;
  vd_spinbox_t *prod_spdbox;

  float price;

  uint32_t max_pcs;
  uint32_t min_pcs;
} vd_cart_item_t;

typedef struct {
    vd_cart_item_t **items;  /* อาเรย์ของพอยน์เตอร์ */
    size_t count;            /* จำนวนสินค้าที่มีอยู่ในตะกร้าปัจจุบัน */
    size_t capacity;         /* ความจุของหน่วยความจำที่จองไว้ */
} vd_cart_list_t;

typedef struct vd_home_page_t {
  lv_obj_t *main;
  lv_obj_t *keyboard;
  lv_obj_t *prod_flex;
  lv_obj_t *app_logo;
  lv_obj_t *prod_search_ta;

  lv_obj_t *cart;
  lv_obj_t *cart_title_txt;
  lv_obj_t *cart_prod_list;
  lv_obj_t *cart_empty_ico;
  lv_obj_t *cart_empty_txt;
  lv_obj_t *cart_checkout;
  lv_obj_t *cart_checkout_total_txt;
  lv_obj_t *cart_checkout_amount;
  lv_obj_t *cart_checkout_btn;
  lv_obj_t *cart_checkout_btn_txt;

  vd_cart_list_t cart_list;
  float total_amounts;
  bool cart_list_init;
} vd_home_page_t;

vd_home_page_t *vd_home_page_create(lv_obj_t *parant, uint16_t display_w, uint16_t display_h, lv_event_cb_t cart_checkout_cb);

vd_prod_card_t *vd_prod_card_create(vd_home_page_t *hp, const char *prod_name, float prod_full_price, float prod_disc_price, const void* prod_img_src, vd_prod_card_clicked_cb_t card_cb);

vd_cart_item_t *vd_cart_item_create(vd_home_page_t *hp, vd_prod_card_t *prod);
vd_err_t        vd_cart_item_delete(vd_cart_item_t *item);
vd_err_t        vd_cart_recalc_total_price(vd_home_page_t *hp);
vd_err_t        vd_cart_item_set_max_pcs(vd_cart_item_t *item, uint32_t max);
float           vd_cart_get_total_amounts(vd_home_page_t *hp);
uint32_t        vd_cart_get_total_items(vd_home_page_t *hp);

#ifdef __cplusplus
}
#endif

#endif /* VD_HOME_PAGE_H */