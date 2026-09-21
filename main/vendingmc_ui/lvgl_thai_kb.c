#include "lvgl_thai_kb.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define BACKS_SYM LV_SYMBOL_BACKSPACE
#define OK_SYM LV_SYMBOL_OK
#define WORLD_SYM "\xEF\x95\xBD"
#define ARROW_UP1 "\xEF\x84\x86"
#define ARROW_UP2 "\xEF\x84\x82"

#define FCTRL_BTN(size) (LV_KEYBOARD_CTRL_BUTTON_FLAGS | size)

#define THAI_LANG_KB 0
#define ENG_LANG_KB  1

/* =========================================================================
 * 1. แป้นพิมพ์ภาษาไทย (เกษมณี ปกติ)
 * ========================================================================= */
static const char * const th_kb_map_normal[] = {
  "ๅ", "/", "_", "ภ", "ถ", "ุ", "ึ", "ค", "ต", "จ", "ข", "ช", "\n",
  "ๆ", "ไ", "ำ", "พ", "ะ", "ั", "ี", "ร", "น", "ย", "บ", "ล", "\n",
  "ฟ", "ห", "ก", "ด", "เ", "้", "่", "า", "ส", "ว", "ง", "ฃ", "\n",
  ARROW_UP1, "ผ", "ป", "แ", "อ", "ิ", "ื", "ท", "ม", "ใ", "ฝ", BACKS_SYM, "\n",
  "?123", ",", WORLD_SYM, " ", ".", OK_SYM, ""                          
};

static const lv_buttonmatrix_ctrl_t th_kb_ctrl_normal[] = {
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  FCTRL_BTN(4), 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6,
  FCTRL_BTN(6), 4, FCTRL_BTN(4), 14, 4, FCTRL_BTN(7)
};

/* =========================================================================
 * 2. แป้นพิมพ์ภาษาไทย (เกษมณี Shift)
 * ========================================================================= */
static const char * const th_kb_map_shift[] = {
  "+", "๑", "๒", "๓", "๔", "ู", "฿", "๕", "๖", "๗", "๘", "๙", "\n",
  "๐", "\"", "ฎ", "ฑ", "ธ", "ํ", "๊", "ณ", "ฯ", "ญ", "ฐ", ",", "\n",
  "ฤ", "ฆ", "ฏ", "โ", "ฌ", "็", "๋", "ษ", "ศ", "ซ", ".", "ฅ", "\n",
  ARROW_UP2, "(", ")", "ฉ", "ฮ", "ฺ", "์", "?", "ฒ", "ฬ", "ฦ", BACKS_SYM, "\n",
  "?123", ",", WORLD_SYM, " ", ".", OK_SYM, ""
};

static const lv_buttonmatrix_ctrl_t th_kb_ctrl_shift[] = {
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  FCTRL_BTN(4), 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6,
  FCTRL_BTN(6), 4, FCTRL_BTN(4), 14, 3, FCTRL_BTN(7)
};

/* =========================================================================
 * 3. แป้นพิมพ์ภาษาอังกฤษ (QWERTY เล็ก)
 * ========================================================================= */
static const char * const en_kb_map_lower[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    ARROW_UP1, "z", "x", "c", "v", "b", "n", "m", BACKS_SYM, "\n",
    "?123", ",", WORLD_SYM, " ", ".", OK_SYM, ""};

static const lv_buttonmatrix_ctrl_t en_kb_ctrl_lower[] = {
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4, 4,
  FCTRL_BTN(6), 4, 4, 4, 4, 4, 4, 4, 6,
  FCTRL_BTN(6), 4, FCTRL_BTN(4), 14, 4, FCTRL_BTN(7)
};

/* =========================================================================
 * 4. แป้นพิมพ์ภาษาอังกฤษ (QWERTY ใหญ่)
 * ========================================================================= */
static const char * const en_kb_map_upper[] = {
  "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
  "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
  ARROW_UP2, "Z", "X", "C", "V", "B", "N", "M", BACKS_SYM, "\n",
  "?123", ",", WORLD_SYM, " ", ".", OK_SYM, ""
};

static const lv_buttonmatrix_ctrl_t en_kb_ctrl_upper[] = {
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4, 4,
  FCTRL_BTN(6), 4, 4, 4, 4, 4, 4, 4, 6,
  FCTRL_BTN(6), 4, FCTRL_BTN(4), 14, 4, FCTRL_BTN(7)
};

/* =========================================================================
 * 5. แป้นพิมพ์ตัวเลขและสัญลักษณ์สากล (?123)
 * ========================================================================= */
static const char * const sym_kb_map[] = {
  "1", "2", "3", "4", "5", "6", "7" ,"8", "9" ,"0", "\n",
  "@", "#", "$" ,"_" ,"&" ,"-" ,"+" ,"(" ,")" ,"/", "\n",
  "#123", "*", "\"", "'", ":", ";", "!", "?", BACKS_SYM, "\n",
  "ABกข", ",", WORLD_SYM, " ", ".", OK_SYM, ""
};

static const lv_buttonmatrix_ctrl_t sym_kb_ctrl[] = {
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  FCTRL_BTN(6), 4, 4, 4, 4, 4, 4, 4, 6,
  FCTRL_BTN(6), 4, FCTRL_BTN(4), 14, 4, FCTRL_BTN(7)
};

/* =========================================================================
 * 1. แป้นพิมพ์ตัวเลข Numpad / ทศนิยม สำหรับงาน POS
 * ========================================================================= */
static const char * const num_kb_map[] = {
  "1", "2", "3", "\n",
  "4", "5", "6", "\n",
  "!?#", "7", "8", "9", BACKS_SYM, "\n",
  "ABกข", ",", "0", ".", OK_SYM, ""
};

static const lv_buttonmatrix_ctrl_t num_kb_ctrl[] = {
  6, 6, 6,
  6, 6, 6,
  FCTRL_BTN(5), 5, 5, 5, 5,
  FCTRL_BTN(6), 4, 7, 4, FCTRL_BTN(6)
};

/* คอนฟิกโครงสร้างแนบตัว Textarea */
typedef struct {
    lv_obj_t * kb;
    kb_input_type_t type;
} ta_bind_cfg_t;

bool _kb_lang_type;
bool _kb_shift_stat;

static void kb_show(lv_obj_t * kb, bool show) {
  lv_obj_set_hidden(kb, !show);
}

/* Callback ประจำตัว Textarea แต่ละช่อง */
static void ta_focus_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    ta_bind_cfg_t * cfg = (ta_bind_cfg_t *)lv_event_get_user_data(e);
    lv_obj_t * kb = cfg->kb;

    if(code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);

        switch(cfg->type) {
            case KB_INPUT_NUMBER:
                lv_textarea_set_accepted_chars(ta, "0123456789");
                lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
                /* ซ่อนปุ่มจุดทศนิยมสำหรับเลขจำนวนเต็ม */
                lv_buttonmatrix_set_button_ctrl(kb, 14, LV_BUTTONMATRIX_CTRL_HIDDEN);
                lv_obj_set_size(kb, 450, 260); // ขนาดกะทัดรัดสำหรับ Numpad
                break;

            case KB_INPUT_DECIMAL:
                lv_textarea_set_accepted_chars(ta, "0123456789.-");
                lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
                lv_buttonmatrix_clear_button_ctrl(kb, 14, LV_BUTTONMATRIX_CTRL_HIDDEN);
                lv_obj_set_size(kb, 450, 260);
                break;

            case KB_INPUT_ALL:
            default:
                lv_textarea_set_accepted_chars(ta, NULL); // ปลดล็อกทุกตัวอักษร
                lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
                lv_obj_set_size(kb, 1024, 280); // กางเต็มจอสำหรับแป้นพิมพ์เต็ม
                break;
        }

        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        kb_show(kb, true);
    }
    else if(code == LV_EVENT_DEFOCUSED) {
        kb_show(kb, false);
    }
}

/* =========================================================================
 * ตัวควบคุมสถานะและการสลับโหมด
 * ========================================================================= */
typedef enum {
    KB_LANG_TH_NORMAL,
    KB_LANG_TH_SHIFT,
    KB_LANG_EN_LOWER,
    KB_LANG_EN_UPPER,
    KB_LANG_SYMBOLS,
    KB_LANG_NUMBER
} lvgl_kb_state_t;

static void lvgl_kb_apply_mode(lv_obj_t * kb, lvgl_kb_state_t state) {
    lv_obj_set_user_data(kb, (void *)(uintptr_t)state);

    switch(state) {
        case KB_LANG_TH_NORMAL:
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
            _kb_lang_type = THAI_LANG_KB;
            _kb_shift_stat = false;
            break;
        case KB_LANG_TH_SHIFT:
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_2);
            _kb_lang_type = THAI_LANG_KB;
            _kb_shift_stat = true;
            break;
        case KB_LANG_EN_LOWER:
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_3);
            _kb_lang_type = ENG_LANG_KB;
            _kb_shift_stat = false;
            break;
        case KB_LANG_EN_UPPER:
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_4);
            _kb_lang_type = ENG_LANG_KB;
            _kb_shift_stat = true;
            break;
        case KB_LANG_SYMBOLS:
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);
            _kb_shift_stat = false;
            break;
        case KB_LANG_NUMBER:
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
            _kb_shift_stat = false;
            break;
    }
}

static void _lvgl_kb_event_cb(lv_event_t * e) {
    lv_obj_t * kb = (lv_obj_t*)lv_event_get_target(e);
    uint32_t btn_id = lv_buttonmatrix_get_selected_button(kb);
    if(btn_id == LV_BUTTONMATRIX_BUTTON_NONE) return;

    lv_obj_t *ta = lv_keyboard_get_textarea(kb);

    const char * txt = lv_buttonmatrix_get_button_text(kb, btn_id);
    if(!txt) return;

    bool skip_add_txt = false;

    // เนื่องจากเราเขียน cb ของ keyboard เอง แล้วถอด cb ของ lvgl ออก เราเลยต้องเขียน logic จัดการเอง
    if(strcmp(txt, LV_SYMBOL_CLOSE) == 0 || strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) {
        skip_add_txt = true;
        lv_result_t res = lv_obj_send_event(kb, LV_EVENT_CANCEL, NULL);
        if(res != LV_RESULT_OK) return;

        if(ta) {
            res = lv_obj_send_event(ta, LV_EVENT_CANCEL, NULL);
            if(res != LV_RESULT_OK) return;
        }
        return;
    }else if(strcmp(txt, LV_SYMBOL_OK) == 0) {
        skip_add_txt = true;
        lv_result_t res = lv_obj_send_event(kb, LV_EVENT_READY, NULL);
        if(res != LV_RESULT_OK) return;

        if(ta) {
            res = lv_obj_send_event(ta, LV_EVENT_READY, NULL);
            if(res != LV_RESULT_OK) return;
        }
        return;
    }else if(strcmp(txt, "Enter") == 0 || strcmp(txt, LV_SYMBOL_NEW_LINE) == 0) {
        skip_add_txt = true;
        lv_textarea_add_char(ta, '\n');
        if(lv_textarea_get_one_line(ta)) {
            lv_result_t res = lv_obj_send_event(ta, LV_EVENT_READY, NULL);
            if(res != LV_RESULT_OK) return;
        }
    }else if(strcmp(txt, LV_SYMBOL_LEFT) == 0) {
        skip_add_txt = true;
        lv_textarea_cursor_left(ta);
    }else if(strcmp(txt, LV_SYMBOL_RIGHT) == 0) {
        skip_add_txt = true;
        lv_textarea_cursor_right(ta);
    }else if(strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        skip_add_txt = true;
        lv_textarea_delete_char(ta);
    }

    // ================================== ส่วนต่อไปนี้ เราเขียนเพิ่มเอง ================================== //

    if(strcmp(txt, ARROW_UP1) == 0 || strcmp(txt, ARROW_UP2) == 0 || strcmp(txt, "ABกข") == 0) {
        if     (_kb_lang_type == THAI_LANG_KB) lvgl_kb_apply_mode(kb, _kb_shift_stat ? KB_LANG_TH_NORMAL : KB_LANG_TH_SHIFT);
        else if(_kb_lang_type == ENG_LANG_KB)  lvgl_kb_apply_mode(kb, _kb_shift_stat ? KB_LANG_EN_LOWER  : KB_LANG_EN_UPPER);
        return;
    }else if(strcmp(txt, "?123") == 0 || strcmp(txt, "!?#") == 0) {
        lvgl_kb_apply_mode(kb, KB_LANG_SYMBOLS);
        return;
    }else if(strcmp(txt, "#123") == 0) {
        lvgl_kb_apply_mode(kb, KB_LANG_NUMBER);
        return;
    }else if(strcmp(txt, WORLD_SYM) == 0) {
        _kb_lang_type = !_kb_lang_type;
        lvgl_kb_apply_mode(kb, _kb_lang_type == THAI_LANG_KB ? KB_LANG_TH_NORMAL : KB_LANG_EN_LOWER);
        return;
    }else{
      if(ta && !skip_add_txt) lv_textarea_add_text(ta, txt);
    }

    /* คืนค่า Shift อัตโนมัติหลังพิมพ์ตัวอักษรพิเศษตัวถัดไป (พฤติกรรมเดียวกับมือถือ) */
    if(_kb_shift_stat && strcmp(txt, BACKS_SYM) != 0) {
        if     (_kb_lang_type == THAI_LANG_KB) lvgl_kb_apply_mode(kb, KB_LANG_TH_NORMAL);
        else if(_kb_lang_type == ENG_LANG_KB)  lvgl_kb_apply_mode(kb, KB_LANG_EN_LOWER );
    }
}

static void _ta_bind_cfg_free_cb(lv_event_t * e) {
    ta_bind_cfg_t * cfg = (ta_bind_cfg_t *)lv_event_get_user_data(e);
    if(cfg) free(cfg);
}

void lvgl_kb_bind_textarea(lv_obj_t * kb, lv_obj_t * ta, kb_input_type_t input_type) {
    if(!kb || !ta) return;

    ta_bind_cfg_t * cfg = (ta_bind_cfg_t *)malloc(sizeof(ta_bind_cfg_t));
    cfg->kb = kb;
    cfg->type = input_type;

    lv_keyboard_set_textarea(kb, ta);

    /* ผูก Event ตรวจจับการแตะเข้า/ออกจากช่องกรอก */
    lv_obj_add_event_cb(ta, ta_focus_event_cb, LV_EVENT_FOCUSED, cfg);
    lv_obj_add_event_cb(ta, ta_focus_event_cb, LV_EVENT_DEFOCUSED, cfg);

    /* คืนค่าแรมอัตโนมัติเมื่อ Textarea นั้นถูกทำลาย */
    lv_obj_add_event_cb(ta, _ta_bind_cfg_free_cb, LV_EVENT_DELETE, cfg);

    lvgl_kb_apply_mode(kb, KB_LANG_TH_NORMAL);
}

lv_obj_t * lvgl_thai_kb_create(lv_obj_t * parent, const lv_font_t * font) {
    lv_obj_t * kb = lv_keyboard_create(parent);

    if(font) lv_obj_set_style_text_font(kb, font, 0);

    /* เริ่มต้นให้ซ่อนไว้ก่อนใต้หน้าจอ */
    lv_obj_set_hidden(kb, true);
    lv_obj_set_align(kb, LV_ALIGN_BOTTOM_MID);

    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, th_kb_map_normal, th_kb_ctrl_normal);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_2, th_kb_map_shift, th_kb_ctrl_shift);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_3, en_kb_map_lower, en_kb_ctrl_lower);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_4, en_kb_map_upper, en_kb_ctrl_upper);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, sym_kb_map, sym_kb_ctrl);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_NUMBER, num_kb_map, num_kb_ctrl);

    lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(kb, _lvgl_kb_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return kb;
}