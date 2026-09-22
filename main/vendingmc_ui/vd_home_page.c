#include "vd_home_page.h"

static lv_style_t s_spdb_btn_style;
static bool s_spdb_style_inited = false;

static void _init_spdb_style_once(void) {
    if (s_spdb_style_inited) return;
    lv_style_init(&s_spdb_btn_style);
    lv_style_set_bg_color(&s_spdb_btn_style, lv_color_hex(0xF0F1F7));
    lv_style_set_border_opa(&s_spdb_btn_style, LV_OPA_TRANSP);
    lv_style_set_shadow_opa(&s_spdb_btn_style, LV_OPA_TRANSP);
    lv_style_set_radius(&s_spdb_btn_style, 10);
    lv_style_set_pad_all(&s_spdb_btn_style, 0);
    lv_style_set_size(&s_spdb_btn_style, 30, 30);
    s_spdb_style_inited = true;
}

/* 1. เริ่มต้นโครงสร้าง */
void vd_cart_list_init(vd_cart_list_t *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* 2. เพิ่มสินค้าลงตะกร้า (ขยายขนาดอัตโนมัติ) */
bool vd_cart_list_add(vd_cart_list_t *list, vd_cart_item_t *item) {
    if(!list || !item) return false;

    // ถ้าเต็มความจุ ให้ขยายขนาด (เริ่มจาก 4 -> 8 -> 16 -> ...)
    if(list->count >= list->capacity) {
        size_t new_capacity = (list->capacity == 0) ? 4 : list->capacity * 2;
        
        // ใช้ตัวแปรชั่วคราวรับค่า realloc เพื่อความปลอดภัยกรณีแรมเต็ม
        vd_cart_item_t **new_items = (vd_cart_item_t **)heap_caps_realloc(list->items, new_capacity * sizeof(vd_cart_item_t *), MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM);
        if(!new_items) {
            LV_LOG_ERROR("Cart list out of memory!");
            return false;
        }

        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count++] = item;

    LV_LOG_USER("new element add to cart list, total: %d, lastest: %p", list->count, item);

    return true;
}

vd_cart_item_t *vd_cart_list_get(vd_cart_list_t *list, size_t idx){
  if(!list || list->count == 0 || idx > list->count) return NULL;
  return list->items[idx];
}

/* 3. ลบสินค้าออกจากตะกร้าและขยับตำแหน่งที่เหลือ */
bool vd_cart_list_remove(vd_cart_list_t *list, vd_cart_item_t *item) {
    if(!list || !item || list->count == 0) return false;

    for(size_t i = 0; i < list->count; i++) {
        if(list->items[i] == item) {
            // เลื่อนข้อมูลตัวถัดไปมาทับตำแหน่งที่ลบ (Shift Left)
            for(size_t j = i; j < list->count - 1; j++) {
                list->items[j] = list->items[j + 1];
            }
            list->count--;
            list->items[list->count] = NULL; // ล้างพอยน์เตอร์ตัวท้ายสุด

            // หากของในตะกร้าถูกลบจนหมด สามารถคืนแรม Capacity ให้เล็กลงได้
            if(list->count == 0) {
                heap_caps_free(list->items);
                list->items = NULL;
                list->capacity = 0;
            }
            return true;
        }
    }
    return false;
}

/* 4. ล้างข้อมูลทั้งหมดในตะกร้า */
void vd_cart_list_free(vd_cart_list_t *list) {
    if(list->items) {
        heap_caps_free(list->items);
        list->items = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

size_t vd_cart_list_count(vd_cart_list_t *list){
  if(!list) return 0;
  return list->count;
}

void _vd_set_cart_empty_txt_enable(vd_home_page_t *hp, bool enable){
  if(!hp) return;
  lv_obj_set_hidden(hp->cart_empty_ico, !enable);
  lv_obj_set_hidden(hp->cart_empty_txt, !enable);
}

static void _vd_cart_item_final_delete_cb(lv_anim_t * a) {
  vd_cart_item_t *item = (vd_cart_item_t*)lv_anim_get_user_data(a);
  vd_home_page_t *hp = item->hp;

  vd_cart_item_delete(item);
  if(vd_cart_list_count(&hp->cart_list) == 0) _vd_set_cart_empty_txt_enable(hp, true);
  vd_cart_recalc_total_price(hp);
}

static void start_collapse_and_delete(lv_obj_t *wrapper, void* user_data) {
  lv_anim_t a_h;
  lv_anim_init(&a_h);
  lv_anim_set_var(&a_h, wrapper);
  lv_anim_set_values(&a_h, lv_obj_get_height(wrapper), 0);
  lv_anim_set_duration(&a_h, 180); // ยุบความสูงใน 180ms
  lv_anim_set_path_cb(&a_h, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a_h, (lv_anim_exec_xcb_t)_vd_anim_set_height);
  lv_anim_set_completed_cb(&a_h, _vd_cart_item_final_delete_cb);
  lv_anim_set_user_data(&a_h, user_data);
  lv_anim_start(&a_h);
}

static void _vd_prod_card_scale_anim_cb(void * var, int32_t v) {
  lv_obj_t * obj = (lv_obj_t *)var;
  lv_obj_set_style_transform_scale_x(obj, v, 0);
  lv_obj_set_style_transform_scale_y(obj, v, 0);
}

static void _vd_restore_card_scale(lv_obj_t *card) {
  lv_anim_delete(card, (lv_anim_exec_xcb_t)_vd_prod_card_scale_anim_cb);

  int32_t cur_scale = lv_obj_get_style_transform_scale_x(card, 0);
  if (cur_scale <= 0) cur_scale = 240;

  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, card);
  lv_anim_set_values(&a, cur_scale, 256);
  lv_anim_set_duration(&a, 180);
  lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)_vd_prod_card_scale_anim_cb);
  lv_anim_start(&a);

  lv_obj_set_style_bg_color(card, lv_color_white(), 0);
}

static void _vd_spdb_decr_value_cb(lv_event_t * e){
  vd_cart_item_t *item = (vd_cart_item_t*)lv_event_get_user_data(e);
  int val = atoi(lv_label_get_text(item->prod_spdbox->value_txt));
  
  if(val > item->min_pcs) {
    lv_label_set_text_fmt(item->prod_spdbox->value_txt, "%d", --val);
    if(val < item->max_pcs) lv_obj_set_disabled(item->prod_spdbox->up_btn, false);
  }

  if(val == item->min_pcs) lv_obj_set_disabled(item->prod_spdbox->down_btn, true);
  vd_cart_recalc_total_price(item->hp);
}

static void _vd_spdb_incr_value_cb(lv_event_t * e){
  vd_cart_item_t *item = (vd_cart_item_t*)lv_event_get_user_data(e);
  int val = atoi(lv_label_get_text(item->prod_spdbox->value_txt));

  if(val < item->max_pcs) {
    lv_label_set_text_fmt(item->prod_spdbox->value_txt, "%d", ++val);
    if(val > item->min_pcs) lv_obj_set_disabled(item->prod_spdbox->down_btn, false);
  }

  if(val == item->max_pcs) lv_obj_set_disabled(item->prod_spdbox->up_btn, true);
  vd_cart_recalc_total_price(item->hp);
}

vd_spinbox_t *vd_spinbox_create(lv_obj_t *parant, uint16_t width, uint16_t height, void* user_data){
  vd_spinbox_t *spdb = (vd_spinbox_t*)malloc(sizeof(vd_spinbox_t));
  if(!spdb) {
    LV_LOG_USER("Create new vd_spinbox_t fail.");
    return NULL;
  }

  spdb->main = lv_obj_create(parant);

  _init_spdb_style_once(); // เตรียมสไตล์แบบ static

  lv_obj_set_size             (spdb->main, width, height);
  lv_obj_set_style_bg_opa     (spdb->main, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa (spdb->main, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all    (spdb->main, 0, 0);
  lv_obj_set_scrollable       (spdb->main, true);

  spdb->value_txt = lv_label_create(spdb->main);

  spdb->down_btn      = lv_button_create(spdb->main);
  spdb->down_btn_sign = lv_label_create(spdb->down_btn);

  lv_obj_add_style            (spdb->down_btn, &s_spdb_btn_style, 0);
  lv_obj_set_align            (spdb->down_btn, LV_ALIGN_LEFT_MID);
  lv_obj_set_scrollable       (spdb->down_btn, false);
  lv_obj_set_ext_click_area   (spdb->down_btn, 10);
  lv_obj_add_event_cb         (spdb->down_btn, _vd_spdb_decr_value_cb, LV_EVENT_RELEASED, user_data);

  lv_label_set_text           (spdb->down_btn_sign, LV_SYMBOL_MINUS);
  lv_obj_set_align            (spdb->down_btn_sign, LV_ALIGN_CENTER);
  lv_obj_set_style_text_color (spdb->down_btn_sign, lv_color_make(0, 0, 0), 0);
  lv_obj_set_style_text_font  (spdb->down_btn_sign, &lv_font_montserrat_12, 0);

  spdb->up_btn      = lv_button_create(spdb->main);
  spdb->up_btn_sign = lv_label_create(spdb->up_btn);

  lv_obj_add_style            (spdb->up_btn, &s_spdb_btn_style, 0);
  lv_obj_set_align            (spdb->up_btn, LV_ALIGN_RIGHT_MID);
  lv_obj_set_scrollable       (spdb->up_btn, false);
  lv_obj_set_ext_click_area   (spdb->up_btn, 10);
  lv_obj_add_event_cb         (spdb->up_btn, _vd_spdb_incr_value_cb, LV_EVENT_RELEASED, user_data);

  lv_label_set_text           (spdb->up_btn_sign, LV_SYMBOL_PLUS);
  lv_obj_set_align            (spdb->up_btn_sign, LV_ALIGN_CENTER);
  lv_obj_set_style_text_color (spdb->up_btn_sign, lv_color_make(0, 0, 0), 0);
  lv_obj_set_style_text_font  (spdb->up_btn_sign, &lv_font_montserrat_12, 0);

  lv_obj_set_align            (spdb->value_txt, LV_ALIGN_CENTER);
  lv_label_set_text           (spdb->value_txt, "0");

  return spdb;
}

void vd_spinbox_set_value(vd_spinbox_t *spdb, uint32_t value){
  if(!spdb) return;
  lv_label_set_text_fmt(spdb->value_txt, "%lu", value);
}

int32_t vd_spinbox_get_value(vd_spinbox_t *spdb){
  if(!spdb) return 0;
  return atoi(lv_label_get_text(spdb->value_txt));
}

vd_err_t vd_spinbox_delete(vd_spinbox_t *spdb){
  if(!spdb) return VD_OK;

  lv_obj_delete_async(spdb->main);

  free(spdb);
  spdb = NULL;

  return VD_OK;
}

static void _vd_cart_item_slide_out_completed_cb(lv_anim_t * a) {
  lv_obj_t * fg_card = (lv_obj_t *)a->var;
  lv_obj_t * wrapper = lv_obj_get_parent(fg_card);
  start_collapse_and_delete(wrapper, lv_anim_get_user_data(a));
}

static void _card_ecb(lv_event_t *e){
  vd_prod_card_t *card = (vd_prod_card_t *)lv_event_get_user_data(e);
  if(!card || !card->clicked_cb) return;

  lv_event_code_t code = lv_event_get_code(e);

  if(code == LV_EVENT_PRESSED) {
    lv_obj_set_style_bg_color(card->main, lv_color_hex(0xF0F2F5), 0);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST || code == LV_EVENT_SCROLL_BEGIN) {
    lv_obj_set_style_bg_color(card->main, lv_color_white(), 0);
  } else if (code == LV_EVENT_CLICKED) {
    lv_obj_set_style_bg_color(card->main, lv_color_white(), 0);

    // เล่นแอนิเมชันเด้ง (Punch / Bounce) สั้นๆ เมื่อยืนยันการเลือก
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, card->main);
    lv_anim_set_values(&a, 240, 256);
    lv_anim_set_duration(&a, 150);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)_vd_prod_card_scale_anim_cb);
    lv_anim_start(&a);

    card->clicked_cb(card, e);
  }
}

static void _cart_item_swipe_cb(lv_event_t *e) {
  vd_cart_item_t *item = (vd_cart_item_t*)lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *fg_card = lv_event_get_target(e);
  lv_indev_t *indev = lv_indev_active();

  static int32_t total_drag_x = 0;

  if (code == LV_EVENT_PRESSED) {
    total_drag_x = lv_obj_get_style_translate_x(fg_card, 0);
  } else if (code == LV_EVENT_PRESSING) {
    if (!indev) return;
    lv_point_t vect;
    lv_indev_get_vect(indev, &vect);

    total_drag_x += vect.x;

    // อนุญาตให้ปัดเฉพาะไปทางซ้าย
    if (total_drag_x > 0) total_drag_x = 0;

    // เลื่อนกล่องสินค้าไปตามนิ้วในแนวตรง
    lv_obj_set_style_translate_x(fg_card, total_drag_x, 0);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    int32_t cur_x = lv_obj_get_style_translate_x(fg_card, 0);
    int32_t w = lv_obj_get_width(fg_card);
    int32_t threshold = -(w / 3); // เกณฑ์ 33%

    if (cur_x < threshold) {
      /* -------------------------------------------------------------
       * [กรณีลบ]: ปัดหลุดเกณฑ์ -> สไลด์ออกซ้ายให้พ้นจอ แล้วยุบแถว
       * ------------------------------------------------------------- */
      lv_anim_t a_x; lv_anim_init(&a_x);  
      lv_anim_set_var(&a_x, fg_card);
      lv_anim_set_values(&a_x, cur_x, -w - 30);
      lv_anim_set_duration(&a_x, 180);
      lv_anim_set_path_cb(&a_x, lv_anim_path_ease_in);
      lv_anim_set_exec_cb(&a_x, (lv_anim_exec_xcb_t)_vd_anim_set_translate_x);
      lv_anim_set_completed_cb(&a_x, _vd_cart_item_slide_out_completed_cb);
      lv_anim_set_user_data(&a_x, item);
      lv_anim_start(&a_x);

      // ค่อยๆ ปรับจางหายไปพร้อมกัน
      lv_anim_t a_opa; lv_anim_init(&a_opa);
      lv_anim_set_var(&a_opa, fg_card);
      lv_anim_set_values(&a_opa, lv_obj_get_style_opa(fg_card, 0), 0);
      lv_anim_set_duration(&a_opa, 180);
      lv_anim_set_exec_cb(&a_opa, (lv_anim_exec_xcb_t)_vd_anim_set_opa);
      lv_anim_start(&a_opa);
    } else {
      /* -------------------------------------------------------------
       * [กรณีคืนค่า]: เด้งสปริง Snap-back กลับมาปิดแถบแดง
       * ------------------------------------------------------------- */
      lv_anim_t a_x; lv_anim_init(&a_x);
      lv_anim_set_var(&a_x, fg_card);
      lv_anim_set_values(&a_x, cur_x, 0);
      lv_anim_set_duration(&a_x, 220);
      lv_anim_set_path_cb(&a_x, lv_anim_path_overshoot);
      lv_anim_set_exec_cb(&a_x, (lv_anim_exec_xcb_t)_vd_anim_set_translate_x);
      lv_anim_start(&a_x);
    }
  }
}

vd_home_page_t *vd_home_page_create(lv_obj_t *parant, uint16_t display_w, uint16_t display_h, lv_event_cb_t cart_checkout_cb){
  vd_home_page_t *hp = (vd_home_page_t*)calloc(1, sizeof(vd_home_page_t));
  if(!hp) {
    LV_LOG_USER("Create new vd_home_page_t fail.");
    return NULL;
  }

  hp->keyboard = lvgl_thai_kb_create(lv_screen_active(), &anuphan_18);

  hp->prod_flex = lv_obj_create(parant);

  lv_obj_set_size             (hp->prod_flex, 690, 480); // สำหรับจอ 1024x600
  lv_obj_set_pos              (hp->prod_flex, 0, 90);
  lv_obj_set_flex_flow        (hp->prod_flex, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_bg_opa     (hp->prod_flex, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa (hp->prod_flex, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode   (hp->prod_flex, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_column (hp->prod_flex, 5, 0);
  lv_obj_update_layout        (hp->prod_flex);

  hp->app_logo = lv_image_create(parant);

  lv_image_set_src            (hp->app_logo, &nvdm200_51);
  lv_image_set_scale          (hp->app_logo, 160);
  lv_obj_align                (hp->app_logo, LV_ALIGN_TOP_LEFT, -5, 20);

  hp->prod_search_ta = lv_textarea_create(parant);

  lv_textarea_set_one_line        (hp->prod_search_ta, true);
  lv_textarea_set_placeholder_text(hp->prod_search_ta, "ค้นหาสินค้า...");
  lvgl_kb_bind_textarea           ( hp->keyboard, hp->prod_search_ta, KB_INPUT_ALL);
  lv_obj_set_scrollbar_mode       (hp->prod_search_ta, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_pos                  (hp->prod_search_ta, lv_obj_get_x2(hp->app_logo) - 10, 25);
  lv_obj_set_width                (hp->prod_search_ta, 260);
  lv_obj_set_style_radius         (hp->prod_search_ta, 20, 0);
  lv_obj_set_style_text_font      (hp->prod_search_ta, &anuphan_14, 0);

  hp->cart = lv_obj_create(parant);

  lv_obj_set_size                 (hp->cart, 350, display_h);
  lv_obj_align                    (hp->cart, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_radius         (hp->cart, 0, 0);
  lv_obj_set_style_border_side    (hp->cart, LV_BORDER_SIDE_LEFT, 0);

  hp->cart_title_txt = lv_label_create(hp->cart);

  lv_obj_set_style_text_font      (hp->cart_title_txt, &anuphan_med_22, 0);
  lv_obj_set_style_text_color     (hp->cart_title_txt, lv_color_make(0, 0, 0), 0);
  lv_label_set_text               (hp->cart_title_txt, "ตระกร้าของฉัน");
  lv_obj_set_pos                  (hp->cart_title_txt, 0, 10);

  hp->cart_prod_list = lv_obj_create(hp->cart);

  lv_obj_align                    (hp->cart_prod_list, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_flex_flow            (hp->cart_prod_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_size                 (hp->cart_prod_list, lv_pct(100), lv_pct(72));
  lv_obj_set_style_border_opa     (hp->cart_prod_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa         (hp->cart_prod_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all        (hp->cart_prod_list, 0, 0);
  lv_obj_set_style_pad_gap        (hp->cart_prod_list, 0, 0);

  hp->cart_empty_txt = lv_label_create(hp->cart_prod_list);

  lv_obj_set_style_text_font      (hp->cart_empty_txt, &anuphan_16, 0);
  lv_label_set_text               (hp->cart_empty_txt, "ยังไม่มีสินค้าเลย\nแตะที่สินค้าเพื่อเพิ่มลงตระกร้า");
  lv_obj_set_floating             (hp->cart_empty_txt, true);
  lv_obj_align                    (hp->cart_empty_txt, LV_ALIGN_CENTER, 0, 25);
  lv_obj_set_style_text_align     (hp->cart_empty_txt, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color     (hp->cart_empty_txt, lv_color_make(180, 180, 180), 0);

  hp->cart_empty_ico = lv_image_create(hp->cart_prod_list);
  
  lv_image_set_src                (hp->cart_empty_ico, &cart250_219);
  lv_image_set_scale              (hp->cart_empty_ico, 64);
  lv_obj_set_floating             (hp->cart_empty_ico, true);
  lv_obj_align                    (hp->cart_empty_ico, LV_ALIGN_CENTER, -5, -40);

  hp->cart_checkout = lv_obj_create(hp->cart);

  lv_obj_set_align                (hp->cart_checkout, LV_ALIGN_BOTTOM_MID);
  lv_obj_set_size                 (hp->cart_checkout, lv_pct(100), lv_pct(20));
  lv_obj_set_style_border_side    (hp->cart_checkout, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_pad_all        (hp->cart_checkout, 0, 0);
  lv_obj_set_overflow_visible     (hp->cart_checkout, true);
  lv_obj_set_ext_draw_size        (hp->cart_checkout, 15);

  hp->cart_checkout_total_txt = lv_label_create(hp->cart_checkout);

  lv_obj_set_style_text_font      (hp->cart_checkout_total_txt, &anuphan_16, 0);
  lv_obj_set_style_text_color     (hp->cart_checkout_total_txt, lv_color_make(0, 0, 0), 0);
  lv_label_set_text               (hp->cart_checkout_total_txt, "ยอดรวมทั้งหมด");
  lv_obj_align                    (hp->cart_checkout_total_txt, LV_ALIGN_TOP_LEFT, 10, 20);

  hp->cart_checkout_amount = lv_label_create(hp->cart_checkout);

  lv_obj_set_style_text_font      (hp->cart_checkout_amount, &anuphan_bold_18, 0);
  lv_obj_set_style_text_color     (hp->cart_checkout_amount, lv_color_make(0, 0, 0), 0);
  lv_label_set_text               (hp->cart_checkout_amount, "฿0");
  lv_obj_align                    (hp->cart_checkout_amount, LV_ALIGN_TOP_RIGHT, -10, 20);

  hp->cart_checkout_btn = lv_button_create(hp->cart_checkout);

  lv_obj_set_style_bg_color       (hp->cart_checkout_btn, lv_color_make(0x5B, 0x6E, 0xF5), 0);
  lv_obj_set_width                (hp->cart_checkout_btn, lv_pct(100));
  lv_obj_set_align                (hp->cart_checkout_btn, LV_ALIGN_BOTTOM_MID);
  lv_obj_add_event_cb             (hp->cart_checkout_btn, cart_checkout_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_set_disabled             (hp->cart_checkout_btn, true);

  hp->cart_checkout_btn_txt = lv_label_create(hp->cart_checkout_btn);

  lv_obj_set_style_text_font      (hp->cart_checkout_btn_txt , &anuphan_bold_18, 0);
  lv_label_set_text               (hp->cart_checkout_btn_txt , "ดำเนินการต่อ");
  lv_obj_set_align                (hp->cart_checkout_btn_txt , LV_ALIGN_CENTER);

  return hp;
}

vd_prod_card_t *vd_prod_card_create(vd_home_page_t *hp, const char *prod_name, float prod_full_price, float prod_disc_price, const void* prod_img_src, vd_prod_card_clicked_cb_t card_cb){
  if(!hp || !hp->prod_flex) return NULL;
  if(!prod_name || prod_full_price <= 0.0) {
    LV_LOG_USER("prod_name or prod_full_price invalid.");
    return NULL;
  }

  vd_prod_card_t *card = (vd_prod_card_t*)calloc(1, sizeof(vd_prod_card_t));
  if(!card) {
    LV_LOG_USER("Create new vd_prod_card_t fail.");
    return NULL;
  }

  card->hp            = hp;
  card->clicked_cb    = card_cb;
  card->have_discount = (prod_full_price > 0.0 && prod_disc_price > 0.0) && (prod_full_price > prod_disc_price);
  card->full_price    = prod_full_price;
  card->disc_price    = prod_disc_price;

  card->main = lv_obj_create(hp->prod_flex);

  lv_obj_set_size                   (card->main, lv_pct(32), 250);
  lv_obj_set_style_border_opa       (card->main, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa       (card->main, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color         (card->main, lv_color_make(255, 255, 255), 0);
  lv_obj_set_style_clip_corner      (card->main, false, 0);
  lv_obj_set_overflow_visible       (card->main, false);
  lv_obj_set_style_pad_all          (card->main, 0, 0);
  lv_obj_set_style_radius           (card->main, 20, 0);
  lv_obj_set_scrollable             (card->main, false);

  // เปิดให้คลิกได้ + ส่งต่อแรงปัดแนวตั้งไปให้ Flex Container
  lv_obj_set_clickable              (card->main, true);
  lv_obj_set_scroll_chain_ver       (card->main, true);

  lv_obj_set_style_transform_pivot_x(card->main, lv_pct(50), 0);
  lv_obj_set_style_transform_pivot_y(card->main, lv_pct(50), 0);
  lv_obj_set_style_transform_scale_x(card->main, 256, 0);
  lv_obj_set_style_transform_scale_y(card->main, 256, 0);
  lv_obj_add_event_cb               (card->main, _card_ecb, LV_EVENT_PRESSED, card);
  lv_obj_add_event_cb               (card->main, _card_ecb, LV_EVENT_RELEASED, card);
  lv_obj_add_event_cb               (card->main, _card_ecb, LV_EVENT_PRESS_LOST, card);
  lv_obj_add_event_cb               (card->main, _card_ecb, LV_EVENT_SCROLL_BEGIN, card);
  lv_obj_add_event_cb               (card->main, _card_ecb, LV_EVENT_CLICKED, card);

  card->prod_name = lv_label_create(card->main);

  lv_obj_set_style_text_font        (card->prod_name, &anuphan_16, 0);
  lv_obj_set_style_text_color       (card->prod_name, lv_color_make(0, 0, 0), 0);
  lv_label_set_text                 (card->prod_name, prod_name);
  lv_obj_set_pos                    (card->prod_name, 10, 180);

  card->prod_full_price = lv_label_create(card->main);
  char price[32];
  vd_format_price(card->have_discount ? prod_disc_price : prod_full_price, price);

  lv_obj_set_style_text_font        (card->prod_full_price, &anuphan_22, 0);
  lv_obj_set_style_text_color       (card->prod_full_price, card->have_discount ? lv_color_make(240, 0, 0) : lv_color_make(0, 0, 0), 0);
  lv_label_set_text_fmt             (card->prod_full_price, "฿%s", price);
  lv_obj_set_pos                    (card->prod_full_price, 10, 205);
  lv_obj_update_layout              (card->prod_full_price);

  if(card->have_discount){
    card->prod_disc_price = lv_label_create(card->main);
    vd_format_price(prod_full_price, price);

    lv_obj_set_style_text_font      (card->prod_disc_price, &anuphan_14, 0);
    lv_obj_set_style_text_color     (card->prod_disc_price, lv_color_make(180, 180, 180), 0);
    lv_obj_set_style_text_decor     (card->prod_disc_price, LV_TEXT_DECOR_STRIKETHROUGH, 0);
    lv_label_set_text_fmt           (card->prod_disc_price, "฿%s", price);
    lv_obj_align_to                 (card->prod_disc_price, card->prod_full_price, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -4);
  }

  card->prod_img = lv_image_create(card->main);

  lv_image_set_src                  (card->prod_img, prod_img_src);
  lv_obj_align                      (card->prod_img, LV_ALIGN_TOP_MID, 0, -40);
  lv_obj_set_overflow_visible       (card->prod_img, false);
  lv_image_set_antialias            (card->prod_img, false);
  lv_image_set_blend_mode           (card->prod_img, LV_BLEND_MODE_NORMAL);

  return card;
}

vd_cart_item_t *vd_cart_item_create(vd_home_page_t *hp, vd_prod_card_t *prod){
  if(!hp || !prod || !hp->cart_prod_list) return NULL;

  vd_cart_item_t *item = (vd_cart_item_t*)calloc(1, sizeof(vd_cart_item_t));
  if(!item) {
    LV_LOG_USER("Create new vd_cart_item_t fail.");
    return NULL;
  }

  item->hp      = hp;
  item->max_pcs = 99;
  item->min_pcs = 1;

  item->main_bg = lv_obj_create(hp->cart_prod_list);

  lv_obj_set_size               (item->main_bg, lv_pct(100), 65);
  lv_obj_set_style_pad_all      (item->main_bg, 0, 0);
  lv_obj_set_style_border_opa   (item->main_bg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa       (item->main_bg, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollable         (item->main_bg, false);
  lv_obj_set_style_radius       (item->main_bg, 0, 0);
  lv_obj_set_style_clip_corner  (item->main_bg, true, 0); // ซ่อนส่วนที่ล้นตอนสไลด์
  //lv_obj_null_on_delete         (&item->main_bg);

  item->bg_delete = lv_obj_create(item->main_bg);

  lv_obj_set_size               (item->bg_delete, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color     (item->bg_delete, lv_color_hex(0xFF3B30), 0); // สีแดงสด Danger
  lv_obj_set_style_bg_opa       (item->bg_delete, LV_OPA_COVER, 0);
  lv_obj_set_style_border_opa   (item->bg_delete, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius       (item->bg_delete, 0, 0);
  lv_obj_set_style_pad_all      (item->bg_delete, 0, 0);
  lv_obj_set_scrollable         (item->bg_delete, false);

  item->trash_icn = lv_label_create(item->bg_delete);

  lv_label_set_text             (item->trash_icn, LV_SYMBOL_TRASH);
  lv_obj_set_style_text_color   (item->trash_icn, lv_color_white(), 0);
  lv_obj_set_style_text_font    (item->trash_icn, &lv_font_montserrat_18, 0);
  lv_obj_align                  (item->trash_icn, LV_ALIGN_RIGHT_MID, -22, 0);

  item->main = lv_obj_create(item->main_bg);

  lv_obj_set_size               (item->main, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color     (item->main, lv_color_white(), 0);
  lv_obj_set_style_bg_opa       (item->main, LV_OPA_COVER, 0); // สำคัญ: ต้องทึบแสง 100% เพื่อบังแถบแดง
  lv_obj_set_style_border_side  (item->main, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color (item->main, lv_color_make(235, 235, 235), 0);
  lv_obj_set_style_border_width (item->main, 1, 0);
  lv_obj_set_style_radius       (item->main, 0, 0);
  lv_obj_set_style_pad_all      (item->main, 0, 0);
  lv_obj_set_scrollable         (item->main, false);
  lv_obj_set_clickable          (item->main, true);
  lv_obj_add_event_cb           (item->main, _cart_item_swipe_cb, LV_EVENT_PRESSED, item);
  lv_obj_add_event_cb           (item->main, _cart_item_swipe_cb, LV_EVENT_PRESSING, item);
  lv_obj_add_event_cb           (item->main, _cart_item_swipe_cb, LV_EVENT_RELEASED, item);
  lv_obj_add_event_cb           (item->main, _cart_item_swipe_cb, LV_EVENT_PRESS_LOST, item);

  item->prod_img_bg = lv_obj_create(item->main);

  lv_obj_set_size               (item->prod_img_bg, lv_pct(17), lv_pct(80));
  lv_obj_set_align              (item->prod_img_bg, LV_ALIGN_LEFT_MID);
  lv_obj_set_style_clip_corner  (item->prod_img_bg, true, 0);
  lv_obj_set_overflow_visible   (item->prod_img_bg, false);
  lv_obj_set_scrollable         (item->prod_img_bg, false);
  lv_obj_set_style_bg_opa       (item->prod_img_bg, 0, 0);
  lv_obj_set_style_border_opa   (item->prod_img_bg, 0, 0);
  lv_obj_set_style_radius       (item->prod_img_bg, 10, 0);

  const void* img_src = lv_image_get_src(prod->prod_img);

  if(img_src){
    item->prod_img = lv_image_create(item->prod_img_bg);

    lv_image_set_src            (item->prod_img, img_src);
    lv_image_set_scale          (item->prod_img, 68);
    lv_obj_set_align            (item->prod_img, LV_ALIGN_CENTER);
    lv_obj_set_overflow_visible (item->prod_img, false);
  }

  item->prod_name = lv_label_create(item->main);

  lv_obj_set_style_text_font    (item->prod_name, &anuphan_16, 0);
  lv_obj_set_style_text_color   (item->prod_name, lv_color_make(0, 0, 0), 0);
  lv_label_set_text             (item->prod_name, lv_label_get_text(prod->prod_name));
  lv_obj_align_to               (item->prod_name, item->prod_img_bg, LV_ALIGN_OUT_RIGHT_TOP, 10, 8);

  item->prod_price = lv_label_create(item->main);
  char price[32];
  item->price = prod->have_discount ? prod->disc_price : prod->full_price;
  vd_format_price(item->price, price);

  lv_obj_set_style_text_font    (item->prod_price, &anuphan_14, 0);
  lv_obj_set_style_text_color   (item->prod_price, lv_color_make(0x5B, 0x6E, 0xF5), 0);
  lv_label_set_text_fmt         (item->prod_price, "%s บาท", price);
  lv_obj_align_to               (item->prod_price, item->prod_img_bg, LV_ALIGN_OUT_RIGHT_TOP, 10, 30);

  item->prod_spdbox = vd_spinbox_create(item->main, 100, 35, item);

  lv_obj_set_align              (item->prod_spdbox->main, LV_ALIGN_RIGHT_MID);
  vd_spinbox_set_value          (item->prod_spdbox, item->min_pcs);

  if(!hp->cart_list_init) {
    hp->cart_list_init = true;
    vd_cart_list_init(&hp->cart_list);
  }

  if(vd_cart_list_count(&hp->cart_list) == 0) _vd_set_cart_empty_txt_enable(hp, false);
  vd_cart_list_add(&hp->cart_list, item);
  vd_cart_recalc_total_price(hp);

  return item;
}

vd_err_t vd_cart_item_delete(vd_cart_item_t *item){
  if(item == NULL || !item || !item->hp) return VD_OK;

  vd_cart_list_remove(&item->hp->cart_list, item);
  lv_image_cache_drop(lv_image_get_src(item->prod_img));

  lv_obj_delete_async(item->main_bg);
  vd_spinbox_delete  (item->prod_spdbox);

  free(item);
  item = NULL;

  return VD_OK;
}

vd_err_t vd_cart_recalc_total_price(vd_home_page_t *hp){
  if(!hp) return VD_ERR_PARAM_INVALID;

  size_t cart_total_item = vd_cart_list_count(&hp->cart_list);
  if(cart_total_item == 0) {
    hp->total_amounts = 0;
    lv_label_set_text(hp->cart_checkout_amount, "฿0");
    lv_obj_set_disabled(hp->cart_checkout_btn, true);
    return VD_OK;
  }

  float amount = 0;
  int item_count = 0;
  char amount_str[32];

  for(size_t i = 0; i < cart_total_item; i++){
    vd_cart_item_t *item = vd_cart_list_get(&hp->cart_list, i);
    item_count = atoi(lv_label_get_text(item->prod_spdbox->value_txt));
    amount += item->price * (float)item_count;
  }

  hp->total_amounts = amount;
  vd_format_price(amount, amount_str);
  lv_label_set_text_fmt(hp->cart_checkout_amount, "฿%s", amount_str);

  lv_obj_set_disabled(hp->cart_checkout_btn, false);

  return VD_OK;
}

vd_err_t vd_cart_item_set_max_pcs(vd_cart_item_t *item, uint32_t max){
  if(!item || max > 10000) return VD_ERR_PARAM_INVALID;

  item->max_pcs = max;
  item->min_pcs = 1;

  return VD_OK;
}

float vd_cart_get_total_amounts(vd_home_page_t *hp){
  if(!hp) return 0;
  return hp->total_amounts;
}

uint32_t vd_cart_get_total_items(vd_home_page_t *hp){
  if(!hp) return 0;
  return (uint32_t)vd_cart_list_count(&hp->cart_list);
}