#include "vd_menu.h"
#include <stdlib.h>
#include <string.h>
//#include "vd_font_manager.h"

LV_FONT_DECLARE(font_awesome_12);
LV_FONT_DECLARE(font_awesome_20);

LV_FONT_DECLARE(anuphan_bold_16);
LV_FONT_DECLARE(anuphan_bold_18);
LV_FONT_DECLARE(anuphan_bold_20);
LV_FONT_DECLARE(anuphan_bold_22);
LV_FONT_DECLARE(anuphan_semi_bold_16);
LV_FONT_DECLARE(anuphan_med_14);
LV_FONT_DECLARE(anuphan_med_16);
LV_FONT_DECLARE(anuphan_16);
LV_FONT_DECLARE(anuphan_14);
LV_FONT_DECLARE(anuphan_12);

/* โครงสร้างเก็บข้อมูลของแต่ละปุ่มเมนู */
typedef struct _vd_menu_node_t {
    uint32_t                id;
    char                    title[64];
    lv_obj_t               *btn_obj;
    lv_obj_t               *icon_obj;
    lv_obj_t               *label_obj;
    struct _vd_menu_node_t *next;
} _vd_menu_node_t;

/* โครงสร้างหลักของ Sidebar */
struct vd_menu_sidebar_t {
    lv_obj_t        *panel;        // คอนเทนเนอร์หลักของ Sidebar
    lv_obj_t        *scroll_area;  // โซนเลื่อนสำหรับรายการปุ่มด้านบน
    lv_obj_t        *bottom_area;  // โซนล่างสำหรับปุ่มออก
    lv_obj_t        *exit_btn;
    uint32_t         active_id;
    vd_menu_cb_t     cb;
    void            *user_data;
    _vd_menu_node_t *head;
};

/* ── โครงสร้างภายใน Content & Sub-frame ── */
struct vd_menu_content_t {
    lv_obj_t *container;
};

struct vd_menu_sub_frame_t {
    lv_obj_t *card;
    uint32_t  item_count;
};

/* โครงสร้างข้อมูลสำหรับจัดการสถานะกลุ่มปุ่ม */
typedef struct {
    lv_obj_t      *group_box;
    lv_event_cb_t  user_cb;
    void          *user_data;
    uint32_t       selected_index;
    uint32_t       total_choices;
} _vd_menu_btn_group_ctx_t;

/* โครงสร้างเก็บคอนฟิกของ Time Range แต่ละแถว */
typedef struct {
    bool        show_hour;
    bool        show_min;
    bool        show_sec;
    uint16_t    hour_step;
    uint16_t    min_step;
    uint16_t    sec_step;
    lv_obj_t   *start_box;
    lv_obj_t   *start_lbl;
    lv_obj_t   *end_box;
    lv_obj_t   *end_lbl;
    lv_event_cb_t user_cb;
    void       *user_data;
} _vd_menu_time_range_ctx_t;

/* โครงสร้างสำหรับ Modal Dialog */
typedef struct {
    _vd_menu_time_range_ctx_t *range_ctx;
    lv_obj_t *target_label;
    lv_obj_t *modal_backdrop;
    lv_obj_t *roller_hour;
    lv_obj_t *roller_min;
    lv_obj_t *roller_sec;
} _vd_menu_time_modal_ctx_t;

/* Event จัดการการกดเพิ่ม-ลดใน Stepper */
typedef struct {
    int32_t val;
    int32_t min;
    int32_t max;
    const char *unit;
    lv_obj_t *val_lbl;
    lv_event_cb_t user_cb;
    void *user_data;
} _vd_stepper_ctx_t;

/* โครงสร้าง Context คุมหน้าต่างแก้ไข */
typedef struct {
    vd_slot_data_t        *target_slot;
    vd_slot_data_t         draft_slot;
    uint16_t               low_threshold;
    vd_slot_modal_cbs_t    cbs;
    void                  *user_data;
    bool                   closing;

    lv_obj_t              *backdrop;
    lv_obj_t              *dialog;
    lv_obj_t              *confirm_layer;
    lv_obj_t              *m_code_box;
    lv_obj_t              *m_sub_lbl;

    /* รูปภาพปก */
    lv_obj_t              *cover_box;
    lv_obj_t              *cover_img;
    lv_obj_t              *cover_ph_label;
    lv_obj_t              *ta_img_path;

    /* ข้อมูลการขาย */
    lv_obj_t              *ta_name;
    lv_obj_t              *ta_price;
    lv_obj_t              *sw_discount;
    lv_obj_t              *r_discount;  // แถวป้อนยอดส่วนลด (ซ่อน/แสดงได้)
    lv_obj_t              *ta_discount;
    lv_obj_t              *discount_net_lbl; // ป้ายราคาขายจริง หรือแจ้งเตือนสีแดง

    /* การจัดการช่อง */
    lv_obj_t              *stock_val_lbl;
    lv_obj_t              *cap_val_lbl;
    lv_obj_t              *low_val_lbl;
    lv_obj_t              *sw_enabled;
    lv_obj_t              *btn_save;
    lv_obj_t              *motor_result_lbl;
    lv_obj_t              *motor_test_btn;
} _vd_slot_modal_ctx_t;

/* Context ประจำหน้าไฟล์ (จอง Dynamic ทั้งหมด) */
typedef struct {
    vd_file_browser_cbs_t cbs;
    void                 *user_data;

    char                 *current_drive_id;   // จองขนาดตามจริง
    char                 *current_drive_name; // จองขนาดตามจริง
    char                 *current_path;       // บัฟเฟอร์ขนาดสูงสุด 513 bytes

    vd_drive_info_t      *cached_drives;
    uint32_t              cached_drive_count;

    vd_file_item_t       *cached_items;
    uint32_t              cached_item_count;

    lv_obj_t             *drive_grid_box;
    lv_obj_t             *browser_box;
    lv_obj_t             *breadcrumb_box;
    lv_obj_t             *file_list_box;

    lv_timer_t           *fill_timer;      /* ตัวทยอยสร้างแถว */
    uint32_t              rendered_rows;

    uint32_t              page;            /* ← เพิ่ม: หน้าปัจจุบัน เริ่มที่ 0 */
    uint32_t              page_start;      /* ← เพิ่ม: index แรกของหน้านี้ */
    uint32_t              page_end;        /* ← เพิ่ม: index สุดท้าย+1 ของหน้านี้ */
    lv_obj_t             *pager_box;       /* ← เพิ่ม: แถบ ◀ 1/20 ▶ */
    lv_obj_t             *pager_label;
    lv_obj_t             *pager_prev;
    lv_obj_t             *pager_next;
} _vd_files_ctx_t;

/* หน้าไฟล์ที่กำลังเปิดอยู่ — ใช้สำหรับ refresh จากข้างนอกโดยไม่สร้างหน้าใหม่ */
static _vd_files_ctx_t *s_active_files_ctx = NULL;

/* โครงสร้าง Context ชั่วคราวสำหรับส่งให้ Action ใน Context Menu */
typedef struct {
    _vd_files_ctx_t *files_ctx;
    vd_file_item_t   item_copy;
    char            *target_full_path;
    uint32_t         action_id;
    lv_obj_t        *overlay_obj;
} _vd_action_click_ctx_t;

/* อัปเดตสไตล์สีของปุ่มตามสถานะ Active / Inactive */
static void _vd_menu_apply_item_style(_vd_menu_node_t *node, bool is_active) {
    if (!node || !node->btn_obj) return;

    if (is_active) {
        lv_obj_set_style_bg_opa(node->btn_obj, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(node->btn_obj, VD_MENU_COLOR_ACTIVE_BG, 0);
        lv_obj_set_style_text_color(node->icon_obj, VD_MENU_COLOR_TEXT_ACTIVE, 0);
        lv_obj_set_style_text_color(node->label_obj, VD_MENU_COLOR_TEXT_ACTIVE, 0);
    } else {
        lv_obj_set_style_bg_opa(node->btn_obj, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(node->icon_obj, VD_MENU_COLOR_TEXT_NORMAL, 0);
        lv_obj_set_style_text_color(node->label_obj, VD_MENU_COLOR_TEXT_NORMAL, 0);
    }
}

/* Event เมื่อแตะปุ่มในรายการ */
static void _vd_menu_item_event_cb(lv_event_t *e) {
    _vd_menu_node_t *node = (_vd_menu_node_t *)lv_event_get_user_data(e);
    vd_menu_sidebar_t *sidebar = (vd_menu_sidebar_t *)lv_obj_get_user_data(lv_event_get_current_target(e));
    if (!node || !sidebar) return;

    vd_menu_sidebar_list_set_active(sidebar, node->id);

    if (sidebar->cb) {
        sidebar->cb(sidebar, node->id, node->title, sidebar->user_data);
    }
}

/* Event เมื่อแตะปุ่มออกจากหน้าตั้งค่า */
static void _vd_menu_exit_event_cb(lv_event_t *e) {
    vd_menu_sidebar_t *sidebar = (vd_menu_sidebar_t *)lv_event_get_user_data(e);
    if (!sidebar) return;

    if (sidebar->cb) {
        sidebar->cb(sidebar, VD_MENU_EXIT_ITEM_ID, "EXIT", sidebar->user_data);
    }
}

/* Event คืนแรมอัตโนมัติเมื่อ Panel ถูกลบ */
static void _vd_menu_panel_delete_cb(lv_event_t *e) {
    vd_menu_sidebar_t *sidebar = (vd_menu_sidebar_t *)lv_event_get_user_data(e);
    if (sidebar) {
        _vd_menu_node_t *curr = sidebar->head;
        while (curr) {
            _vd_menu_node_t *next = curr->next;
            free(curr);
            curr = next;
        }
        free(sidebar);
    }
}

vd_menu_sidebar_t *vd_menu_sidebar_list_create(lv_obj_t *parent, int32_t width_px, int32_t height_px,
                                              vd_menu_cb_t cb, void *user_data) {
    if (!parent) return NULL;

    vd_menu_sidebar_t *sidebar = (vd_menu_sidebar_t *)calloc(1, sizeof(vd_menu_sidebar_t));
    if (!sidebar) return NULL;

    sidebar->cb = cb;
    sidebar->user_data = user_data;

    // 1. คอนเทนเนอร์หลัก (Main Sidebar Panel)
    sidebar->panel = lv_obj_create(parent);
    lv_obj_remove_style_all(sidebar->panel);

    if (width_px > 0) {
        lv_obj_set_width(sidebar->panel, width_px);
    } else {
        lv_obj_set_width(sidebar->panel, lv_pct(VD_MENU_SIDEBAR_DEFAULT_WIDTH_PCT));
    }

    // กำหนดความสูงให้ชัดเจน (ถ้าไม่ระบุให้ใช้เต็มจอ 100%)
    if (height_px > 0) {
        lv_obj_set_height(sidebar->panel, height_px);
    } else {
        lv_obj_set_height(sidebar->panel, lv_pct(100));
    }

    lv_obj_set_style_bg_opa(sidebar->panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sidebar->panel, VD_MENU_COLOR_BG, 0);

    lv_obj_set_style_pad_top(sidebar->panel, VD_MENU_PAD_TOP, 0);
    lv_obj_set_style_pad_left(sidebar->panel, VD_MENU_PAD_LEFT, 0);
    lv_obj_set_style_pad_right(sidebar->panel, VD_MENU_PAD_RIGHT, 0);
    lv_obj_set_style_pad_bottom(sidebar->panel, VD_MENU_PAD_BOTTOM, 0);

    lv_obj_set_style_border_side(sidebar->panel, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(sidebar->panel, 1, 0);
    lv_obj_set_style_border_color(sidebar->panel, VD_MENU_COLOR_BORDER, 0);

    lv_obj_set_flex_flow(sidebar->panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(sidebar->panel, false);

    lv_obj_add_event_cb(sidebar->panel, _vd_menu_panel_delete_cb, LV_EVENT_DELETE, sidebar);

    // 2. โซนเลื่อน (Scroll Area) ด้านบน
    sidebar->scroll_area = lv_obj_create(sidebar->panel);
    lv_obj_remove_style_all(sidebar->scroll_area);
    lv_obj_set_width(sidebar->scroll_area, lv_pct(100));

    // ★ จุดสำคัญ: บังคับความสูงเริ่มต้นเป็น 0 เพื่อให้ flex_grow ยืดกินที่ว่างที่เหลือทั้งหมด
    lv_obj_set_height(sidebar->scroll_area, 0);
    lv_obj_set_flex_grow(sidebar->scroll_area, 1);

    lv_obj_set_flex_flow(sidebar->scroll_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sidebar->scroll_area, VD_MENU_PAD_ROW, 0);
    lv_obj_set_scrollable(sidebar->scroll_area, true);
    lv_obj_set_scrollbar_mode(sidebar->scroll_area, LV_SCROLLBAR_MODE_AUTO);

    // 3. โซนล่างสำหรับปุ่มออก (Bottom Area)
    sidebar->bottom_area = lv_obj_create(sidebar->panel);
    lv_obj_remove_style_all(sidebar->bottom_area);
    lv_obj_set_width(sidebar->bottom_area, lv_pct(100));

    // ★ จุดสำคัญ: ให้สูงเท่าขนาดเนื้อหาข้างในพอดี ไม่ต้องแย่งพื้นที่
    lv_obj_set_height(sidebar->bottom_area, LV_SIZE_CONTENT);

    lv_obj_set_flex_flow(sidebar->bottom_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(sidebar->bottom_area, false);
    lv_obj_set_style_pad_top(sidebar->bottom_area, 4, 0);

    return sidebar;
}

lv_obj_t *vd_menu_sidebar_list_add_item(vd_menu_sidebar_t *sidebar, uint32_t id,
                                       const char *icon, const char *title, bool is_active) {
    if (!sidebar || !sidebar->scroll_area) return NULL;

    _vd_menu_node_t *node = (_vd_menu_node_t *)calloc(1, sizeof(_vd_menu_node_t));
    if (!node) return NULL;

    node->id = id;
    if (title) strncpy(node->title, title, sizeof(node->title) - 1);

    // สร้างปุ่ม
    node->btn_obj = lv_button_create(sidebar->scroll_area);
    lv_obj_set_width(node->btn_obj, lv_pct(100));
    lv_obj_set_height(node->btn_obj, VD_MENU_ITEM_HEIGHT);
    lv_obj_set_flex_flow(node->btn_obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(node->btn_obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_set_style_pad_hor(node->btn_obj, 10, 0);
    lv_obj_set_style_pad_ver(node->btn_obj, 0, 0);
    lv_obj_set_style_pad_column(node->btn_obj, 12, 0);
    lv_obj_set_style_radius(node->btn_obj, 12, 0);
    lv_obj_set_style_shadow_width(node->btn_obj, 0, 0);

    // สร้างไอคอน (ล็อคความกว้าง 24 px และจัดกึ่งกลาง)
    node->icon_obj = lv_label_create(node->btn_obj);
    lv_obj_set_width(node->icon_obj, VD_MENU_ICON_WIDTH);
    lv_obj_set_style_text_align(node->icon_obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(node->icon_obj, &font_awesome_20, 0);
    lv_label_set_text(node->icon_obj, icon ? icon : "");

    // สร้างข้อความ
    node->label_obj = lv_label_create(node->btn_obj);
    lv_obj_set_style_text_font(node->label_obj, &anuphan_med_16, 0);
    lv_label_set_text(node->label_obj, title ? title : "");

    // ผูก Event และ Data
    lv_obj_set_user_data(node->btn_obj, sidebar);
    lv_obj_add_event_cb(node->btn_obj, _vd_menu_item_event_cb, LV_EVENT_CLICKED, node);

    // นำเข้า Linked List
    node->next = sidebar->head;
    sidebar->head = node;

    if (is_active) {
        sidebar->active_id = id;
    }
    _vd_menu_apply_item_style(node, is_active);

    return node->btn_obj;
}

void vd_menu_sidebar_list_add_divider(vd_menu_sidebar_t *sidebar) {
    if (!sidebar || !sidebar->scroll_area) return;

    lv_obj_t *div = lv_obj_create(sidebar->scroll_area);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, lv_pct(100), 1);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(div, VD_MENU_COLOR_DIVIDER, 0);
    lv_obj_set_style_margin_ver(div, 6, 0);
}

void vd_menu_sidebar_list_set_active(vd_menu_sidebar_t *sidebar, uint32_t id) {
    if (!sidebar) return;
    sidebar->active_id = id;

    _vd_menu_node_t *curr = sidebar->head;
    while (curr) {
        _vd_menu_apply_item_style(curr, (curr->id == id));
        curr = curr->next;
    }
}

uint32_t vd_menu_sidebar_list_get_active(const vd_menu_sidebar_t *sidebar) {
    return sidebar ? sidebar->active_id : 0;
}

void vd_menu_sidebar_list_set_exit_btn(vd_menu_sidebar_t *sidebar, const char *icon, const char *title) {
    if (!sidebar || !sidebar->bottom_area) return;

    // เติมเส้นคั่นก่อนถึงปุ่มออกด้านล่าง
    lv_obj_t *div = lv_obj_create(sidebar->bottom_area);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, lv_pct(100), 1);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(div, VD_MENU_COLOR_DIVIDER, 0);
    lv_obj_set_style_margin_bottom(div, 6, 0);

    // สร้างปุ่มออก
    sidebar->exit_btn = lv_button_create(sidebar->bottom_area);
    lv_obj_set_width(sidebar->exit_btn, lv_pct(100));
    lv_obj_set_height(sidebar->exit_btn, VD_MENU_ITEM_HEIGHT);
    lv_obj_set_flex_flow(sidebar->exit_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sidebar->exit_btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_set_style_pad_hor(sidebar->exit_btn, 10, 0);
    lv_obj_set_style_pad_ver(sidebar->exit_btn, 0, 0);
    lv_obj_set_style_pad_column(sidebar->exit_btn, 12, 0);
    lv_obj_set_style_radius(sidebar->exit_btn, 6, 0);
    lv_obj_set_style_bg_opa(sidebar->exit_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(sidebar->exit_btn, 0, 0);

    // ไอคอนปุ่มออก
    lv_obj_t *icon_lbl = lv_label_create(sidebar->exit_btn);
    lv_obj_set_width(icon_lbl, VD_MENU_ICON_WIDTH);
    lv_obj_set_style_text_align(icon_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(icon_lbl, &font_awesome_20, 0);
    lv_obj_set_style_text_color(icon_lbl, VD_MENU_COLOR_EXIT_TEXT, 0);
    lv_label_set_text(icon_lbl, icon ? icon : "\uf2f5"); // รหัสไอคอน Sign-out

    // ข้อความปุ่มออก
    lv_obj_t *txt_lbl = lv_label_create(sidebar->exit_btn);
    lv_obj_set_style_text_font(txt_lbl, &anuphan_semi_bold_16, 0);
    lv_obj_set_style_text_color(txt_lbl, VD_MENU_COLOR_EXIT_TEXT, 0);
    lv_label_set_text(txt_lbl, title ? title : "ออกจากหน้าตั้งค่า");

    lv_obj_add_event_cb(sidebar->exit_btn, _vd_menu_exit_event_cb, LV_EVENT_CLICKED, sidebar);
}

void vd_menu_sidebar_list_delete(vd_menu_sidebar_t *sidebar) {
    if (!sidebar) return;
    if (sidebar->panel) {
        lv_obj_delete(sidebar->panel); // จะไปทริกเกอร์ _vd_menu_panel_delete_cb คืนแรมอัตโนมัติ
    }
}

/* ════════════════════════════════════════════════════════════
   2. จัดการ Content Area และ Sub-frame ฝั่งขวา
   ════════════════════════════════════════════════════════════ */

static void _vd_menu_content_delete_cb(lv_event_t *e) {
    vd_menu_content_t *content = (vd_menu_content_t *)lv_event_get_user_data(e);
    if (content) free(content);
}

static void _vd_menu_sub_frame_delete_cb(lv_event_t *e) {
    vd_menu_sub_frame_t *sub_frame = (vd_menu_sub_frame_t *)lv_event_get_user_data(e);
    if (sub_frame) free(sub_frame);
}

vd_menu_content_t *vd_menu_content_create(lv_obj_t *parent) {
    if (!parent) return NULL;

    vd_menu_content_t *content = (vd_menu_content_t *)calloc(1, sizeof(vd_menu_content_t));
    if (!content) return NULL;

    // กรอบเส้นประสีแดง: Main Content Area
    content->container = lv_obj_create(parent);
    lv_obj_remove_style_all(content->container);

    lv_obj_set_flex_grow(content->container, 1);
    lv_obj_set_height(content->container, lv_pct(100));

    lv_obj_set_style_bg_opa(content->container, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(content->container, VD_MENU_COLOR_CONTENT_BG, 0);

    // Padding โปร่งโล่ง สบายตา
    lv_obj_set_style_pad_top(content->container, 24, 0);
    lv_obj_set_style_pad_bottom(content->container, 28, 0);
    lv_obj_set_style_pad_left(content->container, 32, 0);
    lv_obj_set_style_pad_right(content->container, 32, 0);
    lv_obj_set_style_pad_row(content->container, 18, 0);

    lv_obj_set_flex_flow(content->container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(content->container, true);
    lv_obj_set_scrollbar_mode(content->container, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_add_event_cb(content->container, _vd_menu_content_delete_cb, LV_EVENT_DELETE, content);

    return content;
}

lv_obj_t *vd_menu_content_add_title(vd_menu_content_t *content, const char *title_text) {
    if (!content || !content->container || !title_text) return NULL;

    lv_obj_t *title = lv_label_create(content->container);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_font(title, &anuphan_bold_22, 0);
    lv_obj_set_style_text_color(title, VD_MENU_COLOR_TITLE, 0);
    // เผื่อ Padding ป้องกันสระบน-ล่างโดนเฉือน
    lv_obj_set_style_pad_ver(title, 4, 0);
    lv_obj_set_style_pad_bottom(title, 2, 0);

    return title;
}

vd_menu_sub_frame_t *vd_menu_sub_frame_create(vd_menu_content_t *content, const char *frame_title) {
    if (!content || !content->container) return NULL;

    vd_menu_sub_frame_t *sub_frame = (vd_menu_sub_frame_t *)calloc(1, sizeof(vd_menu_sub_frame_t));
    if (!sub_frame) return NULL;

    // กรอบสีเขียว: sub_frame Card
    sub_frame->card = lv_obj_create(content->container);
    lv_obj_remove_style_all(sub_frame->card);

    lv_obj_set_width(sub_frame->card, lv_pct(100));
    lv_obj_set_height(sub_frame->card, LV_SIZE_CONTENT);

    lv_obj_set_style_bg_opa(sub_frame->card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sub_frame->card, VD_MENU_COLOR_CARD_BG, 0);
    lv_obj_set_style_border_width(sub_frame->card, 1, 0);
    lv_obj_set_style_border_color(sub_frame->card, VD_MENU_COLOR_CARD_BORDER, 0);
    lv_obj_set_style_radius(sub_frame->card, 14, 0);
    lv_obj_set_style_pad_all(sub_frame->card, 18, 0);

    lv_obj_set_flex_flow(sub_frame->card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(sub_frame->card, false);

    lv_obj_add_event_cb(sub_frame->card, _vd_menu_sub_frame_delete_cb, LV_EVENT_DELETE, sub_frame);

    // frame_title: "ข้อมูลเครื่อง"
    if (frame_title) {
        lv_obj_t *f_title = lv_label_create(sub_frame->card);
        lv_label_set_text(f_title, frame_title);
        lv_obj_set_style_text_font(f_title, &anuphan_bold_16, 0);
        lv_obj_set_style_text_color(f_title, VD_MENU_COLOR_TITLE, 0);
        lv_obj_set_style_pad_bottom(f_title, 10, 0);
    }

    return sub_frame;
}

lv_obj_t *vd_menu_sub_content_create(vd_menu_sub_frame_t *sub_frame,
                                     const char *content_title,
                                     const char *content_subtitle) {
    if (!sub_frame || !sub_frame->card) return NULL;

    // เส้นคั่นระหว่างแถว
    if (sub_frame->item_count > 0) {
        lv_obj_t *div = lv_obj_create(sub_frame->card);
        lv_obj_remove_style_all(div);
        lv_obj_set_size(div, lv_pct(100), 1);
        lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(div, VD_MENU_COLOR_ROW_DIVIDER, 0);
        lv_obj_set_style_margin_ver(div, 12, 0); // เว้นระยะเส้นคั่นบน-ล่าง 12px ให้โปร่งขึ้น
    }
    sub_frame->item_count++;

    // แถวแนวนอน (sub_content)
    lv_obj_t *row = lv_obj_create(sub_frame->card);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(row, 2, 0); // ช่องไฟบนล่างในแถว

    // ฝั่งซ้าย: กล่องข้อความ
    lv_obj_t *left_box = lv_obj_create(row);
    lv_obj_remove_style_all(left_box);
    lv_obj_set_width(left_box, LV_SIZE_CONTENT);
    lv_obj_set_height(left_box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left_box, 4, 0); // ขยับช่องไฟระหว่าง Title กับ Subtitle เป็น 4px

    // 1. Content Title: เปลี่ยนเป็น Medium 14px เพื่อลดความหนาเตอะ
    lv_obj_t *t_lbl = lv_label_create(left_box);
    lv_label_set_text(t_lbl, content_title ? content_title : "");
    lv_obj_set_style_text_font(t_lbl, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(t_lbl, VD_MENU_COLOR_TITLE, 0);
    // เพิ่ม Padding แนวตั้ง 2px ป้องกันการขริบสระบน/วรรณยุกต์
    lv_obj_set_style_pad_ver(t_lbl, 2, 0);

    // 2. Content Subtitle: เปลี่ยนเป็น Regular 12px สีเทาโปร่ง
    if (content_subtitle && strlen(content_subtitle) > 0) {
        lv_obj_t *sub_lbl = lv_label_create(left_box);
        lv_label_set_text(sub_lbl, content_subtitle);
        lv_obj_set_style_text_font(sub_lbl, &anuphan_12, 0);
        lv_obj_set_style_text_color(sub_lbl, VD_MENU_COLOR_SUBTITLE, 0);
        // เพิ่ม line_space และ pad_ver ป้องกันสระขาด
        lv_obj_set_style_text_line_space(sub_lbl, 3, 0);
        lv_obj_set_style_pad_ver(sub_lbl, 2, 0);
    }

    // ฝั่งขวา: คอนเทนเนอร์ Slot สำหรับวิดเจ็ต
    lv_obj_t *right_slot = lv_obj_create(row);
    lv_obj_remove_style_all(right_slot);
    lv_obj_set_width(right_slot, LV_SIZE_CONTENT);
    lv_obj_set_height(right_slot, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right_slot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_slot, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_slot, 8, 0);

    return right_slot;
}

/* ════════════════════════════════════════════════════════════
   3. Convenience Helpers
   ════════════════════════════════════════════════════════════ */

lv_obj_t *vd_menu_sub_content_add_text_input(vd_menu_sub_frame_t *sub_frame,
                                            const char *title,
                                            const char *subtitle,
                                            const char *default_text,
                                            uint32_t max_len) {
    lv_obj_t *slot = vd_menu_sub_content_create(sub_frame, title, subtitle);
    if (!slot) return NULL;

    lv_obj_t *ta = lv_textarea_create(slot);
    lv_obj_set_size(ta, 240, 36);
    lv_textarea_set_one_line(ta, true);
    if (default_text) lv_textarea_set_text(ta, default_text);
    if (max_len > 0) lv_textarea_set_max_length(ta, max_len);

    // ปรับสไตล์ช่องกรอกให้ดูเบาและคมชัด
    lv_obj_set_style_bg_color(ta, lv_color_white(), 0);
    lv_obj_set_style_border_color(ta, VD_MENU_COLOR_CARD_BORDER, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 8, 0);
    lv_obj_set_style_pad_hor(ta, 12, 0);
    lv_obj_set_style_pad_ver(ta, 12, 0); // ขยายพื้นที่แนวตั้งไม่ให้สระในช่องกรอกชนขอบ
    lv_obj_set_style_text_font(ta, &anuphan_14, 0);
    lv_obj_set_style_text_color(ta, VD_MENU_COLOR_TITLE, 0);

    return ta;
}

lv_obj_t *vd_menu_sub_content_add_static_badge(vd_menu_sub_frame_t *sub_frame,
                                              const char *title,
                                              const char *subtitle,
                                              const char *badge_text) {
    lv_obj_t *slot = vd_menu_sub_content_create(sub_frame, title, subtitle);
    if (!slot) return NULL;

    lv_obj_t *badge = lv_obj_create(slot);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, VD_MENU_COLOR_BADGE_BG, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_color(badge, VD_MENU_COLOR_CARD_BORDER, 0);
    lv_obj_set_style_radius(badge, 6, 0);
    lv_obj_set_style_pad_hor(badge, 12, 0);
    lv_obj_set_style_pad_ver(badge, 8, 0);

    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(badge);
    lv_label_set_text(lbl, badge_text ? badge_text : "");
    lv_obj_set_style_text_font(lbl, &anuphan_14, 0); // ใช้ Regular 14 แทน SemiBold
    lv_obj_set_style_text_color(lbl, VD_MENU_COLOR_TITLE, 0);
    lv_obj_set_style_pad_ver(lbl, 2, 0);

    return badge;
}

lv_obj_t *vd_menu_sub_content_add_switch(vd_menu_sub_frame_t *sub_frame,
                                        const char *title,
                                        const char *subtitle,
                                        bool default_checked,
                                        lv_event_cb_t event_cb,
                                        void *user_data) {
    lv_obj_t *slot = vd_menu_sub_content_create(sub_frame, title, subtitle);
    if (!slot) return NULL;

    lv_obj_t *sw = lv_switch_create(slot);
    lv_obj_set_size(sw, 44, 24);

    // สไตล์สีเขียวเมื่อเปิดสวิตช์
    lv_obj_set_style_bg_color(sw, VD_MENU_COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);

    if (default_checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    else lv_obj_remove_state(sw, LV_STATE_CHECKED);

    if (event_cb) lv_obj_add_event_cb(sw, event_cb, LV_EVENT_VALUE_CHANGED, user_data);

    return sw;
}

/* Event อัปเดตตัวเลขเปอร์เซ็นต์ของ Slider อัตโนมัติ */
static void _vd_menu_slider_event_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *val_lbl = (lv_obj_t *)lv_event_get_user_data(e);
    const char *unit = (const char *)lv_obj_get_user_data(slider);

    if (val_lbl) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%s", (int)lv_slider_get_value(slider), unit ? unit : "");
        lv_label_set_text(val_lbl, buf);
    }
}

lv_obj_t *vd_menu_sub_content_add_slider(vd_menu_sub_frame_t *sub_frame,
                                        const char *icon,
                                        const char *title,
                                        const char *subtitle,
                                        int32_t min,
                                        int32_t max,
                                        int32_t default_val,
                                        const char *unit,
                                        lv_event_cb_t event_cb) {
    if (!sub_frame || !sub_frame->card) return NULL;

    // เติมเส้นคั่น (Divider) ระหว่างรายการใน sub_frame
    if (sub_frame->item_count > 0) {
        lv_obj_t *div = lv_obj_create(sub_frame->card);
        lv_obj_remove_style_all(div);
        lv_obj_set_size(div, lv_pct(100), 1);
        lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(div, VD_MENU_COLOR_ROW_DIVIDER, 0);
        lv_obj_set_style_margin_ver(div, 12, 0);
    }
    sub_frame->item_count++;

    // 1. คอนเทนเนอร์หลักของ Slider Block (กว้างเต็ม 100% จัดเรียงแนวตั้ง)
    lv_obj_t *box = lv_obj_create(sub_frame->card);
    lv_obj_remove_style_all(box);
    lv_obj_set_width(box, lv_pct(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 8, 0); // ระยะช่องไฟระหว่างชั้น 8px
    lv_obj_set_ext_draw_size(box, 15);
    lv_obj_set_overflow_visible(box, true);

    // 2. ชั้นบน (Header Row): ชื่อรายการฝั่งซ้าย และตัวเลขค่าฝั่งขวา
    lv_obj_t *header_row = lv_obj_create(box);
    lv_obj_remove_style_all(header_row);
    lv_obj_set_width(header_row, lv_pct(100));
    lv_obj_set_height(header_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 2.1 ฝั่งซ้าย: ไอคอน (ถ้ามี) + ข้อความชื่อรายการ
    lv_obj_t *left_group = lv_obj_create(header_row);
    lv_obj_remove_style_all(left_group);
    lv_obj_set_width(left_group, LV_SIZE_CONTENT);
    lv_obj_set_height(left_group, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_group, 8, 0);

    if (icon && strlen(icon) > 0) {
        lv_obj_t *ic_lbl = lv_label_create(left_group);
        lv_label_set_text(ic_lbl, icon);
        lv_obj_set_style_text_font(ic_lbl, &font_awesome_20, 0);
        lv_obj_set_style_text_color(ic_lbl, VD_MENU_COLOR_SUBTITLE, 0);
    }

    lv_obj_t *t_lbl = lv_label_create(left_group);
    lv_label_set_text(t_lbl, title ? title : "");
    lv_obj_set_style_text_font(t_lbl, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(t_lbl, VD_MENU_COLOR_TITLE, 0);
    lv_obj_set_style_pad_ver(t_lbl, 2, 0);

    // 2.2 ฝั่งขวา: ป้ายแสดงค่าตัวเลข (สีเขียวเน้น คมชัด)
    lv_obj_t *val_lbl = lv_label_create(header_row);
    lv_obj_set_style_text_font(val_lbl, &anuphan_bold_16, 0);
    lv_obj_set_style_text_color(val_lbl, VD_MENU_COLOR_ACCENT, 0);
    lv_obj_set_style_pad_ver(val_lbl, 2, 0);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%s", (int)default_val, unit ? unit : "");
    lv_label_set_text(val_lbl, buf);

    // 3. ชั้นกลาง (Slider Bar): ขยายเต็มความกว้าง 100%
    lv_obj_t *slider = lv_slider_create(box);
    lv_obj_set_width(slider, lv_pct(100)); // ★ กว้างเต็มแถว
    lv_obj_set_height(slider, 6);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, default_val, LV_ANIM_OFF);

    // สไตล์รางและปุ่มลาก
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xE5E7EB), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, VD_MENU_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, VD_MENU_COLOR_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 5, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, 15);

    // 4. ชั้นล่าง (Hint / Subtitle): แสดงคำอธิบายสีเทา 12px ด้านล่าง Slider
    if (subtitle && strlen(subtitle) > 0) {
        lv_obj_t *hint_lbl = lv_label_create(box);
        lv_label_set_text(hint_lbl, subtitle);
        lv_obj_set_style_text_font(hint_lbl, &anuphan_12, 0);
        lv_obj_set_style_text_color(hint_lbl, VD_MENU_COLOR_SUBTITLE, 0);
        lv_obj_set_style_text_line_space(hint_lbl, 3, 0);
        lv_obj_set_style_pad_ver(hint_lbl, 2, 0);
    }

    // ผูก Event
    lv_obj_set_user_data(slider, (void *)unit);
    lv_obj_add_event_cb(slider, _vd_menu_slider_event_cb, LV_EVENT_VALUE_CHANGED, val_lbl);

    if (event_cb) {
        lv_obj_add_event_cb(slider, event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }

    return slider;
}

/* Event เมื่อกดสลับปุ่มในกลุ่ม */
static void _vd_menu_btn_group_item_event_cb(lv_event_t *e) {
    lv_obj_t *clicked_btn = lv_event_get_target(e);
    _vd_menu_btn_group_ctx_t *ctx = (_vd_menu_btn_group_ctx_t *)lv_event_get_user_data(e);
    if (!ctx || !ctx->group_box) return;

    // หา index ของปุ่มที่ถูกกดจากตำแหน่งลูกในกลุ่ม
    uint32_t clicked_idx = 0;
    uint32_t cnt = lv_obj_get_child_count(ctx->group_box);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(ctx->group_box, i);
        if (child == clicked_btn) {
            clicked_idx = i;
            break;
        }
    }

    ctx->selected_index = clicked_idx;

    // วนลูปปรับสไตล์: ปุ่มที่เลือกจะเป็นสีขาวทึบ ปุ่มอื่นโปร่งใส
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(ctx->group_box, i);
        lv_obj_t *lbl = lv_obj_get_child(child, 0);
        if (i == clicked_idx) {
            lv_obj_set_style_bg_opa(child, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(child, lv_color_white(), 0);
            lv_obj_set_style_shadow_width(child, 2, 0);
            lv_obj_set_style_shadow_opa(child, LV_OPA_20, 0);
            lv_obj_set_style_text_color(lbl, VD_MENU_COLOR_TITLE, 0);
        } else {
            lv_obj_set_style_bg_opa(child, LV_OPA_TRANSP, 0);
            lv_obj_set_style_shadow_width(child, 0, 0);
            lv_obj_set_style_text_color(lbl, VD_MENU_COLOR_SUBTITLE, 0);
        }
    }

    // เรียกใช้งาน User Callback (ถ้ามี) ส่ง Index ที่เลือกกลับไป
    if (ctx->user_cb) {
        // สามารถดึงค่าผ่าน event หรือส่ง user_data ได้ตามต้องการ
        ctx->user_cb(e);
    }
}

static void _vd_menu_btn_group_delete_cb(lv_event_t *e) {
    void *ctx = lv_event_get_user_data(e);
    if (ctx) free(ctx);
}

lv_obj_t *vd_menu_sub_content_add_button_group(vd_menu_sub_frame_t *sub_frame,
                                              const char *title,
                                              const char *subtitle,
                                              const char *choices[],
                                              uint32_t choice_count,
                                              uint32_t default_index,
                                              lv_event_cb_t event_cb,
                                              void *user_data) {
    if (!sub_frame || !sub_frame->card || !choices || choice_count == 0) return NULL;

    // สร้างแถวมาตรฐานฝั่งซ้าย (Title + Subtitle)
    lv_obj_t *slot = vd_menu_sub_content_create(sub_frame, title, subtitle);
    if (!slot) return NULL;

    // สร้างกล่องคอนเทนเนอร์หุ้มกลุ่มปุ่ม (พื้นหลังสีเทาอ่อน โค้งมน สไตล์ Segmented Control)
    lv_obj_t *group_box = lv_obj_create(slot);
    lv_obj_remove_style_all(group_box);
    lv_obj_set_size(group_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(group_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(group_box, VD_MENU_COLOR_CARD_BORDER, 0);
    lv_obj_set_style_radius(group_box, 8, 0);
    lv_obj_set_style_pad_all(group_box, 3, 0);
    lv_obj_set_flex_flow(group_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(group_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(group_box, 2, 0);

    // จัดการ Context สำหรับเก็บสถานะ
    _vd_menu_btn_group_ctx_t *ctx = (_vd_menu_btn_group_ctx_t *)malloc(sizeof(_vd_menu_btn_group_ctx_t));
    if (!ctx) return NULL;

    ctx->group_box      = group_box;
    ctx->user_cb        = event_cb;
    ctx->user_data      = user_data;
    ctx->selected_index = (default_index < choice_count) ? default_index : 0;
    ctx->total_choices  = choice_count;

    // ผูกการ free ไว้กับ group_box เมื่อ Container โดนทำลาย
    lv_obj_add_event_cb(group_box, _vd_menu_btn_group_delete_cb, LV_EVENT_DELETE, ctx);

    // วนลูปสร้างปุ่มตามจำนวน choices ที่ส่งเข้ามา
    for (uint32_t i = 0; i < choice_count; i++) {
        lv_obj_t *btn = lv_button_create(group_box);
        lv_obj_remove_style_all(btn);
        lv_obj_set_height(btn, 32);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_hor(btn, 18, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, choices[i] ? choices[i] : "");
        lv_obj_set_style_text_font(lbl, &anuphan_med_14, 0);
        lv_obj_center(lbl);

        // กำหนดสถานะ Active หรือ Inactive เริ่มต้น
        if (i == ctx->selected_index) {
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
            lv_obj_set_style_shadow_width(btn, 2, 0);
            lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
            lv_obj_set_style_text_color(lbl, VD_MENU_COLOR_TITLE, 0);
        } else {
            lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(lbl, VD_MENU_COLOR_SUBTITLE, 0);
        }

        // ผูก Event เมื่อกดปุ่ม
        lv_obj_add_event_cb(btn, _vd_menu_btn_group_item_event_cb, LV_EVENT_CLICKED, ctx);
    }

    return group_box;
}

/* ฟังก์ชันสร้าง String ตัวเลือก Roller ตามค่า Step แบบไดนามิก */
static void _build_roller_options(char *buf, size_t buf_size, uint16_t max_val, uint16_t step) {
    if (step == 0) step = 1;
    buf[0] = '\0';
    size_t offset = 0;

    for (uint16_t v = 0; v <= max_val; v += step) {
        int written = snprintf(buf + offset, buf_size - offset,
                               (v + step <= max_val) ? "%02u\n" : "%02u", v);
        if (written < 0 || (size_t)written >= buf_size - offset) break;
        offset += written;
    }
}

// 1. เพิ่ม Callback คืนแรมเมื่อ Backdrop ของ Time Modal ถูกทำลาย
static void _vd_menu_time_modal_delete_cb(lv_event_t *e) {
    void *m_ctx = lv_event_get_user_data(e);
    if (m_ctx) free(m_ctx);
}

// 2. ปรับฟังก์ชัน Close และ Confirm ให้ใช้ delete_async และไม่ต้อง free เอง
static void _vd_menu_time_modal_close_cb(lv_event_t *e) {
    _vd_menu_time_modal_ctx_t *m_ctx = (_vd_menu_time_modal_ctx_t *)lv_event_get_user_data(e);
    if (!m_ctx) return;
    if (m_ctx->modal_backdrop) {
        lv_obj_delete_async(m_ctx->modal_backdrop);
    }
}

/* Event กดยืนยันเวลาจาก Modal */
static void _vd_menu_time_modal_confirm_cb(lv_event_t *e) {
    _vd_menu_time_modal_ctx_t *m_ctx = (_vd_menu_time_modal_ctx_t *)lv_event_get_user_data(e);
    if (!m_ctx || !m_ctx->range_ctx) return;

    _vd_menu_time_range_ctx_t *r_ctx = m_ctx->range_ctx;

    uint32_t hour = 0, min = 0, sec = 0;
    if (r_ctx->show_hour && m_ctx->roller_hour) {
        hour = lv_roller_get_selected(m_ctx->roller_hour) * r_ctx->hour_step;
    }
    if (r_ctx->show_min && m_ctx->roller_min) {
        min = lv_roller_get_selected(m_ctx->roller_min) * r_ctx->min_step;
    }
    if (r_ctx->show_sec && m_ctx->roller_sec) {
        sec = lv_roller_get_selected(m_ctx->roller_sec) * r_ctx->sec_step;
    }

    // จัดฟอร์แมตข้อความตามหน่วยที่เปิดใช้งาน
    char buf[32] = {0};
    if (r_ctx->show_hour && r_ctx->show_min && r_ctx->show_sec) {
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)hour, (unsigned)min, (unsigned)sec);
    } else if (r_ctx->show_hour && r_ctx->show_min) {
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)hour, (unsigned)min);
    } else if (r_ctx->show_min && r_ctx->show_sec) {
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)min, (unsigned)sec);
    } else if (r_ctx->show_hour) {
        snprintf(buf, sizeof(buf), "%02u น.", (unsigned)hour);
    } else if (r_ctx->show_min) {
        snprintf(buf, sizeof(buf), "%02u นาที", (unsigned)min);
    } else if (r_ctx->show_sec) {
        snprintf(buf, sizeof(buf), "%02u วินาที", (unsigned)sec);
    }

    if (m_ctx->target_label) {
        lv_label_set_text(m_ctx->target_label, buf);
    }

    if (r_ctx->user_cb) {
        r_ctx->user_cb(e);
    }

    if (m_ctx->modal_backdrop) {
        lv_obj_delete_async(m_ctx->modal_backdrop);
    }
}

/* Event เมื่อแตะกล่องเวลาเพื่อเปิด Modal */
static void _vd_menu_time_box_click_cb(lv_event_t *e) {
    lv_obj_t *box = lv_event_get_target(e);
    _vd_menu_time_range_ctx_t *r_ctx = (_vd_menu_time_range_ctx_t *)lv_event_get_user_data(e);
    if (!r_ctx) return;

    // ตรวจสอบว่าเป็นกล่องเริ่มหรือสิ้นสุด
    lv_obj_t *target_lbl = (box == r_ctx->start_box) ? r_ctx->start_lbl : r_ctx->end_lbl;
    if (!target_lbl) return;

    int cur_h = 0, cur_m = 0, cur_s = 0;
    const char *txt = lv_label_get_text(target_lbl);
    if (txt) {
        if (r_ctx->show_hour && r_ctx->show_min && r_ctx->show_sec) {
            sscanf(txt, "%d:%d:%d", &cur_h, &cur_m, &cur_s);
        } else if (r_ctx->show_hour && r_ctx->show_min) {
            sscanf(txt, "%d:%d", &cur_h, &cur_m);
        } else if (r_ctx->show_min && r_ctx->show_sec) {
            sscanf(txt, "%d:%d", &cur_m, &cur_s);
        } else if (r_ctx->show_hour) {
            sscanf(txt, "%d", &cur_h);
        } else if (r_ctx->show_min) {
            sscanf(txt, "%d", &cur_m);
        } else if (r_ctx->show_sec) {
            sscanf(txt, "%d", &cur_s);
        }
    }

    _vd_menu_time_modal_ctx_t *m_ctx = (_vd_menu_time_modal_ctx_t *)calloc(1, sizeof(_vd_menu_time_modal_ctx_t));
    if (!m_ctx) return;
    m_ctx->range_ctx    = r_ctx;
    m_ctx->target_label = target_lbl;

    // คำนวณความกว้าง Modal ตามจำนวนหน่วย
    int unit_count = (r_ctx->show_hour ? 1 : 0) + (r_ctx->show_min ? 1 : 0) + (r_ctx->show_sec ? 1 : 0);
    int32_t dialog_w = 140 + (unit_count * 75);

    // 1. Backdrop
    m_ctx->modal_backdrop = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(m_ctx->modal_backdrop);
    lv_obj_set_size(m_ctx->modal_backdrop, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(m_ctx->modal_backdrop, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(m_ctx->modal_backdrop, lv_color_black(), 0);
    lv_obj_add_event_cb(m_ctx->modal_backdrop, _vd_menu_time_modal_close_cb, LV_EVENT_CLICKED, m_ctx);
    lv_obj_add_event_cb(m_ctx->modal_backdrop, _vd_menu_time_modal_delete_cb, LV_EVENT_DELETE, m_ctx);

    // 2. Dialog Box
    lv_obj_t *dialog = lv_obj_create(m_ctx->modal_backdrop);
    lv_obj_remove_style_all(dialog);
    lv_obj_set_size(dialog, dialog_w, 240);
    lv_obj_center(dialog);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dialog, lv_color_white(), 0);
    lv_obj_set_style_radius(dialog, 12, 0);
    lv_obj_set_style_pad_all(dialog, 16, 0);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dialog, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(dialog, LV_OBJ_FLAG_CLICKABLE);

    // Title
    lv_obj_t *t_lbl = lv_label_create(dialog);
    lv_label_set_text(t_lbl, "ตั้งเวลา");
    lv_obj_set_style_text_font(t_lbl, &anuphan_bold_16, 0);
    lv_obj_set_style_text_color(t_lbl, VD_MENU_COLOR_TITLE, 0);

    // Roller Container
    lv_obj_t *roller_row = lv_obj_create(dialog);
    lv_obj_remove_style_all(roller_row);
    lv_obj_set_size(roller_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(roller_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(roller_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(roller_row, 8, 0);

    char opt_buf[300];

    // สร้าง Roller ชั่วโมง (ถ้าเปิดใช้งาน)
    if (r_ctx->show_hour) {
        _build_roller_options(opt_buf, sizeof(opt_buf), 23, r_ctx->hour_step);
        m_ctx->roller_hour = lv_roller_create(roller_row);
        lv_roller_set_options(m_ctx->roller_hour, opt_buf, LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(m_ctx->roller_hour, 3);
        lv_obj_set_width(m_ctx->roller_hour, 65);
        uint32_t sel = cur_h / (r_ctx->hour_step ? r_ctx->hour_step : 1);
        lv_roller_set_selected(m_ctx->roller_hour, sel, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(m_ctx->roller_hour, VD_MENU_COLOR_ACCENT, LV_PART_SELECTED);
    }

    // จุดคั่น ":"
    if (r_ctx->show_hour && r_ctx->show_min) {
        lv_obj_t *c = lv_label_create(roller_row);
        lv_label_set_text(c, ":");
        lv_obj_set_style_text_font(c, &anuphan_bold_18, 0);
    }

    // สร้าง Roller นาที (ถ้าเปิดใช้งาน)
    if (r_ctx->show_min) {
        _build_roller_options(opt_buf, sizeof(opt_buf), 59, r_ctx->min_step);
        m_ctx->roller_min = lv_roller_create(roller_row);
        lv_roller_set_options(m_ctx->roller_min, opt_buf, LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(m_ctx->roller_min, 3);
        lv_obj_set_width(m_ctx->roller_min, 65);
        uint32_t sel = cur_m / (r_ctx->min_step ? r_ctx->min_step : 1);
        lv_roller_set_selected(m_ctx->roller_min, sel, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(m_ctx->roller_min, VD_MENU_COLOR_ACCENT, LV_PART_SELECTED);
    }

    // จุดคั่น ":"
    if (r_ctx->show_min && r_ctx->show_sec) {
        lv_obj_t *c = lv_label_create(roller_row);
        lv_label_set_text(c, ":");
        lv_obj_set_style_text_font(c, &anuphan_bold_18, 0);
    }

    // สร้าง Roller วินาที (ถ้าเปิดใช้งาน)
    if (r_ctx->show_sec) {
        _build_roller_options(opt_buf, sizeof(opt_buf), 59, r_ctx->sec_step);
        m_ctx->roller_sec = lv_roller_create(roller_row);
        lv_roller_set_options(m_ctx->roller_sec, opt_buf, LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(m_ctx->roller_sec, 3);
        lv_obj_set_width(m_ctx->roller_sec, 65);
        uint32_t sel = cur_s / (r_ctx->sec_step ? r_ctx->sec_step : 1);
        lv_roller_set_selected(m_ctx->roller_sec, sel, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(m_ctx->roller_sec, VD_MENU_COLOR_ACCENT, LV_PART_SELECTED);
    }

    // ปุ่ม Cancel / OK
    lv_obj_t *btn_row = lv_obj_create(dialog);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, lv_pct(100), 34);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btn_cancel = lv_button_create(btn_row);
    lv_obj_set_size(btn_cancel, (dialog_w / 2) - 24, 32);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0xF1F5F9), 0);
    lv_obj_set_style_radius(btn_cancel, 6, 0);
    lv_obj_t *clbl = lv_label_create(btn_cancel);
    lv_label_set_text(clbl, "ยกเลิก");
    lv_obj_set_style_text_font(clbl, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(clbl, VD_MENU_COLOR_TEXT_NORMAL, 0);
    lv_obj_center(clbl);
    lv_obj_add_event_cb(btn_cancel, _vd_menu_time_modal_close_cb, LV_EVENT_CLICKED, m_ctx);

    lv_obj_t *btn_ok = lv_button_create(btn_row);
    lv_obj_set_size(btn_ok, (dialog_w / 2) - 24, 32);
    lv_obj_set_style_bg_color(btn_ok, VD_MENU_COLOR_ACCENT, 0);
    lv_obj_set_style_radius(btn_ok, 6, 0);
    lv_obj_t *oklbl = lv_label_create(btn_ok);
    lv_label_set_text(oklbl, "ตกลง");
    lv_obj_set_style_text_font(oklbl, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(oklbl, lv_color_white(), 0);
    lv_obj_center(oklbl);
    lv_obj_add_event_cb(btn_ok, _vd_menu_time_modal_confirm_cb, LV_EVENT_CLICKED, m_ctx);
}

/* คืนหน่วยความจำ Context เมื่อแถวถูกทำลาย */
static void _vd_menu_time_range_delete_cb(lv_event_t *e) {
    _vd_menu_time_range_ctx_t *r_ctx = (_vd_menu_time_range_ctx_t *)lv_event_get_user_data(e);
    if (r_ctx) free(r_ctx);
}

/* สร้างกล่องเวลาเดี่ยว */
static lv_obj_t *_create_time_box(lv_obj_t *parent, const char *txt, lv_obj_t **lbl_out, _vd_menu_time_range_ctx_t *r_ctx) {
    lv_obj_t *box = lv_button_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_min_width(box, 105, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, VD_MENU_COLOR_CARD_BORDER, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_hor(box, 10, 0);

    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, txt ? txt : "00:00");
    lv_obj_set_style_text_font(lbl, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(lbl, VD_MENU_COLOR_TITLE, 0);
    if (lbl_out) *lbl_out = lbl;

    lv_obj_t *ic = lv_label_create(box);
    lv_label_set_text(ic, "\uf017"); // Font Awesome Clock
    lv_obj_set_style_text_font(ic, &font_awesome_20, 0);
    lv_obj_set_style_text_color(ic, VD_MENU_COLOR_TEXT_NORMAL, 0);

    lv_obj_add_event_cb(box, _vd_menu_time_box_click_cb, LV_EVENT_CLICKED, r_ctx);
    return box;
}

lv_obj_t *vd_menu_sub_content_add_time_range(vd_menu_sub_frame_t *sub_frame,
                                            const char *title,
                                            const char *subtitle,
                                            const char *default_start,
                                            const char *default_end,
                                            lv_event_cb_t cb,
                                            void *user_data) {
    if (!sub_frame || !sub_frame->card) return NULL;

    lv_obj_t *slot = vd_menu_sub_content_create(sub_frame, title, subtitle);
    if (!slot) return NULL;

    _vd_menu_time_range_ctx_t *r_ctx = (_vd_menu_time_range_ctx_t *)calloc(1, sizeof(_vd_menu_time_range_ctx_t));
    if (!r_ctx) return NULL;

    // ค่าเริ่มต้นมาตรฐาน: แสดง ชม. + นาที, ละเอียดทีละ 1 สเต็ป
    r_ctx->show_hour = true;
    r_ctx->show_min  = true;
    r_ctx->show_sec  = false;
    r_ctx->hour_step = 1;
    r_ctx->min_step  = 1;
    r_ctx->sec_step  = 1;
    r_ctx->user_cb   = cb;
    r_ctx->user_data = user_data;

    lv_obj_t *range_box = lv_obj_create(slot);
    lv_obj_remove_style_all(range_box);
    lv_obj_set_size(range_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(range_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(range_box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(range_box, 8, 0);

    // เก็บ Context ไว้ในตัวแปร user_data ของ range_box
    lv_obj_set_user_data(range_box, r_ctx);
    lv_obj_add_event_cb(range_box, _vd_menu_time_range_delete_cb, LV_EVENT_DELETE, r_ctx);

    // กล่องเวลาเริ่มต้น
    r_ctx->start_box = _create_time_box(range_box, default_start ? default_start : "06:00", &r_ctx->start_lbl, r_ctx);

    // ป้ายข้อความ "ถึง"
    lv_obj_t *dash = lv_label_create(range_box);
    lv_label_set_text(dash, "ถึง");
    lv_obj_set_style_text_font(dash, &anuphan_12, 0);
    lv_obj_set_style_text_color(dash, VD_MENU_COLOR_SUBTITLE, 0);
    lv_obj_set_style_pad_ver(dash, 2, 0);

    // กล่องเวลาสิ้นสุด
    r_ctx->end_box = _create_time_box(range_box, default_end ? default_end : "22:00", &r_ctx->end_lbl, r_ctx);

    return range_box;
}

void vd_menu_time_range_set_unit(lv_obj_t *range_box, bool show_hour, bool show_min, bool show_sec) {
    if (!range_box) return;
    _vd_menu_time_range_ctx_t *r_ctx = (_vd_menu_time_range_ctx_t *)lv_obj_get_user_data(range_box);
    if (!r_ctx) return;

    r_ctx->show_hour = show_hour;
    r_ctx->show_min  = show_min;
    r_ctx->show_sec  = show_sec;

    // ขยายความกว้างกล่องเวลาอัตโนมัติหากมีวินาที (HH:MM:SS)
    int32_t min_w = show_sec ? 125 : 105;
    if (r_ctx->start_box) lv_obj_set_style_min_width(r_ctx->start_box, min_w, 0);
    if (r_ctx->end_box)   lv_obj_set_style_min_width(r_ctx->end_box, min_w, 0);
}

void vd_menu_time_range_set_step(lv_obj_t *range_box, uint16_t hour_step, uint16_t min_step, uint16_t sec_step) {
    if (!range_box) return;
    _vd_menu_time_range_ctx_t *r_ctx = (_vd_menu_time_range_ctx_t *)lv_obj_get_user_data(range_box);
    if (!r_ctx) return;

    r_ctx->hour_step = (hour_step > 0) ? hour_step : 1;
    r_ctx->min_step  = (min_step > 0)  ? min_step  : 1;
    r_ctx->sec_step  = (sec_step > 0)  ? sec_step  : 1;
}

/* ════════════════════════════════════════════════════════════
   Network UI Components & Helpers
   ════════════════════════════════════════════════════════════ */

lv_obj_t *vd_menu_status_badge_create(lv_obj_t *parent, bool is_online, const char *text) {
    if (!parent) return NULL;

    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, 28);
    lv_obj_set_style_radius(badge, 999, 0);
    lv_obj_set_style_pad_hor(badge, 12, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, is_online ? VD_MENU_COLOR_NET_ONLINE_BG : VD_MENU_COLOR_NET_OFFLINE_BG, 0);

    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(badge, 8, 0);

    // จุดไฟสถานะ
    lv_obj_t *dot = lv_obj_create(badge);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, 999, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, is_online ? lv_color_hex(0x10B981) : lv_color_hex(0xEF4444), 0);

    // ข้อความ
    lv_obj_t *lbl = lv_label_create(badge);
    lv_label_set_text(lbl, text ? text : (is_online ? "ออนไลน์" : "ออฟไลน์"));
    lv_obj_set_style_text_font(lbl, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(lbl, is_online ? VD_MENU_COLOR_NET_ONLINE_TXT : VD_MENU_COLOR_NET_OFFLINE_TXT, 0);
    lv_obj_set_style_pad_ver(lbl, 2, 0);

    return badge;
}

lv_obj_t *vd_menu_signal_bars_create(lv_obj_t *parent, uint8_t level) {
    if (!parent) return NULL;

    lv_obj_t *sig = lv_obj_create(parent);
    lv_obj_remove_style_all(sig);
    lv_obj_set_size(sig, 22, 16);
    lv_obj_set_flex_flow(sig, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sig, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(sig, 2, 0);

    static const uint8_t bar_h[4] = {4, 7, 10, 14};
    lv_color_t active_color;

    if (level >= 3)      active_color = lv_color_hex(0x059669); // เขียว (สัญญาณดีมาก)
    else if (level == 2) active_color = lv_color_hex(0xD97706); // ส้ม (ปานกลาง)
    else                 active_color = lv_color_hex(0xDC2626); // แดง (อ่อน)

    for (int i = 0; i < 4; i++) {
        lv_obj_t *bar = lv_obj_create(sig);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, 3, bar_h[i]);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

        if ((i + 1) <= level) {
            lv_obj_set_style_bg_color(bar, active_color, 0);
        } else {
            lv_obj_set_style_bg_color(bar, lv_color_hex(0xCBD5E1), 0); // สีเทาว่าง
        }
    }

    return sig;
}

lv_obj_t *vd_menu_sub_content_add_copyable_badge(vd_menu_sub_frame_t *sub_frame,
                                                const char *title,
                                                const char *subtitle,
                                                const char *value_text,
                                                lv_event_cb_t copy_cb,
                                                void *user_data) {
    lv_obj_t *slot = vd_menu_sub_content_create(sub_frame, title, subtitle);
    if (!slot) return NULL;

    // Badge แสดงค่าตัวอักษร
    lv_obj_t *badge = lv_obj_create(slot);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0xF9FAFB), 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_color(badge, VD_MENU_COLOR_CARD_BORDER, 0);
    lv_obj_set_style_radius(badge, 8, 0);
    lv_obj_set_style_pad_hor(badge, 12, 0);
    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(badge);
    lv_label_set_text(lbl, value_text ? value_text : "");
    lv_obj_set_style_text_font(lbl, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(lbl, VD_MENU_COLOR_TEXT_NORMAL, 0);
    lv_obj_set_style_pad_ver(lbl, 2, 0);

    // ปุ่มไอคอน Copy
    lv_obj_t *btn_copy = lv_button_create(slot);
    lv_obj_remove_style_all(btn_copy);
    lv_obj_set_size(btn_copy, 36, 36);
    lv_obj_set_style_bg_opa(btn_copy, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn_copy, lv_color_white(), 0);
    lv_obj_set_style_border_width(btn_copy, 1, 0);
    lv_obj_set_style_border_color(btn_copy, VD_MENU_COLOR_CARD_BORDER, 0);
    lv_obj_set_style_radius(btn_copy, 8, 0);

    lv_obj_t *ic = lv_label_create(btn_copy);
    lv_label_set_text(ic, "\uf0c5"); // Font Awesome Copy
    lv_obj_set_style_text_font(ic, &font_awesome_12, 0);
    lv_obj_set_style_text_color(ic, VD_MENU_COLOR_SUBTITLE, 0);
    lv_obj_center(ic);

    if (copy_cb) {
        lv_obj_add_event_cb(btn_copy, copy_cb, LV_EVENT_CLICKED, user_data ? user_data : (void *)value_text);
    }

    return badge;
}

lv_obj_t *vd_menu_sub_content_add_action_btn(vd_menu_sub_frame_t *sub_frame,
                                            const char *title,
                                            const char *subtitle,
                                            const char *btn_icon,
                                            const char *btn_text,
                                            const char *initial_result,
                                            lv_obj_t **result_label_out,
                                            lv_event_cb_t cb,
                                            void *user_data) {
    lv_obj_t *slot = vd_menu_sub_content_create(sub_frame, title, subtitle);
    if (!slot) return NULL;

    // ป้ายข้อความผลลัพธ์ (อยู่ซ้ายของปุ่มกด)
    lv_obj_t *res_lbl = lv_label_create(slot);
    lv_label_set_text(res_lbl, initial_result ? initial_result : "");
    lv_obj_set_style_text_font(res_lbl, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(res_lbl, VD_MENU_COLOR_SUBTITLE, 0);
    lv_obj_set_style_pad_ver(res_lbl, 2, 0);
    if (result_label_out) *result_label_out = res_lbl;

    // ปุ่ม Action (สีขาว กรอบบาง)
    lv_obj_t *btn = lv_button_create(slot);
    lv_obj_remove_style_all(btn);
    lv_obj_set_height(btn, 36);
    lv_obj_set_style_pad_hor(btn, 14, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, VD_MENU_COLOR_CARD_BORDER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn, 8, 0);

    if (btn_icon && strlen(btn_icon) > 0) {
        lv_obj_t *ic = lv_label_create(btn);
        lv_label_set_text(ic, btn_icon);
        lv_obj_set_style_text_font(ic, &font_awesome_12, 0);
        lv_obj_set_style_text_color(ic, VD_MENU_COLOR_TEXT_NORMAL, 0);
    }

    lv_obj_t *txt = lv_label_create(btn);
    lv_label_set_text(txt, btn_text ? btn_text : "");
    lv_obj_set_style_text_font(txt, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(txt, VD_MENU_COLOR_TITLE, 0);
    lv_obj_set_style_pad_ver(txt, 2, 0);

    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }

    return btn;
}

lv_obj_t *vd_menu_sub_content_add_progress_bar(vd_menu_sub_frame_t *sub_frame,
                                              const char *title,
                                              const char *subtitle,
                                              uint32_t current_val,
                                              uint32_t max_val,
                                              const char *display_text) {
    lv_obj_t *slot = vd_menu_sub_content_create(sub_frame, title, subtitle);
    if (!slot) return NULL;

    // คอนเทนเนอร์หุ้มแถบและตัวหนังสือ
    lv_obj_t *wrap = lv_obj_create(slot);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, 300, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wrap, 12, 0);

    // หลอด Bar
    lv_obj_t *bar = lv_bar_create(wrap);
    lv_obj_set_size(bar, 180, 8);
    lv_bar_set_range(bar, 0, max_val);
    lv_bar_set_value(bar, current_val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xE5E7EB), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, VD_MENU_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);

    // ตัวหนังสือค่า (เช่น 3.1 / 8.0 GB)
    lv_obj_t *lbl = lv_label_create(wrap);
    lv_label_set_text(lbl, display_text ? display_text : "");
    lv_obj_set_style_text_font(lbl, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(lbl, VD_MENU_COLOR_TEXT_NORMAL, 0);
    lv_obj_set_style_pad_ver(lbl, 2, 0);

    return bar;
}

/* เส้นแบ่งกลุ่ม 2px สีเทาอ่อน (#E5E7EB) */
lv_obj_t *vd_menu_wifi_list_add_section_divider(lv_obj_t *parent) {
    if (!parent) return NULL;
    lv_obj_t *div = lv_obj_create(parent);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, lv_pct(100), 2);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(div, lv_color_hex(0xE5E7EB), 0);
    lv_obj_set_style_radius(div, 1, 0);
    lv_obj_set_style_margin_ver(div, 6, 0);
    return div;
}

lv_obj_t *vd_menu_wifi_list_add_item(lv_obj_t *parent_list,
                                     const char *ssid,
                                     bool is_secured,
                                     bool is_connected,
                                     uint8_t signal_level,
                                     lv_event_cb_t click_cb,
                                     void *user_data) {
    if (!parent_list) return NULL;

    // 1. แถวเครือข่าย
    lv_obj_t *item = lv_obj_create(parent_list);
    lv_obj_remove_style_all(item);
    lv_obj_set_width(item, lv_pct(100));
    lv_obj_set_height(item, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(item, 8, 0);
    lv_obj_set_style_pad_ver(item, 9, 0);
    lv_obj_set_style_radius(item, 8, 0);

    // สีพื้นหลังปกติโปร่งใส + ไฮไลต์เทาอ่อนเมื่อกดแตะ (Touch Feedback)
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(0xF1F5F9), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);

    /* ── จุดแก้เส้นซ้อน: ไม่ใส่ขอบล่าง แต่ใส่ขอบบนเฉพาะแถวถัดไปในกลุ่ม ── */
    lv_obj_set_style_border_side(item, LV_BORDER_SIDE_NONE, 0);

    uint32_t child_cnt = lv_obj_get_child_count(parent_list);
    if (!is_connected && child_cnt > 1) {
        lv_obj_t *prev_child = lv_obj_get_child(parent_list, child_cnt - 2);
        // เช็คว่าตัวก่อนหน้าเป็นแถว Wi-Fi จริงๆ (ไม่ใช่เส้น Section Divider)
        if (lv_obj_get_child_count(prev_child) > 0) {
            lv_obj_set_style_border_side(item, LV_BORDER_SIDE_TOP, 0);
            lv_obj_set_style_border_width(item, 1, 0);
            lv_obj_set_style_border_color(item, lv_color_hex(0xF3F4F6), 0);
        }
    }

    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(item, 12, 0);

    // 2. ไอคอน Wi-Fi (เขียวเมื่อเชื่อมต่อ / เทาเมื่อพร้อมใช้งาน)
    lv_obj_t *w_ic = lv_label_create(item);
    lv_label_set_text(w_ic, "\uf1eb");
    lv_obj_set_style_text_font(w_ic, &font_awesome_20, 0);
    lv_obj_set_style_text_color(w_ic, is_connected ? VD_MENU_COLOR_ACCENT : lv_color_hex(0x64748B), 0);

    // 3. กล่องข้อความ (ชื่อ SSID + สถานะ)
    lv_obj_t *info_box = lv_obj_create(item);
    lv_obj_remove_style_all(info_box);
    lv_obj_set_flex_grow(info_box, 1);
    lv_obj_set_height(info_box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(info_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info_box, 1, 0);

    // แถวชื่อ SSID + แม่กุญแจ 12px
    lv_obj_t *name_row = lv_obj_create(info_box);
    lv_obj_remove_style_all(name_row);
    lv_obj_set_size(name_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(name_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(name_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(name_row, 8, 0);

    lv_obj_t *name_lbl = lv_label_create(name_row);
    lv_label_set_text(name_lbl, ssid ? ssid : "");
    lv_obj_set_style_text_font(name_lbl, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(name_lbl, VD_MENU_COLOR_TITLE, 0);
    lv_obj_set_style_pad_ver(name_lbl, 2, 0);

    // ไอคอนแม่กุญแจ 12px สีเทา
    if (is_secured) {
        lv_obj_t *lock_ic = lv_label_create(name_row);
        lv_label_set_text(lock_ic, "\uf023");
        lv_obj_set_style_text_font(lock_ic, &font_awesome_12, 0);
        lv_obj_set_style_text_color(lock_ic, lv_color_hex(0x9CA3AF), 0);
    }

    // ข้อความ "เชื่อมต่อแล้ว" ใต้ชื่อ
    if (is_connected) {
        lv_obj_t *conn_lbl = lv_label_create(info_box);
        lv_label_set_text(conn_lbl, "เชื่อมต่อแล้ว");
        lv_obj_set_style_text_font(conn_lbl, &anuphan_12, 0);
        lv_obj_set_style_text_color(conn_lbl, VD_MENU_COLOR_ACCENT, 0);
        lv_obj_set_style_pad_ver(conn_lbl, 1, 0);
    }

    // 4. แท่งสัญญาณขวาสุด
    vd_menu_signal_bars_create(item, signal_level);

    // 5. ใช้ฟังก์ชันใหม่ lv_obj_set_clickable แทนการ add_flag
    lv_obj_set_clickable(item, true);
    if (click_cb) {
        lv_obj_add_event_cb(item, click_cb, LV_EVENT_CLICKED, user_data);
    }

    return item;
}

lv_obj_t *vd_menu_sub_frame_get_card(const vd_menu_sub_frame_t *sub_frame) {
    return sub_frame ? sub_frame->card : NULL;
}

void vd_menu_content_delete(vd_menu_content_t *content) {
    if (!content) return;
    if (content->container) lv_obj_delete(content->container);
}

void vd_menu_content_clear(vd_menu_content_t *content) {
    if (!content || !content->container) return;

    // 1. ทำลาย Widgets ลูกทั้งหมดใน Container (ทริกเกอร์ _vd_menu_sub_frame_delete_cb อัตโนมัติ)
    lv_obj_clean(content->container);

    // 2. ดีด Scrollbar กลับไปที่จุดเริ่มต้นบนสุด (y = 0) เสมอ
    lv_obj_scroll_to_y(content->container, 0, LV_ANIM_OFF);
}

/* ฟังก์ชัน Utility กำหนดค่า */
void vd_slot_set_capacity(vd_slot_data_t *slot, uint16_t capacity) {
    if (!slot) return;
    slot->capacity = (capacity > 0) ? capacity : 1;
    if (slot->stock > slot->capacity) slot->stock = slot->capacity;
}

void vd_slot_set_threshold(vd_slot_data_t *slot, uint16_t low_threshold) {
    if (!slot) return;
    slot->low_threshold = low_threshold;
}

/* คำนวณสถานะของช่องจำหน่าย (รองรับทั้งเกณฑ์เฉพาะช่องและเกณฑ์กลาง) */
static vd_slot_status_t _vd_get_slot_status(const vd_slot_data_t *slot, uint16_t default_low_threshold) {
    if (slot->disabled) return VD_SLOT_STATUS_OFF;
    if (slot->stock == 0) return VD_SLOT_STATUS_OUT;

    // เลือกใช้เกณฑ์เฉพาะช่องก่อน หากไม่ได้ตั้งไว้จึงใช้เกณฑ์กลาง
    uint16_t threshold = (slot->low_threshold > 0) ? slot->low_threshold : default_low_threshold;
    if (slot->stock <= threshold) return VD_SLOT_STATUS_LOW;

    return VD_SLOT_STATUS_OK;
}

lv_obj_t *vd_menu_slot_legend_create(lv_obj_t *parent) {
    if (!parent) return NULL;

    lv_obj_t *legend = lv_obj_create(parent);
    lv_obj_remove_style_all(legend);
    lv_obj_set_size(legend, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(legend, 12, 0);

    const struct { lv_color_t color; const char *text; } items[] = {
        { VD_SLOT_COLOR_OK_DOT,  "พร้อมขาย" },
        { VD_SLOT_COLOR_LOW_DOT, "เหลือน้อย" },
        { VD_SLOT_COLOR_OUT_DOT, "ขายหมด" },
        { VD_SLOT_COLOR_OFF_DOT, "ปิดขาย" }
    };

    for (int i = 0; i < 4; i++) {
        lv_obj_t *item = lv_obj_create(legend);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 5, 0);

        // จุดสีกลม
        lv_obj_t *dot = lv_obj_create(item);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, 999, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, items[i].color, 0);

        // ข้อความ
        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, items[i].text);
        lv_obj_set_style_text_font(lbl, &anuphan_12, 0);
        lv_obj_set_style_text_color(lbl, VD_MENU_COLOR_SUBTITLE, 0);
        lv_obj_set_style_pad_ver(lbl, 2, 0);
    }

    return legend;
}

lv_obj_t *vd_menu_slot_grid_create(lv_obj_t *parent,
                                  const vd_slot_data_t *slots,
                                  uint32_t total_slots,
                                  uint8_t cols,
                                  uint16_t default_low_threshold,
                                  lv_event_cb_t click_cb,
                                  void *user_data) {
    if (!parent || !slots || total_slots == 0) return NULL;
    if (cols < 1) cols = 6;

    // คำนวณ % ความกว้างของแต่ละช่องตามจำนวนคอลัมน์ พร้อมหักลบช่องไฟ 8px
    int32_t w_pct;
    switch (cols) {
        case 3:  w_pct = 31; break;
        case 4:  w_pct = 23; break;
        case 5:  w_pct = 18; break;
        case 6:  w_pct = 15; break;
        default: w_pct = (100 - (cols * 2)) / cols; break;
    }

    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);
    lv_obj_set_scrollable(grid, false);

    for (uint32_t i = 0; i < total_slots; i++) {
        const vd_slot_data_t *s = &slots[i];
        vd_slot_status_t st = _vd_get_slot_status(s, default_low_threshold);

        lv_obj_t *chip = lv_button_create(grid);
        lv_obj_remove_style_all(chip);
        lv_obj_set_width(chip, lv_pct(w_pct));
        lv_obj_set_height(chip, 76);
        lv_obj_set_style_pad_hor(chip, 8, 0);
        lv_obj_set_style_pad_ver(chip, 7, 0);
        lv_obj_set_style_radius(chip, 10, 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_clickable(chip, true);

        // กำหนดชุดสีตามสถานะ
        lv_color_t bg_color, border_color, badge_bg, badge_txt;
        switch (st) {
            case VD_SLOT_STATUS_LOW:
                bg_color = VD_SLOT_COLOR_LOW_BG;       border_color = VD_SLOT_COLOR_LOW_BORDER;
                badge_bg = VD_SLOT_COLOR_LOW_BADGE_BG; badge_txt = VD_SLOT_COLOR_LOW_BADGE_TXT;
                break;
            case VD_SLOT_STATUS_OUT:
                bg_color = VD_SLOT_COLOR_OUT_BG;       border_color = VD_SLOT_COLOR_OUT_BORDER;
                badge_bg = VD_SLOT_COLOR_OUT_BADGE_BG; badge_txt = VD_SLOT_COLOR_OUT_BADGE_TXT;
                break;
            case VD_SLOT_STATUS_OFF:
                bg_color = VD_SLOT_COLOR_OFF_BG;       border_color = VD_SLOT_COLOR_OFF_BORDER;
                badge_bg = VD_SLOT_COLOR_OFF_BADGE_BG; badge_txt = VD_SLOT_COLOR_OFF_BADGE_TXT;
                lv_obj_set_style_opa(chip, LV_OPA_60, 0);
                break;
            default:
                bg_color = VD_SLOT_COLOR_OK_BG;        border_color = VD_SLOT_COLOR_OK_BORDER;
                badge_bg = VD_SLOT_COLOR_OK_BADGE_BG;  badge_txt = VD_SLOT_COLOR_OK_BADGE_TXT;
                break;
        }

        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(chip, bg_color, 0);
        lv_obj_set_style_border_color(chip, border_color, 0);

        // ── 1. แถวบน: รหัสช่อง (ซ้าย) + Badge จำนวนสินค้า (ขวา) ──
        lv_obj_t *top_row = lv_obj_create(chip);
        lv_obj_remove_style_all(top_row);
        lv_obj_set_width(top_row, lv_pct(100));
        lv_obj_set_height(top_row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *code_lbl = lv_label_create(top_row);
        lv_label_set_text(code_lbl, s->code);
        lv_obj_set_style_text_font(code_lbl, &anuphan_bold_16, 0);
        lv_obj_set_style_text_color(code_lbl, VD_SLOT_COLOR_CODE_TXT, 0);

        lv_obj_t *badge_lbl = lv_label_create(top_row);
        if (st == VD_SLOT_STATUS_OFF) {
            lv_label_set_text(badge_lbl, "ปิด");
        } else if (st == VD_SLOT_STATUS_OUT) {
            lv_label_set_text(badge_lbl, "หมด");
        } else {
            lv_label_set_text_fmt(badge_lbl, "%u/%u", s->stock, s->capacity);
        }
        lv_obj_set_style_text_font(badge_lbl, &anuphan_12, 0);
        lv_obj_set_style_text_color(badge_lbl, badge_txt, 0);
        lv_obj_set_style_bg_color(badge_lbl, badge_bg, 0);
        lv_obj_set_style_bg_opa(badge_lbl, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(badge_lbl, 999, 0);
        lv_obj_set_style_pad_hor(badge_lbl, 6, 0);
        lv_obj_set_style_pad_ver(badge_lbl, 2, 0);

        // ── 2. แถวกลาง: ชื่อสินค้า ──
        lv_obj_t *name_lbl = lv_label_create(chip);
        lv_label_set_text(name_lbl, s->product);
        lv_obj_set_style_text_font(name_lbl, &anuphan_med_14, 0);
        lv_obj_set_style_text_color(name_lbl, VD_MENU_COLOR_TITLE, 0);
        lv_obj_set_width(name_lbl, lv_pct(100));
        lv_obj_set_height(name_lbl, 18);
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);

        // ── 3. แถวล่าง: ราคาขาย (อยู่ล่างสุดเสมอ) ──
        lv_obj_t *price_row = lv_obj_create(chip);
        lv_obj_remove_style_all(price_row);
        lv_obj_set_size(price_row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(price_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(price_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_set_style_pad_column(price_row, 4, 0);

        bool has_discount = s->discount_on && (s->discount > 0) && (s->discount < s->price);

        if (has_discount) {
            // ราคาเดิม: ขีดฆ่าสีเทา
            lv_obj_t *old_price_lbl = lv_label_create(price_row);
            lv_label_set_text_fmt(old_price_lbl, "%u฿", (unsigned)s->price);
            lv_obj_set_style_text_font(old_price_lbl, &anuphan_12, 0);
            lv_obj_set_style_text_color(old_price_lbl, VD_SLOT_COLOR_PRICE_OLD_TXT, 0);
            lv_obj_set_style_text_decor(old_price_lbl, LV_TEXT_DECOR_STRIKETHROUGH, 0);

            // ราคาลดพิเศษ: สีแดง
            uint32_t net_price = s->price - s->discount;
            lv_obj_t *new_price_lbl = lv_label_create(price_row);
            lv_label_set_text_fmt(new_price_lbl, "%u฿", (unsigned)net_price);
            lv_obj_set_style_text_font(new_price_lbl, &anuphan_bold_16, 0);
            lv_obj_set_style_text_color(new_price_lbl, VD_SLOT_COLOR_PRICE_DISC_TXT, 0);
        } else {
            // ราคาปกติ: สีเขียวมรกต
            lv_obj_t *price_lbl = lv_label_create(price_row);
            lv_label_set_text_fmt(price_lbl, "%u฿", (unsigned)s->price);
            lv_obj_set_style_text_font(price_lbl, &anuphan_bold_16, 0);
            lv_obj_set_style_text_color(price_lbl, VD_SLOT_COLOR_PRICE_TXT, 0);
        }

        if (click_cb) lv_obj_add_event_cb(chip, click_cb, LV_EVENT_CLICKED, (void *)s);
    }

    return grid;
}

lv_obj_t *vd_menu_slot_stats_create(lv_obj_t *parent,
                                    const vd_slot_data_t *slots,
                                    uint32_t total_slots,
                                    uint16_t default_low_threshold) {
    if (!parent || !slots) return NULL;

    uint32_t cnt_ok = 0, cnt_low = 0, cnt_out = 0, cnt_off = 0;
    for (uint32_t i = 0; i < total_slots; i++) {
        switch (_vd_get_slot_status(&slots[i], default_low_threshold)) {
            case VD_SLOT_STATUS_OK:  cnt_ok++;  break;
            case VD_SLOT_STATUS_LOW: cnt_low++; break;
            case VD_SLOT_STATUS_OUT: cnt_out++; break;
            case VD_SLOT_STATUS_OFF: cnt_off++; break;
        }
    }

    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(wrap, 16, 0);
    lv_obj_set_style_margin_top(wrap, 6, 0);

    const struct {
        lv_color_t bg; lv_color_t txt; lv_color_t dot; uint32_t count; const char *fmt;
    } stats[] = {
        { VD_SLOT_COLOR_OK_BADGE_BG,  VD_SLOT_COLOR_OK_BADGE_TXT,  VD_SLOT_COLOR_OK_DOT,  cnt_ok,  "พร้อมขาย %u ช่อง" },
        { VD_SLOT_COLOR_LOW_BADGE_BG, VD_SLOT_COLOR_LOW_BADGE_TXT, VD_SLOT_COLOR_LOW_DOT, cnt_low, "เหลือน้อย %u ช่อง" },
        { VD_SLOT_COLOR_OUT_BADGE_BG, VD_SLOT_COLOR_OUT_BADGE_TXT, VD_SLOT_COLOR_OUT_DOT, cnt_out, "ขายหมด %u ช่อง" },
        { VD_SLOT_COLOR_OFF_BADGE_BG, VD_SLOT_COLOR_OFF_BADGE_TXT, VD_SLOT_COLOR_OFF_DOT, cnt_off, "ปิดขาย %u ช่อง" }
    };

    for (int i = 0; i < 4; i++) {
        lv_obj_t *pill = lv_obj_create(wrap);
        lv_obj_remove_style_all(pill);
        lv_obj_set_size(pill, LV_SIZE_CONTENT, 28);
        lv_obj_set_style_radius(pill, 999, 0);
        lv_obj_set_style_pad_hor(pill, 12, 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(pill, stats[i].bg, 0);
        lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(pill, 6, 0);

        lv_obj_t *dot = lv_obj_create(pill);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, 999, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, stats[i].dot, 0);

        char buf[64];
        snprintf(buf, sizeof(buf), stats[i].fmt, (unsigned)stats[i].count);

        lv_obj_t *lbl = lv_label_create(pill);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_font(lbl, &anuphan_med_14, 0);
        lv_obj_set_style_text_color(lbl, stats[i].txt, 0);
        lv_obj_set_style_pad_ver(lbl, 2, 0);
    }
    return wrap;
}

static void _vd_stepper_btn_cb(lv_event_t *e) {
    _vd_stepper_ctx_t *ctx = (_vd_stepper_ctx_t *)lv_event_get_user_data(e);
    intptr_t dir = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (!ctx) return;

    ctx->val += dir;
    if (ctx->val < ctx->min) ctx->val = ctx->min;
    if (ctx->val > ctx->max) ctx->val = ctx->max;

    char buf[32];
    snprintf(buf, sizeof(buf), "%d %s", (int)ctx->val, ctx->unit ? ctx->unit : "");
    lv_label_set_text(ctx->val_lbl, buf);

    if (ctx->user_cb) ctx->user_cb(e);
}

static void _vd_stepper_delete_cb(lv_event_t *e) {
    void *ctx = lv_event_get_user_data(e);
    if (ctx) free(ctx);
}

lv_obj_t *vd_menu_sub_content_add_stepper(vd_menu_sub_frame_t *sub_frame,
                                         const char *title,
                                         const char *subtitle,
                                         int32_t default_val,
                                         const char *unit,
                                         lv_event_cb_t change_cb,
                                         void *user_data) {
    lv_obj_t *slot = vd_menu_sub_content_create(sub_frame, title, subtitle);
    if (!slot) return NULL;

    lv_obj_t *stepper = lv_obj_create(slot);
    lv_obj_remove_style_all(stepper);
    lv_obj_set_size(stepper, LV_SIZE_CONTENT, 34);
    lv_obj_set_style_bg_opa(stepper, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(stepper, lv_color_white(), 0);
    lv_obj_set_style_border_width(stepper, 1, 0);
    lv_obj_set_style_border_color(stepper, VD_MENU_COLOR_CARD_BORDER, 0);
    lv_obj_set_style_radius(stepper, 8, 0);
    lv_obj_set_flex_flow(stepper, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stepper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(stepper, false);

    _vd_stepper_ctx_t *ctx = (_vd_stepper_ctx_t *)malloc(sizeof(_vd_stepper_ctx_t));
    if (!ctx) return NULL;

    ctx->val       = default_val;
    ctx->min       = 1;
    ctx->max       = 99;
    ctx->unit      = unit;
    ctx->user_cb   = change_cb;
    ctx->user_data = user_data;

    lv_obj_add_event_cb(stepper, _vd_stepper_delete_cb, LV_EVENT_DELETE, ctx);

    // ปุ่มลบ (-)
    lv_obj_t *btn_minus = lv_button_create(stepper);
    lv_obj_remove_style_all(btn_minus);
    lv_obj_set_size(btn_minus, 34, 34);
    lv_obj_set_clickable(btn_minus, true);
    lv_obj_set_user_data(btn_minus, (void *)-1);
    lv_obj_t *m_ic = lv_label_create(btn_minus);
    lv_label_set_text(m_ic, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(m_ic, VD_MENU_COLOR_SUBTITLE, 0);
    lv_obj_center(m_ic);
    lv_obj_add_event_cb(btn_minus, _vd_stepper_btn_cb, LV_EVENT_CLICKED, ctx);

    // ตัวเลขค่าตรงกลาง
    ctx->val_lbl = lv_label_create(stepper);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d %s", (int)default_val, unit ? unit : "");
    lv_label_set_text(ctx->val_lbl, buf);
    lv_obj_set_style_pad_hor(ctx->val_lbl, 14, 0);
    lv_obj_set_style_text_font(ctx->val_lbl, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(ctx->val_lbl, VD_MENU_COLOR_TITLE, 0);
    lv_obj_set_style_pad_ver(ctx->val_lbl, 2, 0);

    // ปุ่มบวก (+)
    lv_obj_t *btn_plus = lv_button_create(stepper);
    lv_obj_remove_style_all(btn_plus);
    lv_obj_set_size(btn_plus, 34, 34);
    lv_obj_set_clickable(btn_plus, true);
    lv_obj_set_user_data(btn_plus, (void *)1);
    lv_obj_t *p_ic = lv_label_create(btn_plus);
    lv_label_set_text(p_ic, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(p_ic, VD_MENU_COLOR_SUBTITLE, 0);
    lv_obj_center(p_ic);
    lv_obj_add_event_cb(btn_plus, _vd_stepper_btn_cb, LV_EVENT_CLICKED, ctx);

    return stepper;
}

/* ตรวจสอบสถานะการแก้ไข (Dirty Check) */
static bool _vd_slot_modal_is_dirty(const _vd_slot_modal_ctx_t *ctx) {
    if (!ctx || !ctx->target_slot) return false;

    const char *cur_name = lv_textarea_get_text(ctx->ta_name);
    const char *cur_img  = lv_textarea_get_text(ctx->ta_img_path);
    uint32_t cur_price = (uint32_t)atoi(lv_textarea_get_text(ctx->ta_price));
    uint32_t cur_discount = (uint32_t)atoi(lv_textarea_get_text(ctx->ta_discount));
    bool cur_discount_on = lv_obj_has_state(ctx->sw_discount, LV_STATE_CHECKED);
    bool is_enabled = lv_obj_has_state(ctx->sw_enabled, LV_STATE_CHECKED);

    if (strcmp(cur_name, ctx->target_slot->product) != 0) return true;
    if (strcmp(cur_img, ctx->target_slot->img) != 0) return true;
    if (cur_price != ctx->target_slot->price) return true;
    if (cur_discount_on != ctx->target_slot->discount_on) return true;
    if (cur_discount_on && (cur_discount != ctx->target_slot->discount)) return true;
    if (ctx->draft_slot.stock != ctx->target_slot->stock) return true;
    if (ctx->draft_slot.capacity != ctx->target_slot->capacity) return true;
    if (ctx->draft_slot.low_threshold != ctx->target_slot->low_threshold) return true;
    if (is_enabled == ctx->target_slot->disabled) return true;

    return false;
}

/* ตรวจสอบความถูกต้องของส่วนลด และแสดงผลราคาขายจริง */
static bool _vd_slot_modal_validate_discount(_vd_slot_modal_ctx_t *ctx, uint32_t price, uint32_t discount, bool discount_on) {
    if (!discount_on) {
        lv_label_set_text(ctx->discount_net_lbl, "");
        return true;
    }

    if (discount >= price) {
        lv_label_set_text_fmt(ctx->discount_net_lbl, "ส่วนลดต้องน้อยกว่าราคาเต็ม (%u บาท)", (unsigned)price);
        lv_obj_set_style_text_color(ctx->discount_net_lbl, lv_color_hex(0xDC2626), 0); // แดง
        return false;
    } else {
        uint32_t net = price - discount;
        lv_label_set_text_fmt(ctx->discount_net_lbl, "ขายจริง %u บาท", (unsigned)net);
        lv_obj_set_style_text_color(ctx->discount_net_lbl, VD_MENU_COLOR_ACCENT, 0); // เขียว
        return true;
    }
}

static void _vd_slot_modal_check_dirty(_vd_slot_modal_ctx_t *ctx) {
    if (!ctx) return;

    uint32_t cur_price = (uint32_t)atoi(lv_textarea_get_text(ctx->ta_price));
    uint32_t cur_discount = (uint32_t)atoi(lv_textarea_get_text(ctx->ta_discount));
    bool cur_discount_on = lv_obj_has_state(ctx->sw_discount, LV_STATE_CHECKED);

    bool valid_discount = _vd_slot_modal_validate_discount(ctx, cur_price, cur_discount, cur_discount_on);
    bool is_dirty = _vd_slot_modal_is_dirty(ctx);

    if (is_dirty && valid_discount) {
        lv_obj_set_style_bg_color(ctx->btn_save, VD_MODAL_COLOR_BTN_PRIMARY, 0);
        lv_obj_set_clickable(ctx->btn_save, true);
    } else {
        lv_obj_set_style_bg_color(ctx->btn_save, VD_MODAL_COLOR_BTN_DISABLED, 0);
        lv_obj_set_clickable(ctx->btn_save, false);
    }

    const char *cur_name = lv_textarea_get_text(ctx->ta_name);
    lv_label_set_text_fmt(ctx->m_sub_lbl, "%s", cur_name);
}

static void _vd_slot_modal_stock_step_cb(lv_event_t *e) {
    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;

    // ดึงค่าทิศทาง (-1 หรือ +1) ผ่าน API ทางการของ LVGL
    lv_obj_t *target_btn = lv_event_get_target(e);
    intptr_t dir = (intptr_t)lv_obj_get_user_data(target_btn);

    int32_t next_stk = (int32_t)ctx->draft_slot.stock + dir;
    if (next_stk < 0) next_stk = 0;
    if (next_stk > ctx->draft_slot.capacity) next_stk = ctx->draft_slot.capacity;

    ctx->draft_slot.stock = (uint16_t)next_stk;

    char buf[32];
    snprintf(buf, sizeof(buf), "%u / %u ชิ้น", ctx->draft_slot.stock, ctx->draft_slot.capacity);
    lv_label_set_text(ctx->stock_val_lbl, buf);

    _vd_slot_modal_check_dirty(ctx);
}

/* Event เมื่อพิมพ์ข้อความ / เปลี่ยนตัวเลข / สลับสวิตช์ */
static void _vd_slot_modal_input_change_cb(lv_event_t *e) {
    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)lv_event_get_user_data(e);
    _vd_slot_modal_check_dirty(ctx);
}

/* ตัวแปร Static เก็บ Context ของ Modal ที่กำลังเปิดอยู่ปัจจุบัน */
static _vd_slot_modal_ctx_t *s_active_modal_ctx = NULL;

void vd_slot_modal_set_motor_result(bool success, const char *message) {
    if (!s_active_modal_ctx || !s_active_modal_ctx->motor_result_lbl) return;

    lv_label_set_text(s_active_modal_ctx->motor_result_lbl, message ? message : "");
    lv_obj_set_style_text_color(s_active_modal_ctx->motor_result_lbl,
                               success ? VD_MENU_COLOR_ACCENT : lv_color_hex(0xEF4444), 0);

    // เปิดให้กดปุ่มทดสอบได้ใหม่อีกครั้ง
    if (s_active_modal_ctx->motor_test_btn) {
        lv_obj_set_clickable(s_active_modal_ctx->motor_test_btn, true);
    }
}

/* Event เมื่อกดปุ่มทดสอบมอเตอร์ */
static void _vd_slot_modal_motor_test_cb(lv_event_t *e) {
    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;

    // 1. ล็อกปุ่มและขึ้นสถานะกำลังหมุนบนหน้าจอ
    lv_label_set_text(ctx->motor_result_lbl, "กำลังหมุนมอเตอร์…");
    lv_obj_set_style_text_color(ctx->motor_result_lbl, lv_color_hex(0xD97706), 0);
    lv_obj_set_clickable(ctx->motor_test_btn, false);

    // 2. ยิง Callback แจ้งให้ฮาร์ดแวร์ ESP32 เริ่มสั่งขับมอเตอร์จริง
    if (ctx->cbs.on_motor_test) {
        ctx->cbs.on_motor_test(ctx->target_slot->code, ctx->user_data);
    }
}

/* ฟังก์ชันตัวช่วยสร้างเส้นคั่นบางระหว่างแถวภายใน Modal */
static lv_obj_t *_create_modal_divider(lv_obj_t *parent) {
    lv_obj_t *div = lv_obj_create(parent);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, lv_pct(100), 1);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(div, lv_color_hex(0xF3F4F6), 0);
    return div;
}

/* ฟังก์ชันตัวช่วยสร้างคอลัมน์ฝั่งซ้าย (ชื่อการตั้งค่า + คำอธิบายย่อยสีเทา) */
static lv_obj_t *_create_modal_ctrl_info(lv_obj_t *row, const char *title, const char *desc) {
    lv_obj_t *info = lv_obj_create(row);
    lv_obj_remove_style_all(info);
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info, 2, 0);

    lv_obj_t *t_lbl = lv_label_create(info);
    lv_label_set_text(t_lbl, title ? title : "");
    lv_obj_set_style_text_font(t_lbl, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(t_lbl, VD_MENU_COLOR_TITLE, 0);
    lv_obj_set_style_pad_ver(t_lbl, 1, 0);

    if (desc && strlen(desc) > 0) {
        lv_obj_t *d_lbl = lv_label_create(info);
        lv_label_set_text(d_lbl, desc);
        lv_obj_set_style_text_font(d_lbl, &anuphan_12, 0);
        lv_obj_set_style_text_color(d_lbl, VD_MENU_COLOR_SUBTITLE, 0);
        lv_obj_set_style_pad_ver(d_lbl, 1, 0);
    }
    return info;
}

/* สร้างหัวข้อ Section (เช่น "ข้อมูลการขาย", "การจัดการช่อง") พร้อมเส้นบาง */
static lv_obj_t *_create_modal_section_title(lv_obj_t *parent, const char *title) {
    lv_obj_t *sec = lv_obj_create(parent);
    lv_obj_remove_style_all(sec);
    lv_obj_set_size(sec, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sec, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sec, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sec, 10, 0);
    lv_obj_set_style_margin_top(sec, 6, 0);
    lv_obj_set_style_pad_ver(sec, 2, 0); // เผื่อระยะแถวแนวตั้ง 2px

    lv_obj_t *lbl = lv_label_create(sec);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &anuphan_bold_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x9CA3AF), 0);

    /* ── จุดแก้: ขยายพื้นที่วาดด้านบนไม่ให้หัวตัวอักษรและวรรณยุกต์โดนตัด ── */
    lv_obj_set_style_pad_top(lbl, 3, 0);   // เพิ่มช่องว่างด้านบน 3px ให้หัวอักษรไม่ชนขอบ
    lv_obj_set_style_pad_bottom(lbl, 2, 0);
    lv_obj_set_ext_draw_size(lbl, 4);     // เผื่อขอบเขตวาดภายนอกอีก 4px

    lv_obj_t *line = lv_obj_create(sec);
    lv_obj_remove_style_all(line);
    lv_obj_set_flex_grow(line, 1);
    lv_obj_set_height(line, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(0xE5E7EB), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);

    return sec;
}

/* อัปเดตพรีวิวรูปภาพปก */
static void _vd_slot_modal_update_cover(_vd_slot_modal_ctx_t *ctx) {
    if (!ctx || !ctx->cover_img || !ctx->cover_ph_label) return;

    const char *p = lv_textarea_get_text(ctx->ta_img_path);
    if (p && strlen(p) > 0) {
        // ซ่อนข้อความ Placeholder และแสดง Image Widget
        lv_obj_set_hidden(ctx->cover_ph_label, true);
        lv_obj_set_hidden(ctx->cover_img, false);

        // ★ ถ้ามี Callback ที่ผู้ใช้กำหนดมา ให้เรียกใช้ Callback ของผู้ใช้
        if (ctx->cbs.on_set_image) {
            ctx->cbs.on_set_image(ctx->cover_img, p, ctx->user_data);
        } else {
            // ค่าดีฟอลต์: ส่ง String Path ให้ LVGL ดึงตรงๆ
            lv_image_set_src(ctx->cover_img, p);
        }
    } else {
        // กรณีไม่มีข้อความพาธ ให้ซ่อนรูปและแสดง Placeholder
        lv_obj_set_hidden(ctx->cover_img, true);
        lv_obj_set_hidden(ctx->cover_ph_label, false);
    }
}

/* ปรับความจุสูงสุด (Capacity) */
static void _vd_slot_modal_cap_step_cb(lv_event_t *e) {
    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)lv_event_get_user_data(e);
    intptr_t dir = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (!ctx) return;

    int32_t next_cap = (int32_t)ctx->draft_slot.capacity + dir;
    if (next_cap < 1) next_cap = 1;
    if (next_cap > 50) next_cap = 50;

    ctx->draft_slot.capacity = (uint16_t)next_cap;
    if (ctx->draft_slot.stock > ctx->draft_slot.capacity) {
        ctx->draft_slot.stock = ctx->draft_slot.capacity;
    }
    if (ctx->draft_slot.low_threshold >= ctx->draft_slot.capacity) {
        ctx->draft_slot.low_threshold = ctx->draft_slot.capacity - 1;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%u ชิ้น", ctx->draft_slot.capacity);
    lv_label_set_text(ctx->cap_val_lbl, buf);

    snprintf(buf, sizeof(buf), "%u / %u ชิ้น", ctx->draft_slot.stock, ctx->draft_slot.capacity);
    lv_label_set_text(ctx->stock_val_lbl, buf);

    snprintf(buf, sizeof(buf), "%u ชิ้น", ctx->draft_slot.low_threshold);
    lv_label_set_text(ctx->low_val_lbl, buf);

    _vd_slot_modal_check_dirty(ctx);
}

/* ปรับเกณฑ์เหลือน้อยเฉพาะช่อง (Low Threshold) */
static void _vd_slot_modal_low_step_cb(lv_event_t *e) {
    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)lv_event_get_user_data(e);
    intptr_t dir = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (!ctx) return;

    int32_t next_low = (int32_t)ctx->draft_slot.low_threshold + dir;
    if (next_low < 0) next_low = 0;
    if (next_low >= ctx->draft_slot.capacity) next_low = ctx->draft_slot.capacity - 1;

    ctx->draft_slot.low_threshold = (uint16_t)next_low;

    char buf[32];
    snprintf(buf, sizeof(buf), "%u ชิ้น", ctx->draft_slot.low_threshold);
    lv_label_set_text(ctx->low_val_lbl, buf);

    _vd_slot_modal_check_dirty(ctx);
}

/* สลับเปิด/ปิดแถวป้อนยอดส่วนลด */
static void _vd_slot_modal_discount_toggle_cb(lv_event_t *e) {
    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)lv_event_get_user_data(e);
    bool on = lv_obj_has_state(ctx->sw_discount, LV_STATE_CHECKED);
    if (on) {
        lv_obj_remove_flag(ctx->r_discount, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->r_discount, LV_OBJ_FLAG_HIDDEN);
    }
    _vd_slot_modal_check_dirty(ctx);
}

/* คัดลอกค่าใหม่เข้า Target Struct และยิง Callback */
static void _vd_slot_modal_do_save(_vd_slot_modal_ctx_t *ctx) {
    if (!ctx || !ctx->target_slot) return;

    // ชื่อสินค้า
    const char *new_name = lv_textarea_get_text(ctx->ta_name);
    strncpy(ctx->target_slot->product, new_name, sizeof(ctx->target_slot->product) - 1);
    ctx->target_slot->product[sizeof(ctx->target_slot->product) - 1] = '\0';

    // พาธรูปภาพ
    const char *new_img = lv_textarea_get_text(ctx->ta_img_path);
    strncpy(ctx->target_slot->img, new_img, sizeof(ctx->target_slot->img) - 1);
    ctx->target_slot->img[sizeof(ctx->target_slot->img) - 1] = '\0';

    // ราคาและส่วนลด
    ctx->target_slot->price         = (uint32_t)atoi(lv_textarea_get_text(ctx->ta_price));
    ctx->target_slot->discount_on   = lv_obj_has_state(ctx->sw_discount, LV_STATE_CHECKED);
    ctx->target_slot->discount      = (uint32_t)atoi(lv_textarea_get_text(ctx->ta_discount));

    // สต็อก, ความจุ, เกณฑ์เตือน, และสถานะ
    ctx->target_slot->stock         = ctx->draft_slot.stock;
    ctx->target_slot->capacity      = ctx->draft_slot.capacity;
    ctx->target_slot->low_threshold = ctx->draft_slot.low_threshold;
    ctx->target_slot->disabled      = !lv_obj_has_state(ctx->sw_enabled, LV_STATE_CHECKED);

    if (ctx->cbs.on_save) {
        ctx->cbs.on_save(ctx->target_slot, ctx->user_data);
    }
}

/* ทำลาย Modal ออกจากหน้าจออย่างปลอดภัย */
static void _vd_slot_modal_do_close(_vd_slot_modal_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->closing) return; // ★ ดักป้องกันการเรียกซ้ำ
    ctx->closing = true;

    if (ctx->cbs.on_close) {
        ctx->cbs.on_close(ctx->user_data);
    }

    if (ctx->backdrop) {
        lv_obj_delete_async(ctx->backdrop);
    }
}

/* ร้องขอการปิดหน้าต่าง (ดักกรณียังไม่ได้บันทึก) */
static void _vd_slot_modal_request_close(_vd_slot_modal_ctx_t *ctx) {
    if (!ctx) return;

    if (_vd_slot_modal_is_dirty(ctx)) {
      if (ctx->confirm_layer) {
        lv_obj_set_style_bg_color(ctx->confirm_layer, lv_color_hex(0x0F172A), 0);
        lv_obj_set_style_bg_opa(ctx->confirm_layer, LV_OPA_70, 0); // ค่อยเปิดสีทึบตอนจะแสดง
        lv_obj_remove_flag(ctx->confirm_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ctx->confirm_layer);
      }
    } else {
      // ไม่ได้แก้ไข -> ปิดได้ทันที
      _vd_slot_modal_do_close(ctx);
    }
}

/* ปุ่ม 1: ยกเลิก -> ซ่อนกล่องถามเพื่อกลับไปแก้ไขต่อ */
static void _confirm_cancel_cb(lv_event_t *e) {
    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)lv_event_get_user_data(e);
    if (ctx && ctx->confirm_layer) {
        lv_obj_add_flag(ctx->confirm_layer, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ปุ่ม 2: ปิดโดยไม่บันทึก -> ทิ้งค่าแล้วปิด Modal ทันที */
static void _confirm_discard_cb(lv_event_t *e) {
    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)lv_event_get_user_data(e);
    _vd_slot_modal_do_close(ctx);
}

/* ปุ่ม 3: บันทึกและปิด -> เซฟค่า, เรียก on_save(), แล้วปิด */
static void _confirm_save_and_close_cb(lv_event_t *e) {
    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    _vd_slot_modal_do_save(ctx);
    _vd_slot_modal_do_close(ctx);
}

/* Event เมื่อกดปุ่ม "บันทึก" หลักสีเขียวใน Footer */
static void _vd_slot_modal_save_cb(lv_event_t *e) {
    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    _vd_slot_modal_do_save(ctx);
    _vd_slot_modal_do_close(ctx);
}

/* Event เมื่อกดปุ่มปิด [X] ที่ Header */
static void _vd_slot_modal_close_cb(lv_event_t *e) {
    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)lv_event_get_user_data(e);
    _vd_slot_modal_request_close(ctx);
}

/* Event เมื่อแตะ Backdrop ด้านนอก */
static void _vd_slot_modal_backdrop_click_cb(lv_event_t *e) {
    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)lv_event_get_user_data(e);
    if (lv_event_get_target(e) == ctx->backdrop) {
        _vd_slot_modal_request_close(ctx);
    }
}

// ตัวจัดการคืนแรม Modal เพียงจุดเดียวเมื่อ Backdrop ถูกทำลาย
static void _vd_slot_modal_backdrop_delete_cb(lv_event_t *e) {
    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;

    if (s_active_modal_ctx == ctx) {
        s_active_modal_ctx = NULL; // ป้องกัน Dangling Pointer
    }

    free(ctx);

    lv_obj_set_clickable(lv_layer_top(), false);
}

void vd_slot_edit_modal_open(vd_slot_data_t *slot,
                            uint16_t low_threshold,
                            const vd_slot_modal_cbs_t *cbs,
                            void *user_data) {
    if (!slot) return;
    if (s_active_modal_ctx) return; // ป้องกันเปิดซ้อน

    _vd_slot_modal_ctx_t *ctx = (_vd_slot_modal_ctx_t *)calloc(1, sizeof(_vd_slot_modal_ctx_t));
    if (!ctx) return;
    s_active_modal_ctx = ctx;

    if (cbs) ctx->cbs = *cbs;
    ctx->user_data     = user_data;
    ctx->target_slot   = slot;
    ctx->draft_slot    = *slot;
    ctx->low_threshold = low_threshold;

    lv_obj_set_clickable(lv_layer_top(), true);

    // 1. ฉากหลังมืดโปร่งแสง (Backdrop เต็มจอ)
    ctx->backdrop = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ctx->backdrop);
    lv_obj_set_size(ctx->backdrop, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(ctx->backdrop, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(ctx->backdrop, VD_MODAL_COLOR_BACKDROP, 0);
    lv_obj_set_clickable(ctx->backdrop, true);
    lv_obj_add_event_cb(ctx->backdrop, _vd_slot_modal_backdrop_delete_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(ctx->backdrop, _vd_slot_modal_backdrop_click_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_set_scrollable(ctx->backdrop, false);

    // 2. การ์ดหน้าต่าง Modal สีขาว (กว้าง 520px, สูง 540px กำหนดความสูงตายตัว ป้องกันค้าง)
    ctx->dialog = lv_obj_create(ctx->backdrop);
    lv_obj_remove_style_all(ctx->dialog);
    lv_obj_set_size(ctx->dialog, 520, 540);
    lv_obj_center(ctx->dialog);
    lv_obj_set_style_bg_opa(ctx->dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ctx->dialog, VD_MODAL_COLOR_BG, 0);
    lv_obj_set_style_radius(ctx->dialog, 16, 0);
    lv_obj_set_flex_flow(ctx->dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_clickable(ctx->dialog, false);

    // ── 3. Header Section (รหัสช่อง + หัวข้อ + ปุ่มปิด X) ──
    lv_obj_t *header = lv_obj_create(ctx->dialog);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT); // ปรับเป็น SIZE_CONTENT ให้ขยายตามความสูงจริง
    lv_obj_set_style_pad_hor(header, 22, 0);
    lv_obj_set_style_pad_ver(header, 14, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, VD_MODAL_COLOR_DIVIDER, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 14, 0);

    ctx->m_code_box = lv_obj_create(header);
    lv_obj_remove_style_all(ctx->m_code_box);
    lv_obj_set_size(ctx->m_code_box, 48, 48);
    lv_obj_set_style_radius(ctx->m_code_box, 12, 0);
    lv_obj_set_style_bg_opa(ctx->m_code_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ctx->m_code_box, VD_SLOT_COLOR_OK_BADGE_BG, 0);
    lv_obj_set_flex_flow(ctx->m_code_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->m_code_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // ★ จุดแก้ที่ 1: คำว่า "ช่อง" เพิ่ม pad_top กันไม้เอกด้วน
    lv_obj_t *lbl_c_sub = lv_label_create(ctx->m_code_box);
    lv_label_set_text(lbl_c_sub, "ช่อง");
    lv_obj_set_style_text_font(lbl_c_sub, &anuphan_12, 0);
    lv_obj_set_style_text_color(lbl_c_sub, VD_SLOT_COLOR_OK_BADGE_TXT, 0);
    lv_obj_set_style_pad_top(lbl_c_sub, 3, 0);   // เพิ่มช่องว่างบน 3px เผื่อไม้เอก
    lv_obj_set_ext_draw_size(lbl_c_sub, 4);

    lv_obj_t *lbl_c_val = lv_label_create(ctx->m_code_box);
    lv_label_set_text(lbl_c_val, slot->code);
    lv_obj_set_style_text_font(lbl_c_val, &anuphan_bold_16, 0);
    lv_obj_set_style_text_color(lbl_c_val, VD_SLOT_COLOR_OK_BADGE_TXT, 0);

    // คอนเทนเนอร์ข้อความหัวข้อ
    lv_obj_t *h_info = lv_obj_create(header);
    lv_obj_remove_style_all(h_info);
    lv_obj_set_flex_grow(h_info, 1);
    lv_obj_set_height(h_info, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(h_info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(h_info, 2, 0);

    // ★ จุดแก้ที่ 2: คำว่า "แก้ไขช่องจำหน่าย" เพิ่ม pad_top กันไม้โท/ไม้เอกด้วน
    lv_obj_t *m_title = lv_label_create(h_info);
    lv_label_set_text(m_title, "แก้ไขช่องจำหน่าย");
    lv_obj_set_style_text_font(m_title, &anuphan_bold_16, 0);
    lv_obj_set_style_text_color(m_title, VD_MENU_COLOR_TITLE, 0);
    lv_obj_set_style_pad_top(m_title, 4, 0);    // เผื่อความสูงให้ไม้โทบน "แก้" และไม้เอกบน "ช่อง"
    lv_obj_set_style_pad_bottom(m_title, 2, 0);
    lv_obj_set_ext_draw_size(m_title, 6);

    // ★ จุดแก้ที่ 3: คำบรรยายย่อยด้านล่าง เผื่อระยะวรรณยุกต์และสระล่าง
    ctx->m_sub_lbl = lv_label_create(h_info);
    lv_obj_set_style_text_font(ctx->m_sub_lbl, &anuphan_12, 0);
    lv_obj_set_style_text_color(ctx->m_sub_lbl, VD_MENU_COLOR_SUBTITLE, 0);
    lv_obj_set_style_pad_ver(ctx->m_sub_lbl, 2, 0);
    lv_obj_set_ext_draw_size(ctx->m_sub_lbl, 4);
    lv_label_set_text_fmt(ctx->m_sub_lbl, "%s", slot->product);

    // ปุ่มปิด X
    lv_obj_t *btn_x = lv_button_create(header);
    lv_obj_remove_style_all(btn_x);
    lv_obj_set_size(btn_x, 36, 36);
    lv_obj_set_style_radius(btn_x, 8, 0);
    lv_obj_set_clickable(btn_x, true);
    lv_obj_t *x_ic = lv_label_create(btn_x);
    lv_label_set_text(x_ic, "\uf00d");
    lv_obj_set_style_text_font(x_ic, &font_awesome_20, 0);
    lv_obj_set_style_text_color(x_ic, VD_MENU_COLOR_SUBTITLE, 0);
    lv_obj_center(x_ic);
    lv_obj_add_event_cb(btn_x, _vd_slot_modal_close_cb, LV_EVENT_CLICKED, ctx);

    // ── 4. Body Section (เลื่อน Scroll ได้อย่างลื่นไหล) ──
    lv_obj_t *body = lv_obj_create(ctx->dialog);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, 22, 0);
    lv_obj_set_style_pad_ver(body, 14, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 12, 0);
    lv_obj_set_scrollable(body, true);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *cover_wrap = lv_obj_create(body);
    lv_obj_remove_style_all(cover_wrap);
    lv_obj_set_size(cover_wrap, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cover_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cover_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 2. กรอบรูปภาพ ล็อกขนาด 204 x 154 px เป๊ะๆ
    ctx->cover_box = lv_obj_create(cover_wrap);
    lv_obj_remove_style_all(ctx->cover_box);
    lv_obj_set_size(ctx->cover_box, 204, 154);
    lv_obj_set_style_bg_color(ctx->cover_box, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_opa(ctx->cover_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ctx->cover_box, 1, 0);
    lv_obj_set_style_border_color(ctx->cover_box, VD_MODAL_COLOR_BORDER, 0);
    //lv_obj_set_style_radius(ctx->cover_box, 10, 0);
    //lv_obj_set_style_clip_corner(ctx->cover_box, true, 0); // บังคับตัดขอบรูปไม่ให้ล้นมุมมน 10px

    // 3. วิดเจ็ต Image ด้านใน ล็อกขนาด 204 x 154 และจัดกึ่งกลางกรอบ
    ctx->cover_img = lv_image_create(ctx->cover_box);
    lv_obj_set_size(ctx->cover_img, 204, 154);
    lv_image_set_inner_align(ctx->cover_img, LV_IMAGE_ALIGN_CENTER); // จัดตำแหน่งภาพข้างในให้อยู่กลาง
    lv_obj_center(ctx->cover_img);

    // 4. ป้ายกรณีไม่มีรูปภาพ
    ctx->cover_ph_label = lv_label_create(ctx->cover_box);
    lv_label_set_text(ctx->cover_ph_label, "ไม่มีภาพให้แสดง");
    lv_obj_set_style_text_font(ctx->cover_ph_label, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(ctx->cover_ph_label, lv_color_hex(0x9CA3AF), 0);
    lv_obj_center(ctx->cover_ph_label);

    // 5. ช่องป้อนที่อยู่ไฟล์รูปภาพ (อยู่ใต้กล่องรูป)
    ctx->ta_img_path = lv_textarea_create(body);
    lv_obj_set_size(ctx->ta_img_path, lv_pct(100), 36);
    lv_textarea_set_one_line(ctx->ta_img_path, true);
    lv_textarea_set_placeholder_text(ctx->ta_img_path, "/flash/สินค้า/ชื่อไฟล์.png");
    lv_textarea_set_text(ctx->ta_img_path, slot->img);
    lv_obj_set_style_border_color(ctx->ta_img_path, VD_MODAL_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(ctx->ta_img_path, 1, 0);
    lv_obj_set_style_radius(ctx->ta_img_path, 8, 0);
    lv_obj_set_style_text_font(ctx->ta_img_path, &anuphan_med_14, 0);
    lv_obj_add_event_cb(ctx->ta_img_path, _vd_slot_modal_input_change_cb, LV_EVENT_VALUE_CHANGED, ctx);
    _vd_slot_modal_update_cover(ctx);

    /* ── ส่วนที่ 2: ข้อมูลการขาย ── */
    _create_modal_section_title(body, "ข้อมูลการขาย");

    // ชื่อสินค้า
    lv_obj_t *r_name = lv_obj_create(body);
    lv_obj_remove_style_all(r_name);
    lv_obj_set_size(r_name, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r_name, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r_name, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _create_modal_ctrl_info(r_name, "สินค้าในช่อง", NULL);

    ctx->ta_name = lv_textarea_create(r_name);
    lv_obj_set_size(ctx->ta_name, 280, 36);
    lv_textarea_set_one_line(ctx->ta_name, true);
    lv_textarea_set_max_length(ctx->ta_name, 40);
    lv_textarea_set_text(ctx->ta_name, slot->product);
    lv_obj_set_style_border_color(ctx->ta_name, VD_MODAL_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(ctx->ta_name, 1, 0);
    lv_obj_set_style_radius(ctx->ta_name, 8, 0);
    lv_obj_set_style_text_font(ctx->ta_name, &anuphan_med_14, 0);
    lv_obj_add_event_cb(ctx->ta_name, _vd_slot_modal_input_change_cb, LV_EVENT_VALUE_CHANGED, ctx);

    // ราคาเต็ม
    lv_obj_t *r_price = lv_obj_create(body);
    lv_obj_remove_style_all(r_price);
    lv_obj_set_size(r_price, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r_price, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r_price, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _create_modal_ctrl_info(r_price, "ราคาเต็ม (บาท)", "ราคาก่อนหักส่วนลด");

    ctx->ta_price = lv_textarea_create(r_price);
    lv_obj_set_size(ctx->ta_price, 100, 36);
    lv_textarea_set_one_line(ctx->ta_price, true);
    char p_buf[16];
    snprintf(p_buf, sizeof(p_buf), "%u", (unsigned)slot->price);
    lv_textarea_set_text(ctx->ta_price, p_buf);
    lv_textarea_set_accepted_chars(ctx->ta_price, "0123456789");
    lv_obj_set_style_border_color(ctx->ta_price, VD_MODAL_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(ctx->ta_price, 1, 0);
    lv_obj_set_style_radius(ctx->ta_price, 8, 0);
    lv_obj_set_style_text_font(ctx->ta_price, &anuphan_bold_16, 0);
    lv_obj_add_event_cb(ctx->ta_price, _vd_slot_modal_input_change_cb, LV_EVENT_VALUE_CHANGED, ctx);

    // เปิดใช้ส่วนลด
    lv_obj_t *r_disc_toggle = lv_obj_create(body);
    lv_obj_remove_style_all(r_disc_toggle);
    lv_obj_set_size(r_disc_toggle, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r_disc_toggle, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r_disc_toggle, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _create_modal_ctrl_info(r_disc_toggle, "เปิดใช้ส่วนลด", "หักยอดส่วนลดจากราคาเต็ม");

    ctx->sw_discount = lv_switch_create(r_disc_toggle);
    lv_obj_set_size(ctx->sw_discount, 44, 24);
    lv_obj_set_style_bg_color(ctx->sw_discount, VD_MENU_COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (slot->discount_on) lv_obj_add_state(ctx->sw_discount, LV_STATE_CHECKED);
    lv_obj_add_event_cb(ctx->sw_discount, _vd_slot_modal_discount_toggle_cb, LV_EVENT_VALUE_CHANGED, ctx);

    // แถวยอดส่วนลด (ซ่อนถ้าไม่ได้เปิด)
    ctx->r_discount = lv_obj_create(body);
    lv_obj_remove_style_all(ctx->r_discount);
    lv_obj_set_size(ctx->r_discount, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->r_discount, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->r_discount, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _create_modal_ctrl_info(ctx->r_discount, "ยอดส่วนลด (บาท)", NULL);

    lv_obj_t *disc_r_box = lv_obj_create(ctx->r_discount);
    lv_obj_remove_style_all(disc_r_box);
    lv_obj_set_size(disc_r_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(disc_r_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(disc_r_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_row(disc_r_box, 3, 0);

    ctx->ta_discount = lv_textarea_create(disc_r_box);
    lv_obj_set_size(ctx->ta_discount, 100, 36);
    lv_textarea_set_one_line(ctx->ta_discount, true);
    char d_buf[16];
    snprintf(d_buf, sizeof(d_buf), "%u", (unsigned)slot->discount);
    lv_textarea_set_text(ctx->ta_discount, d_buf);
    lv_textarea_set_accepted_chars(ctx->ta_discount, "0123456789");
    lv_obj_set_style_border_color(ctx->ta_discount, VD_MODAL_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(ctx->ta_discount, 1, 0);
    lv_obj_set_style_radius(ctx->ta_discount, 8, 0);
    lv_obj_set_style_text_font(ctx->ta_discount, &anuphan_bold_16, 0);
    lv_obj_add_event_cb(ctx->ta_discount, _vd_slot_modal_input_change_cb, LV_EVENT_VALUE_CHANGED, ctx);

    ctx->discount_net_lbl = lv_label_create(disc_r_box);
    lv_obj_set_style_text_font(ctx->discount_net_lbl, &anuphan_12, 0);

    if (!slot->discount_on) {
        lv_obj_add_flag(ctx->r_discount, LV_OBJ_FLAG_HIDDEN);
    }

    /* ── ส่วนที่ 3: การจัดการช่อง ── */
    _create_modal_section_title(body, "การจัดการช่อง");

    // จำนวนคงเหลือ
    lv_obj_t *r_stock = lv_obj_create(body);
    lv_obj_remove_style_all(r_stock);
    lv_obj_set_size(r_stock, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r_stock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r_stock, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _create_modal_ctrl_info(r_stock, "จำนวนสินค้าคงเหลือ", NULL);

    lv_obj_t *stk_stepper = lv_obj_create(r_stock);
    lv_obj_remove_style_all(stk_stepper);
    lv_obj_set_size(stk_stepper, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_border_width(stk_stepper, 1, 0);
    lv_obj_set_style_border_color(stk_stepper, VD_MODAL_COLOR_BORDER, 0);
    lv_obj_set_style_radius(stk_stepper, 8, 0);
    lv_obj_set_flex_flow(stk_stepper, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stk_stepper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(stk_stepper, false);

    lv_obj_t *btn_sm = lv_button_create(stk_stepper);
    lv_obj_remove_style_all(btn_sm);
    lv_obj_set_size(btn_sm, 36, 36);
    lv_obj_set_clickable(btn_sm, true);
    lv_obj_set_user_data(btn_sm, (void *)(intptr_t)-1);
    lv_obj_t *sm_sign = lv_label_create(btn_sm);
    lv_label_set_text(sm_sign, LV_SYMBOL_MINUS);
    lv_obj_center(sm_sign);
    lv_obj_add_event_cb(btn_sm, _vd_slot_modal_stock_step_cb, LV_EVENT_CLICKED, ctx);

    ctx->stock_val_lbl = lv_label_create(stk_stepper);
    char s_buf[32];
    snprintf(s_buf, sizeof(s_buf), "%u / %u ชิ้น", slot->stock, slot->capacity);
    lv_label_set_text(ctx->stock_val_lbl, s_buf);
    lv_obj_set_style_pad_hor(ctx->stock_val_lbl, 14, 0);
    lv_obj_set_style_text_font(ctx->stock_val_lbl, &anuphan_med_14, 0);

    lv_obj_t *btn_sp = lv_button_create(stk_stepper);
    lv_obj_remove_style_all(btn_sp);
    lv_obj_set_size(btn_sp, 36, 36);
    lv_obj_set_clickable(btn_sp, true);
    lv_obj_set_user_data(btn_sp, (void *)(intptr_t)1);
    lv_obj_t *sp_sign = lv_label_create(btn_sp);
    lv_label_set_text(sp_sign, LV_SYMBOL_PLUS);
    lv_obj_center(sp_sign);
    lv_obj_add_event_cb(btn_sp, _vd_slot_modal_stock_step_cb, LV_EVENT_CLICKED, ctx);

    // ความจุสูงสุดของช่อง
    lv_obj_t *r_cap = lv_obj_create(body);
    lv_obj_remove_style_all(r_cap);
    lv_obj_set_size(r_cap, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r_cap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r_cap, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _create_modal_ctrl_info(r_cap, "ความจุสูงสุดของช่อง", NULL);

    lv_obj_t *cap_stepper = lv_obj_create(r_cap);
    lv_obj_remove_style_all(cap_stepper);
    lv_obj_set_size(cap_stepper, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_border_width(cap_stepper, 1, 0);
    lv_obj_set_style_border_color(cap_stepper, VD_MODAL_COLOR_BORDER, 0);
    lv_obj_set_style_radius(cap_stepper, 8, 0);
    lv_obj_set_flex_flow(cap_stepper, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cap_stepper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(cap_stepper, false);

    lv_obj_t *btn_cm = lv_button_create(cap_stepper);
    lv_obj_remove_style_all(btn_cm);
    lv_obj_set_size(btn_cm, 36, 36);
    lv_obj_set_clickable(btn_cm, true);
    lv_obj_set_user_data(btn_cm, (void *)(intptr_t)-1);
    lv_obj_t *cm_sign = lv_label_create(btn_cm);
    lv_label_set_text(cm_sign, LV_SYMBOL_MINUS);
    lv_obj_center(cm_sign);
    lv_obj_add_event_cb(btn_cm, _vd_slot_modal_cap_step_cb, LV_EVENT_CLICKED, ctx);

    ctx->cap_val_lbl = lv_label_create(cap_stepper);
    snprintf(s_buf, sizeof(s_buf), "%u ชิ้น", slot->capacity);
    lv_label_set_text(ctx->cap_val_lbl, s_buf);
    lv_obj_set_style_pad_hor(ctx->cap_val_lbl, 14, 0);
    lv_obj_set_style_text_font(ctx->cap_val_lbl, &anuphan_med_14, 0);

    lv_obj_t *btn_cp = lv_button_create(cap_stepper);
    lv_obj_remove_style_all(btn_cp);
    lv_obj_set_size(btn_cp, 36, 36);
    lv_obj_set_clickable(btn_cp, true);
    lv_obj_set_user_data(btn_cp, (void *)(intptr_t)1);
    lv_obj_t *cp_sign = lv_label_create(btn_cp);
    lv_label_set_text(cp_sign, LV_SYMBOL_PLUS);
    lv_obj_center(cp_sign);
    lv_obj_add_event_cb(btn_cp, _vd_slot_modal_cap_step_cb, LV_EVENT_CLICKED, ctx);

    // เกณฑ์ "เหลือน้อย" ของช่องนี้
    lv_obj_t *r_low = lv_obj_create(body);
    lv_obj_remove_style_all(r_low);
    lv_obj_set_size(r_low, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r_low, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r_low, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _create_modal_ctrl_info(r_low, "เกณฑ์สินค้าเหลือน้อย", "แสดงสถานะเหลือน้อยเมื่อเหลือไม่เกินค่านี้");

    lv_obj_t *low_stepper = lv_obj_create(r_low);
    lv_obj_remove_style_all(low_stepper);
    lv_obj_set_size(low_stepper, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_border_width(low_stepper, 1, 0);
    lv_obj_set_style_border_color(low_stepper, VD_MODAL_COLOR_BORDER, 0);
    lv_obj_set_style_radius(low_stepper, 8, 0);
    lv_obj_set_flex_flow(low_stepper, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(low_stepper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(low_stepper, false);

    lv_obj_t *btn_lm = lv_button_create(low_stepper);
    lv_obj_remove_style_all(btn_lm);
    lv_obj_set_size(btn_lm, 36, 36);
    lv_obj_set_clickable(btn_lm, true);
    lv_obj_set_user_data(btn_lm, (void *)(intptr_t)-1);
    lv_obj_t *lm_sign = lv_label_create(btn_lm);
    lv_label_set_text(lm_sign, LV_SYMBOL_MINUS);
    lv_obj_center(lm_sign);
    lv_obj_add_event_cb(btn_lm, _vd_slot_modal_low_step_cb, LV_EVENT_CLICKED, ctx);

    ctx->low_val_lbl = lv_label_create(low_stepper);
    snprintf(s_buf, sizeof(s_buf), "%u ชิ้น", (slot->low_threshold > 0 ? slot->low_threshold : low_threshold));
    lv_label_set_text(ctx->low_val_lbl, s_buf);
    lv_obj_set_style_pad_hor(ctx->low_val_lbl, 14, 0);
    lv_obj_set_style_text_font(ctx->low_val_lbl, &anuphan_med_14, 0);

    lv_obj_t *btn_lp = lv_button_create(low_stepper);
    lv_obj_remove_style_all(btn_lp);
    lv_obj_set_size(btn_lp, 36, 36);
    lv_obj_set_clickable(btn_lp, true);
    lv_obj_set_user_data(btn_lp, (void *)(intptr_t)1);
    lv_obj_t *lp_sign = lv_label_create(btn_lp);
    lv_label_set_text(lp_sign, LV_SYMBOL_PLUS);
    lv_obj_center(lp_sign);
    lv_obj_add_event_cb(btn_lp, _vd_slot_modal_low_step_cb, LV_EVENT_CLICKED, ctx);

    // เปิดขายช่องนี้
    lv_obj_t *r_enable = lv_obj_create(body);
    lv_obj_remove_style_all(r_enable);
    lv_obj_set_size(r_enable, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r_enable, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r_enable, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _create_modal_ctrl_info(r_enable, "วางจำหน่างสินค้านี้", "หากต้องการพักการขาย ให้ปิดไว้");

    ctx->sw_enabled = lv_switch_create(r_enable);
    lv_obj_set_size(ctx->sw_enabled, 44, 24);
    lv_obj_set_style_bg_color(ctx->sw_enabled, VD_MENU_COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (!slot->disabled) lv_obj_add_state(ctx->sw_enabled, LV_STATE_CHECKED);
    lv_obj_add_event_cb(ctx->sw_enabled, _vd_slot_modal_input_change_cb, LV_EVENT_VALUE_CHANGED, ctx);

    // ทดสอบมอเตอร์
    lv_obj_t *r_motor = lv_obj_create(body);
    lv_obj_remove_style_all(r_motor);
    lv_obj_set_size(r_motor, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r_motor, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r_motor, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _create_modal_ctrl_info(r_motor, "ทดสอบเกลียวหมุนสินค้า", NULL);

    lv_obj_t *motor_right = lv_obj_create(r_motor);
    lv_obj_remove_style_all(motor_right);
    lv_obj_set_size(motor_right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(motor_right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(motor_right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(motor_right, 10, 0);

    ctx->motor_result_lbl = lv_label_create(motor_right);
    lv_label_set_text(ctx->motor_result_lbl, "ยังไม่ได้ทดสอบ");
    lv_obj_set_style_text_font(ctx->motor_result_lbl, &anuphan_12, 0);
    lv_obj_set_style_text_color(ctx->motor_result_lbl, VD_MENU_COLOR_SUBTITLE, 0);

    ctx->motor_test_btn = lv_button_create(motor_right);
    lv_obj_remove_style_all(ctx->motor_test_btn);
    lv_obj_set_height(ctx->motor_test_btn, 34);
    lv_obj_set_style_pad_hor(ctx->motor_test_btn, 12, 0);
    lv_obj_set_style_border_width(ctx->motor_test_btn, 1, 0);
    lv_obj_set_style_border_color(ctx->motor_test_btn, VD_MODAL_COLOR_BORDER, 0);
    lv_obj_set_style_radius(ctx->motor_test_btn, 8, 0);
    lv_obj_set_flex_flow(ctx->motor_test_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->motor_test_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctx->motor_test_btn, 8, 0);
    lv_obj_set_clickable(ctx->motor_test_btn, true);

    lv_obj_t *m_ic = lv_label_create(ctx->motor_test_btn);
    lv_label_set_text(m_ic, "\uf7d9");
    lv_obj_set_style_text_font(m_ic, &font_awesome_12, 0);
    lv_obj_set_style_text_color(m_ic, VD_MENU_COLOR_SUBTITLE, 0);

    lv_obj_t *btn_motor_lbl = lv_label_create(ctx->motor_test_btn);
    lv_label_set_text(btn_motor_lbl, "ทดสอบ");
    lv_obj_set_style_text_font(btn_motor_lbl, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(btn_motor_lbl, VD_MENU_COLOR_TITLE, 0);
    lv_obj_add_event_cb(ctx->motor_test_btn, _vd_slot_modal_motor_test_cb, LV_EVENT_CLICKED, ctx);

    // ── 5. Footer Section (ปุ่มบันทึกสีเขียว) ──
    lv_obj_t *footer = lv_obj_create(ctx->dialog);
    lv_obj_remove_style_all(footer);
    lv_obj_set_size(footer, lv_pct(100), 62);
    lv_obj_set_style_pad_hor(footer, 22, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_color(footer, VD_MODAL_COLOR_DIVIDER, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ctx->btn_save = lv_button_create(footer);
    lv_obj_remove_style_all(ctx->btn_save);
    lv_obj_set_height(ctx->btn_save, 38);
    lv_obj_set_style_pad_hor(ctx->btn_save, 20, 0);
    lv_obj_set_style_radius(ctx->btn_save, 10, 0);
    lv_obj_set_style_bg_opa(ctx->btn_save, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ctx->btn_save, VD_MODAL_COLOR_BTN_DISABLED, 0);
    lv_obj_set_flex_flow(ctx->btn_save, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->btn_save, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctx->btn_save, 6, 0);
    lv_obj_set_clickable(ctx->btn_save, false);

    lv_obj_t *chk_ic = lv_label_create(ctx->btn_save);
    lv_label_set_text(chk_ic, "\uf00c");
    lv_obj_set_style_text_font(chk_ic, &font_awesome_12, 0);
    lv_obj_set_style_text_color(chk_ic, lv_color_white(), 0);

    lv_obj_t *save_lbl = lv_label_create(ctx->btn_save);
    lv_label_set_text(save_lbl, "บันทึก");
    lv_obj_set_style_text_font(save_lbl, &anuphan_bold_16, 0);
    lv_obj_set_style_text_color(save_lbl, lv_color_white(), 0);
    lv_obj_add_event_cb(ctx->btn_save, _vd_slot_modal_save_cb, LV_EVENT_CLICKED, ctx);

    // ── 6. Confirm Layer (สร้างบน backdrop ตรงๆ ตัดปัญหา Layout วนลูป) ──
    ctx->confirm_layer = lv_obj_create(ctx->backdrop);
    lv_obj_remove_style_all(ctx->confirm_layer);
    lv_obj_set_size(ctx->confirm_layer, 520, 540);
    lv_obj_center(ctx->confirm_layer);

    // ★ ปรับเป็นโปร่งใสเริ่มต้น (ไม่ต้องให้เอนจินมานั่งคำนวณ Blend สีทับ)
    lv_obj_set_style_bg_opa(ctx->confirm_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(ctx->confirm_layer, 16, 0);
    lv_obj_set_hidden(ctx->confirm_layer, true); // ซ่อนไว้

    lv_obj_set_style_bg_color(ctx->confirm_layer, lv_color_hex(0x0F172A), 0);
    lv_obj_set_flex_flow(ctx->confirm_layer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->confirm_layer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(ctx->confirm_layer, 20, 0);
    lv_obj_set_clickable(ctx->confirm_layer, true);

    lv_obj_t *c_box = lv_obj_create(ctx->confirm_layer);
    lv_obj_remove_style_all(c_box);
    lv_obj_set_width(c_box, lv_pct(92));
    lv_obj_set_height(c_box, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c_box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(c_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c_box, 1, 0);
    lv_obj_set_style_border_color(c_box, VD_MODAL_COLOR_BORDER, 0);
    lv_obj_set_style_radius(c_box, 14, 0);
    lv_obj_set_style_pad_all(c_box, 18, 0);
    lv_obj_set_flex_flow(c_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c_box, 16, 0);

    lv_obj_t *c_head = lv_obj_create(c_box);
    lv_obj_remove_style_all(c_head);
    lv_obj_set_size(c_head, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c_head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c_head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(c_head, 12, 0);

    lv_obj_t *warn_circle = lv_obj_create(c_head);
    lv_obj_remove_style_all(warn_circle);
    lv_obj_set_size(warn_circle, 38, 38);
    lv_obj_set_style_radius(warn_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(warn_circle, lv_color_hex(0xFEF3C7), 0);
    lv_obj_set_style_bg_opa(warn_circle, LV_OPA_COVER, 0);
    lv_obj_t *w_ic = lv_label_create(warn_circle);
    lv_label_set_text(w_ic, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(w_ic, lv_color_hex(0xB45309), 0);
    lv_obj_center(w_ic);

    lv_obj_t *c_txt_box = lv_obj_create(c_head);
    lv_obj_remove_style_all(c_txt_box);
    lv_obj_set_flex_grow(c_txt_box, 1);
    lv_obj_set_height(c_txt_box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c_txt_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c_txt_box, 3, 0);

    lv_obj_t *c_title = lv_label_create(c_txt_box);
    lv_label_set_text(c_title, "บันทึกการเปลี่ยนแปลงก่อนปิด?");
    lv_obj_set_style_text_font(c_title, &anuphan_bold_16, 0);
    lv_obj_set_style_text_color(c_title, VD_MENU_COLOR_TITLE, 0);

    lv_obj_t *c_desc = lv_label_create(c_txt_box);
    lv_label_set_text(c_desc, "มีการแก้ไขที่ยังไม่ได้บันทึก หากปิดตอนนี้การแก้ไขจะสูญหาย");
    lv_obj_set_style_text_font(c_desc, &anuphan_12, 0);
    lv_obj_set_style_text_color(c_desc, VD_MENU_COLOR_SUBTITLE, 0);

    lv_obj_t *c_actions = lv_obj_create(c_box);
    lv_obj_remove_style_all(c_actions);
    lv_obj_set_size(c_actions, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c_actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c_actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c_actions, 8, 0);

    // ปุ่ม: ยกเลิก
    lv_obj_t *btn_c_cancel = lv_button_create(c_actions);
    lv_obj_remove_style_all(btn_c_cancel);
    lv_obj_set_height(btn_c_cancel, 36);
    lv_obj_set_style_pad_hor(btn_c_cancel, 14, 0);
    lv_obj_set_style_radius(btn_c_cancel, 8, 0);
    lv_obj_set_style_bg_color(btn_c_cancel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn_c_cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn_c_cancel, 1, 0);
    lv_obj_set_style_border_color(btn_c_cancel, VD_MODAL_COLOR_BORDER, 0);
    lv_obj_t *lbl_c_cancel = lv_label_create(btn_c_cancel);
    lv_label_set_text(lbl_c_cancel, "ยกเลิก");
    lv_obj_set_style_text_font(lbl_c_cancel, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(lbl_c_cancel, VD_MENU_COLOR_TITLE, 0);
    lv_obj_center(lbl_c_cancel);
    lv_obj_add_event_cb(btn_c_cancel, _confirm_cancel_cb, LV_EVENT_CLICKED, ctx);

    // ปุ่ม: ปิดโดยไม่บันทึก
    lv_obj_t *btn_c_discard = lv_button_create(c_actions);
    lv_obj_remove_style_all(btn_c_discard);
    lv_obj_set_height(btn_c_discard, 36);
    lv_obj_set_style_pad_hor(btn_c_discard, 14, 0);
    lv_obj_set_style_radius(btn_c_discard, 8, 0);
    lv_obj_set_style_bg_color(btn_c_discard, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn_c_discard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn_c_discard, 1, 0);
    lv_obj_set_style_border_color(btn_c_discard, lv_color_hex(0xFECACA), 0);
    lv_obj_t *lbl_c_discard = lv_label_create(btn_c_discard);
    lv_label_set_text(lbl_c_discard, "ปิดโดยไม่บันทึก");
    lv_obj_set_style_text_font(lbl_c_discard, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(lbl_c_discard, lv_color_hex(0xB91C1C), 0);
    lv_obj_center(lbl_c_discard);
    lv_obj_add_event_cb(btn_c_discard, _confirm_discard_cb, LV_EVENT_CLICKED, ctx);

    // ปุ่ม: บันทึกและปิด
    lv_obj_t *btn_c_save = lv_button_create(c_actions);
    lv_obj_remove_style_all(btn_c_save);
    lv_obj_set_height(btn_c_save, 36);
    lv_obj_set_style_pad_hor(btn_c_save, 16, 0);
    lv_obj_set_style_radius(btn_c_save, 8, 0);
    lv_obj_set_style_bg_color(btn_c_save, VD_MODAL_COLOR_BTN_PRIMARY, 0);
    lv_obj_set_style_bg_opa(btn_c_save, LV_OPA_COVER, 0);
    lv_obj_t *lbl_c_save = lv_label_create(btn_c_save);
    lv_label_set_text(lbl_c_save, "บันทึกและปิด");
    lv_obj_set_style_text_font(lbl_c_save, &anuphan_bold_16, 0);
    lv_obj_set_style_text_color(lbl_c_save, lv_color_white(), 0);
    lv_obj_center(lbl_c_save);
    lv_obj_add_event_cb(btn_c_save, _confirm_save_and_close_cb, LV_EVENT_CLICKED, ctx);
}



/* ════════════════════════════════════════════════════════════
   Dynamic File Browser Engine (Memory Managed)
   ════════════════════════════════════════════════════════════ */

/* ════════════════════════════════════════════════════════════
   Dynamic File Browser Engine (Memory Managed & Dynamic Menu)
   ════════════════════════════════════════════════════════════ */

/* ฟังก์ชันคืนแรมรายการไฟล์ */
void vd_file_items_free(vd_file_item_t *items, uint32_t count) {
    if (!items) return;
    for (uint32_t i = 0; i < count; i++) {
        if (items[i].name) {
            free(items[i].name);
            items[i].name = NULL;
        }
    }
    free(items);
}

/* ฟังก์ชันคืนแรมรายการไดรฟ์ */
void vd_drives_free(vd_drive_info_t *drives, uint32_t count) {
    if (!drives) return;
    for (uint32_t i = 0; i < count; i++) {
        if (drives[i].id) free(drives[i].id);
        if (drives[i].name) free(drives[i].name);
    }
    free(drives);
}

/* เคลียร์ข้อมูลไฟล์ชุดปัจจุบันในแรม */
static void _vd_files_clear_items_cache(_vd_files_ctx_t *ctx) {
    if (ctx->cached_items) {
        vd_file_items_free(ctx->cached_items, ctx->cached_item_count);
        ctx->cached_items = NULL;
        ctx->cached_item_count = 0;
    }
}

/* เคลียร์ข้อมูลไดรฟ์ในแรม */
static void _vd_files_clear_drives_cache(_vd_files_ctx_t *ctx) {
    if (ctx->cached_drives) {
        vd_drives_free(ctx->cached_drives, ctx->cached_drive_count);
        ctx->cached_drives = NULL;
        ctx->cached_drive_count = 0;
    }
}

/* ฟอร์แมตขนาด MB ให้เป็น GB พร้อมทศนิยมที่ถูกต้อง */
static void _vd_file_format_mb(uint32_t mb, char *out_buf, size_t buf_size) {
    if (mb >= 1024) {
        float gb = (float)mb / 1024.0f;
        if ((mb % 1024) == 0) {
            snprintf(out_buf, buf_size, "%uGB", (unsigned)(mb / 1024));
        } else {
            snprintf(out_buf, buf_size, "%.1fGB", gb);
        }
    } else {
        snprintf(out_buf, buf_size, "%uMB", (unsigned)mb);
    }
}

/* แปลงขนาดไฟล์ KB เป็นข้อความอ่านง่าย */
static void _vd_file_format_size(uint32_t size_kb, char *out_buf, size_t buf_size) {
    if (size_kb < 1024) {
        snprintf(out_buf, buf_size, "%u KB", (unsigned)size_kb);
    } else if (size_kb < (1024 * 1024)) {
        snprintf(out_buf, buf_size, "%.1f MB", (float)size_kb / 1024.0f);
    } else {
        snprintf(out_buf, buf_size, "%.1f GB", (float)size_kb / (1024.0f * 1024.0f));
    }
}

/* สร้างการ์ดไดรฟ์ (แก้ปัญหาวรรณยุกต์, สี Bar ธีมเขียว/แดง และฟอร์แมต GB) */
lv_obj_t *vd_menu_file_drive_tile_create(lv_obj_t *parent,
                                         const char *drive_name,
                                         uint32_t used_mb,
                                         uint32_t total_mb,
                                         lv_event_cb_t click_cb,
                                         void *user_data) {
    if (!parent) return NULL;

    // การ์ดไดรฟ์สไตล์ปุ่มกด
    lv_obj_t *tile = lv_button_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_set_width(tile, lv_pct(31)); // แบ่ง 3 คอลัมน์
    lv_obj_set_height(tile, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(tile, 12, 0);
    lv_obj_set_style_radius(tile, 12, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0xF8FAFC), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 10, 0);
    lv_obj_set_clickable(tile, true);

    // แถวบน: ไอคอนฮาร์ดไดรฟ์ + ชื่อไดรฟ์
    lv_obj_t *top = lv_obj_create(tile);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 9, 0);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_CLICKABLE); // ★ ปลดคลิกเพื่อให้กดทะลุลงการ์ดแม่

    // ★ แก้ไขที่ 3: เปลี่ยนไอคอนเป็น \uf0a0 (fa-hard-drive) ตาม HTML
    lv_obj_t *ic = lv_label_create(top);
    lv_label_set_text(ic, "\uf0a0");
    lv_obj_set_style_text_font(ic, &font_awesome_12, 0);
    lv_obj_set_style_text_color(ic, VD_MENU_COLOR_SUBTITLE, 0);
    lv_obj_remove_flag(ic, LV_OBJ_FLAG_CLICKABLE); // ★ กดทะลุ

    lv_obj_t *name_lbl = lv_label_create(top);
    lv_label_set_text(name_lbl, drive_name ? drive_name : "Drive");
    lv_obj_set_style_text_font(name_lbl, &anuphan_med_16, 0);
    lv_obj_set_style_text_color(name_lbl, VD_MENU_COLOR_TITLE, 0);
    lv_obj_set_style_pad_top(name_lbl, 4, 0);
    lv_obj_set_style_pad_bottom(name_lbl, 2, 0);
    lv_obj_set_ext_draw_size(name_lbl, 6);
    lv_obj_remove_flag(name_lbl, LV_OBJ_FLAG_CLICKABLE); // ★ กดทะลุ

    // แท่งแสดงสัดส่วนการใช้งาน
    uint32_t pct = (total_mb > 0) ? (used_mb * 100 / total_mb) : 0;
    if (pct > 100) pct = 100;

    bool is_low_space = (total_mb > 0) && (((total_mb - used_mb) * 100 / total_mb) < 10);
    lv_color_t bar_color = is_low_space ? lv_color_hex(0xDC2626) : VD_MENU_COLOR_ACCENT;

    lv_obj_t *bar = lv_bar_create(tile);
    lv_obj_set_size(bar, lv_pct(100), 10);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xE5E7EB), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, bar_color, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE); // ★ จุดแก้ที่ 1: ปลดคลิกบนแท่งบาร์เพื่อให้แตะติดทั้งการ์ด

    // ข้อความบอกขนาด
    char used_str[24], total_str[24], sub_buf[64];
    _vd_file_format_mb(used_mb, used_str, sizeof(used_str));
    _vd_file_format_mb(total_mb, total_str, sizeof(total_str));
    snprintf(sub_buf, sizeof(sub_buf), "%s / %s", used_str, total_str);

    lv_obj_t *sub_lbl = lv_label_create(tile);
    lv_label_set_text(sub_lbl, sub_buf);
    lv_obj_set_style_text_font(sub_lbl, &anuphan_12, 0);
    lv_obj_set_style_text_color(sub_lbl, VD_MENU_COLOR_SUBTITLE, 0);
    lv_obj_remove_flag(sub_lbl, LV_OBJ_FLAG_CLICKABLE); // ★ จุดแก้ที่ 1: ปลดคลิกบนข้อความเพื่อให้แตะติดทั้งการ์ด

    if (click_cb) {
        lv_obj_add_event_cb(tile, click_cb, LV_EVENT_CLICKED, user_data);
    }

    return tile;
}

/* สไตล์ร่วมของแถวไฟล์ — สร้างครั้งเดียวใช้ทุกแถว
 * ลด lv_obj_set_style_*() จาก ~25 ครั้ง/แถว เหลือ lv_obj_add_style() 2 ครั้ง */
static lv_style_t s_fr_row, s_fr_row_pr, s_fr_ico, s_fr_name, s_fr_size;
static bool       s_fr_ready = false;

static void _file_row_styles_init(void)
{
    if (s_fr_ready) return;
    s_fr_ready = true;

    lv_style_init(&s_fr_row);
    lv_style_set_width(&s_fr_row, lv_pct(100));
    lv_style_set_height(&s_fr_row, 40);
    lv_style_set_pad_hor(&s_fr_row, 10);
    lv_style_set_pad_ver(&s_fr_row, 0);
    lv_style_set_radius(&s_fr_row, 8);
    lv_style_set_bg_opa(&s_fr_row, LV_OPA_TRANSP);
    lv_style_set_bg_color(&s_fr_row, lv_color_hex(0xF1F5F9));
    lv_style_set_layout(&s_fr_row, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&s_fr_row, LV_FLEX_FLOW_ROW);
    lv_style_set_flex_main_place(&s_fr_row, LV_FLEX_ALIGN_START);
    lv_style_set_flex_cross_place(&s_fr_row, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_track_place(&s_fr_row, LV_FLEX_ALIGN_CENTER);
    lv_style_set_pad_column(&s_fr_row, 12);
    lv_style_set_border_side(&s_fr_row, LV_BORDER_SIDE_TOP);
    lv_style_set_border_width(&s_fr_row, 1);
    lv_style_set_border_color(&s_fr_row, lv_color_hex(0xF3F4F6));

    lv_style_init(&s_fr_row_pr);
    lv_style_set_bg_color(&s_fr_row_pr, lv_color_hex(0xF8FAFC));
    lv_style_set_bg_opa(&s_fr_row_pr, LV_OPA_COVER);

    lv_style_init(&s_fr_ico);
    lv_style_set_text_font(&s_fr_ico, &font_awesome_12);
    lv_style_set_text_color(&s_fr_ico, VD_MENU_COLOR_SUBTITLE);

    lv_style_init(&s_fr_name);
    lv_style_set_text_font(&s_fr_name, &anuphan_med_14);
    lv_style_set_text_color(&s_fr_name, VD_MENU_COLOR_TITLE);
    lv_style_set_flex_grow(&s_fr_name, 1);
    lv_style_set_pad_top(&s_fr_name, 2);

    lv_style_init(&s_fr_size);
    lv_style_set_text_font(&s_fr_size, &anuphan_12);
    lv_style_set_text_color(&s_fr_size, lv_color_hex(0x9CA3AF));
}

/* ★ จุดแก้ที่ 5: แถวรายการไฟล์ (ตัด Subtitle ออก แสดงเฉพาะ Title และขนาดทางขวา) */
lv_obj_t *vd_menu_file_row_create(lv_obj_t *parent,
                                  const char *name,
                                  const char *meta_text,
                                  const char *size_text,
                                  bool is_directory,
                                  bool is_selected,
                                  lv_event_cb_t click_cb,
                                  lv_event_cb_t long_press_cb,
                                  void *user_data) {
    if (!parent) return NULL;
    (void)meta_text;

    _file_row_styles_init();

    lv_obj_t *row = lv_button_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, &s_fr_row,    0);
    lv_obj_add_style(row, &s_fr_row_pr, LV_STATE_PRESSED);

    if (is_selected)                       lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    if (lv_obj_get_child_count(parent) == 1) lv_obj_set_style_border_width(row, 0, 0); /* แถวแรกไม่มีเส้นคั่น */

    lv_obj_t *ico = lv_label_create(row);
    lv_label_set_text(ico, is_directory ? "\uf07b" : "\uf15b");
    lv_obj_add_style(ico, &s_fr_ico, 0);

    lv_obj_t *name_lbl = lv_label_create(row);
    lv_label_set_text(name_lbl, name ? name : "ชื่อไฟล์");
    lv_obj_add_style(name_lbl, &s_fr_name, 0);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);

    if (size_text && size_text[0]) {
        lv_obj_t *sz_lbl = lv_label_create(row);
        lv_label_set_text(sz_lbl, size_text);
        lv_obj_add_style(sz_lbl, &s_fr_size, 0);
    }

    if (click_cb)      lv_obj_add_event_cb(row, click_cb,      LV_EVENT_CLICKED,      user_data);
    if (long_press_cb) lv_obj_add_event_cb(row, long_press_cb, LV_EVENT_LONG_PRESSED, user_data);
    return row;
}

/* ════════════════════════════════════════════════════════════
   ★ จุดแก้ที่ 6: กล่องเมนูลอยเมื่อกดค้าง (Dynamic Context Menu)
   ════════════════════════════════════════════════════════════ */

static void _ctx_menu_item_click_cb(lv_event_t *e) {
    _vd_action_click_ctx_t *act_ctx = (_vd_action_click_ctx_t *)lv_event_get_user_data(e);
    if (!act_ctx || !act_ctx->files_ctx) return;

    _vd_files_ctx_t *fctx = act_ctx->files_ctx;
    uint32_t aid = act_ctx->action_id;

    if (fctx->cbs.on_action_click) {
        fctx->cbs.on_action_click(
            aid,
            fctx->current_drive_id,
            act_ctx->target_full_path,
            &act_ctx->item_copy,
            fctx->user_data
        );
    }

    // ปิดเมนูลอย
    if (act_ctx->overlay_obj) {
        lv_obj_delete_async(act_ctx->overlay_obj);
    }
}

static void _ctx_menu_overlay_delete_cb(lv_event_t *e) {
    _vd_action_click_ctx_t *act_ctx = (_vd_action_click_ctx_t *)lv_event_get_user_data(e);
    if (act_ctx) {
        if (act_ctx->item_copy.name) free(act_ctx->item_copy.name);
        if (act_ctx->target_full_path) free(act_ctx->target_full_path);
        free(act_ctx);
    }
}

static void _ctx_menu_backdrop_click_cb(lv_event_t *e) {
    lv_obj_t *overlay = lv_event_get_target(e);
    lv_obj_delete_async(overlay);
}

/* ★ แสดง Context Menu ที่ตำแหน่งปลาย Pointer พร้อมล็อกแนวตัวอักษรให้ชิดซ้ายตรงกัน */
static void _vd_files_show_context_menu(_vd_files_ctx_t *ctx, vd_file_item_t *item, const lv_point_t *pt) {
    if (!ctx || !item || !ctx->cbs.actions || ctx->cbs.action_count == 0) return;

    // 1. Overlay คลุมเต็มจอเพื่อดักคลิกปิดเมื่อแตะข้างนอก
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(overlay, true);
    lv_obj_add_event_cb(overlay, _ctx_menu_backdrop_click_cb, LV_EVENT_CLICKED, NULL);

    // 2. กล่องเมนูลอยสีขาว
    lv_obj_t *menu_box = lv_obj_create(overlay);
    lv_obj_remove_style_all(menu_box);
    lv_obj_set_size(menu_box, 190, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(menu_box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(menu_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(menu_box, 1, 0);
    lv_obj_set_style_border_color(menu_box, VD_MODAL_COLOR_BORDER, 0);
    lv_obj_set_style_radius(menu_box, 12, 0);
    lv_obj_set_style_pad_all(menu_box, 5, 0);
    lv_obj_set_style_shadow_width(menu_box, 18, 0);
    lv_obj_set_style_shadow_opa(menu_box, LV_OPA_20, 0);
    lv_obj_set_flex_flow(menu_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(menu_box, 1, 0);

    // ★ จุดแก้ที่ 2: คำนวณพิกัดให้กล่องโผล่ที่ปลาย Pointer + ป้องกันกล่องล้นขอบจอ
    int32_t screen_w = lv_obj_get_width(lv_layer_top());
    int32_t screen_h = lv_obj_get_height(lv_layer_top());
    if (screen_w <= 0) screen_w = 1024;
    if (screen_h <= 0) screen_h = 600;

    int32_t menu_w = 190;
    int32_t menu_h = (ctx->cbs.action_count * 31) + 12; // ความสูงโดยประมาณตามจำนวนปุ่ม

    int32_t x = pt ? pt->x : (screen_w / 2 - menu_w / 2);
    int32_t y = pt ? pt->y : (screen_h / 2 - menu_h / 2);

    // ป้องกันล้นขอบขวาและขอบล่าง
    if (x + menu_w > screen_w - 8) {
        x = screen_w - menu_w - 8;
    }
    if (x < 8) x = 8;

    if (y + menu_h > screen_h - 8) {
        y = screen_h - menu_h - 8;
    }
    if (y < 8) y = 8;

    lv_obj_set_pos(menu_box, x, y);

    char full_p[VD_FILE_MAX_PATH_LEN + 1];
    snprintf(full_p, sizeof(full_p), "/%s%s%s%s",
             ctx->current_drive_id,
             ctx->current_path,
             (ctx->current_path[strlen(ctx->current_path) - 1] == '/') ? "" : "/",
             item->name);

    for (uint32_t i = 0; i < ctx->cbs.action_count; i++) {
        const vd_file_action_t *act = &ctx->cbs.actions[i];

        lv_obj_t *btn = lv_button_create(menu_box);
        lv_obj_remove_style_all(btn);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 30);
        lv_obj_set_style_pad_hor(btn, 10, 0);
        lv_obj_set_style_pad_ver(btn, 0, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xF1F5F9), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(btn, 10, 0);
        lv_obj_set_clickable(btn, true);

        // ★ จุดแก้ที่ 1: ล็อกความกว้างไอคอน 18px และจัดกึ่งกลาง เพื่อให้ข้อความแถวถัดไปชิดซ้ายตรงกันเป๊ะ
        lv_obj_t *ic = lv_label_create(btn);
        lv_label_set_text(ic, act->icon ? act->icon : "");
        lv_obj_set_width(ic, 18);
        lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(ic, &font_awesome_12, 0);
        lv_obj_set_style_text_color(ic, act->is_danger ? lv_color_hex(0xDC2626) : VD_MENU_COLOR_SUBTITLE, 0);

        // ข้อความคำสั่ง
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, act->title ? act->title : "");
        lv_obj_set_style_text_font(lbl, &anuphan_med_14, 0);
        lv_obj_set_style_text_color(lbl, act->is_danger ? lv_color_hex(0xDC2626) : VD_MENU_COLOR_TITLE, 0);
        lv_obj_set_style_pad_top(lbl, 2, 0);

        _vd_action_click_ctx_t *act_ctx = (_vd_action_click_ctx_t *)malloc(sizeof(_vd_action_click_ctx_t));
        if (act_ctx) {
            act_ctx->files_ctx = ctx;
            act_ctx->item_copy.name = strdup(item->name);
            act_ctx->item_copy.type = item->type;
            act_ctx->item_copy.size_kb = item->size_kb;
            act_ctx->target_full_path = strdup(full_p);
            act_ctx->action_id = act->action_id;
            act_ctx->overlay_obj = overlay;

            lv_obj_add_event_cb(btn, _ctx_menu_item_click_cb, LV_EVENT_CLICKED, act_ctx);
            lv_obj_add_event_cb(btn, _ctx_menu_overlay_delete_cb, LV_EVENT_DELETE, act_ctx);
        }
    }
}

/* Event เมื่อกดค้างไฟล์/โฟลเดอร์: ดึงพิกัดจาก Input Device ปัจจุบัน */
static void _file_item_long_press_cb(lv_event_t *e) {
    _vd_files_ctx_t *ctx = (_vd_files_ctx_t *)lv_event_get_user_data(e);
    vd_file_item_t *item = (vd_file_item_t *)lv_obj_get_user_data(lv_event_get_target(e));
    if (!ctx || !item) return;

    // ★ อ่านพิกัด x, y ของจุดสัมผัส/เคอร์เซอร์ขณะกดค้าง
    lv_point_t point = {0, 0};
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, &point);
    }

    _vd_files_show_context_menu(ctx, item, &point);
}

static void _file_item_click_cb(lv_event_t *e);
static void _file_item_long_press_cb(lv_event_t *e);
static void _vd_file_format_size(uint32_t size_kb, char *out_buf, size_t buf_size);

#define VD_FILE_ROWS_PER_TICK 24      /* ~24 แถว/รอบ ใช้เวลาราว 20-30 ms */

static void _file_rows_fill_cb(lv_timer_t *t)
{
    _vd_files_ctx_t *ctx = (_vd_files_ctx_t *)lv_timer_get_user_data(t);
    if (!ctx || !ctx->file_list_box) { lv_timer_delete(t); return; }

    if (ctx->rendered_rows < ctx->page_start) ctx->rendered_rows = ctx->page_start; /* ← เพิ่ม */

    uint32_t end = ctx->rendered_rows + VD_FILE_ROWS_PER_TICK;
    if (end > ctx->page_end) end = ctx->page_end;                                   /* ← แก้ */

    for (; ctx->rendered_rows < end; ctx->rendered_rows++) {
        vd_file_item_t *item = &ctx->cached_items[ctx->rendered_rows];
        char size_buf[32] = {0};
        if (item->type != VD_FILE_TYPE_DIR)
            _vd_file_format_size(item->size_kb, size_buf, sizeof(size_buf));

        lv_obj_t *row = vd_menu_file_row_create(
            ctx->file_list_box, item->name, NULL,
            (item->type == VD_FILE_TYPE_DIR) ? NULL : size_buf,
            (item->type == VD_FILE_TYPE_DIR), false,
            _file_item_click_cb, _file_item_long_press_cb, ctx);
        if (row) lv_obj_set_user_data(row, item);
    }

    if (ctx->rendered_rows >= ctx->page_end) {                                      /* ← แก้ */
        ctx->fill_timer = NULL;
        lv_timer_delete(t);
    }
}

static void _render_files_view(_vd_files_ctx_t *ctx);

static void _pager_btn_cb(lv_event_t *e)
{
    _vd_files_ctx_t *ctx = (_vd_files_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;

    /* +1 / -1 ฝากมากับ user_data ของปุ่ม */
    intptr_t step = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    uint32_t pages = (ctx->cached_item_count + VD_FILE_ITEMS_PER_PAGE - 1)
                   / VD_FILE_ITEMS_PER_PAGE;
    if (pages == 0) pages = 1;

    if (step < 0 && ctx->page == 0)         return;
    if (step > 0 && ctx->page + 1 >= pages) return;

    ctx->page = (uint32_t)((int32_t)ctx->page + (int32_t)step);
    lv_obj_scroll_to_y(ctx->file_list_box, 0, LV_ANIM_OFF);   /* ขึ้นบนสุดของหน้าใหม่ */
    _render_files_view(ctx);
}

/* Event เมื่อแตะปุ่มใน Breadcrumb */
static void _breadcrumb_btn_cb(lv_event_t *e) {
    _vd_files_ctx_t *ctx = (_vd_files_ctx_t *)lv_event_get_user_data(e);
    const char *target_path = (const char *)lv_obj_get_user_data(lv_event_get_target(e));

    if (!target_path) {
        if (ctx->current_drive_id) { free(ctx->current_drive_id); ctx->current_drive_id = NULL; }
        if (ctx->current_drive_name) { free(ctx->current_drive_name); ctx->current_drive_name = NULL; }
        ctx->current_path[0] = '\0';
    } else {
        strncpy(ctx->current_path, target_path, VD_FILE_MAX_PATH_LEN);
        ctx->current_path[VD_FILE_MAX_PATH_LEN] = '\0';
    }

    ctx->page = 0;

    _render_files_view(ctx);
}

/* Event เมื่อเลือกไดรฟ์ */
static void _drive_tile_click_cb(lv_event_t *e) {
    _vd_files_ctx_t *ctx = (_vd_files_ctx_t *)lv_event_get_user_data(e);
    vd_drive_info_t *d = (vd_drive_info_t *)lv_obj_get_user_data(lv_event_get_target(e));
    if (!ctx || !d) return;

    if (ctx->current_drive_id) free(ctx->current_drive_id);
    if (ctx->current_drive_name) free(ctx->current_drive_name);

    ctx->current_drive_id = strdup(d->id);
    ctx->current_drive_name = strdup(d->name);
    strcpy(ctx->current_path, "/");

    ctx->page = 0;

    _render_files_view(ctx);
}

/* Event เมื่อแตะไฟล์/โฟลเดอร์ */
static void _file_item_click_cb(lv_event_t *e) {
    _vd_files_ctx_t *ctx = (_vd_files_ctx_t *)lv_event_get_user_data(e);
    vd_file_item_t *item = (vd_file_item_t *)lv_obj_get_user_data(lv_event_get_target(e));
    if (!ctx || !item) return;

    if (item->type == VD_FILE_TYPE_DIR) {
        size_t cur_len = strlen(ctx->current_path);
        if (cur_len == 1 && ctx->current_path[0] == '/') {
            snprintf(ctx->current_path, VD_FILE_MAX_PATH_LEN, "/%s", item->name);
        } else {
            snprintf(ctx->current_path + cur_len, VD_FILE_MAX_PATH_LEN - cur_len, "/%s", item->name);
        }
        ctx->page = 0;
        _render_files_view(ctx);
    } else {
        if (ctx->cbs.on_file_click) {
            char full_path[VD_FILE_MAX_PATH_LEN + 1];
            snprintf(full_path, sizeof(full_path), "/%s%s%s%s",
                     ctx->current_drive_id,
                     ctx->current_path,
                     (ctx->current_path[strlen(ctx->current_path) - 1] == '/') ? "" : "/",
                     item->name);
            ctx->cbs.on_file_click(ctx->current_drive_id, full_path, item, ctx->user_data);
        }
    }
}

/* Event กดสร้างโฟลเดอร์ใหม่ */
static void _btn_new_dir_cb(lv_event_t *e) {
    _vd_files_ctx_t *ctx = (_vd_files_ctx_t *)lv_event_get_user_data(e);
    if (ctx && ctx->cbs.on_new_folder) {
        ctx->cbs.on_new_folder(ctx->current_drive_id, ctx->current_path, ctx->user_data);
    }
}

/* ฟังก์ชันหลักในการวาด UI ตาม State ปัจจุบัน */
static void _render_files_view(_vd_files_ctx_t *ctx) {
    if (!ctx) return;

    // ── มุมมองที่ 1: หน้าเลือกไดรฟ์ (Drive Grid) ──
    if (!ctx->current_drive_id) {
        if (ctx->fill_timer) { lv_timer_delete(ctx->fill_timer); ctx->fill_timer = NULL; }

        lv_obj_set_hidden(ctx->drive_grid_box, false);
        lv_obj_set_hidden(ctx->browser_box, true);
        lv_obj_clean(ctx->drive_grid_box);

        _vd_files_clear_drives_cache(ctx);
        _vd_files_clear_items_cache(ctx);

        if (ctx->cbs.get_drives) {
            ctx->cbs.get_drives(&ctx->cached_drives, &ctx->cached_drive_count, ctx->user_data);
        }

        for (uint32_t i = 0; i < ctx->cached_drive_count; i++) {
            vd_drive_info_t *d = &ctx->cached_drives[i];
            lv_obj_t *tile = vd_menu_file_drive_tile_create(
                ctx->drive_grid_box,
                d->name,
                d->used_mb,
                d->total_mb,
                _drive_tile_click_cb,
                ctx
            );
            lv_obj_set_user_data(tile, d);
        }
        return;
    }

    // ── มุมมองที่ 2: หน้าดูไฟล์ในโฟลเดอร์ (Browser View) ──
    lv_obj_set_hidden(ctx->drive_grid_box, true);
    lv_obj_set_hidden(ctx->browser_box, false);

    // ★ จุดแก้ที่ 4: Breadcrumb Navigation จัดกึ่งกลางแนวตั้ง ไม่ลอยสูง
    lv_obj_clean(ctx->breadcrumb_box);

    lv_obj_t *b_root = lv_button_create(ctx->breadcrumb_box);
    lv_obj_remove_style_all(b_root);
    lv_obj_set_height(b_root, 28);
    lv_obj_set_style_pad_hor(b_root, 4, 0);
    lv_obj_set_flex_flow(b_root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b_root, 6, 0);
    lv_obj_set_clickable(b_root, true);
    lv_obj_set_user_data(b_root, NULL);
    lv_obj_add_event_cb(b_root, _breadcrumb_btn_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *ic_root = lv_label_create(b_root);
    lv_label_set_text(ic_root, "\uf0a0");
    lv_obj_set_style_text_font(ic_root, &font_awesome_12, 0);
    lv_obj_set_style_text_color(ic_root, VD_MENU_COLOR_SUBTITLE, 0);

    lv_obj_t *lbl_root = lv_label_create(b_root);
    lv_label_set_text(lbl_root, "ที่จัดเก็บ");
    lv_obj_set_style_text_font(lbl_root, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(lbl_root, VD_MENU_COLOR_SUBTITLE, 0);
    lv_obj_set_style_pad_top(lbl_root, 2, 0);

    lv_obj_t *sep = lv_label_create(ctx->breadcrumb_box);
    lv_label_set_text(sep, ">");
    lv_obj_set_style_text_font(sep, &anuphan_12, 0);
    lv_obj_set_style_text_color(sep, lv_color_hex(0x9CA3AF), 0);

    bool is_drive_root = (strcmp(ctx->current_path, "/") == 0 || strlen(ctx->current_path) == 0);
    lv_obj_t *b_drv = lv_button_create(ctx->breadcrumb_box);
    lv_obj_remove_style_all(b_drv);
    lv_obj_set_height(b_drv, 28);
    lv_obj_set_style_pad_hor(b_drv, 4, 0);
    lv_obj_set_flex_flow(b_drv, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b_drv, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_clickable(b_drv, !is_drive_root);
    lv_obj_set_user_data(b_drv, (void *)"/");
    lv_obj_add_event_cb(b_drv, _breadcrumb_btn_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *lbl_drv = lv_label_create(b_drv);
    lv_label_set_text(lbl_drv, ctx->current_drive_name);
    lv_obj_set_style_text_font(lbl_drv, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(lbl_drv, is_drive_root ? VD_MENU_COLOR_TITLE : VD_MENU_COLOR_SUBTITLE, 0);
    lv_obj_set_style_pad_top(lbl_drv, 2, 0);

    if (!is_drive_root) {
        char temp_path[VD_FILE_MAX_PATH_LEN + 1];
        snprintf(temp_path, sizeof(temp_path), "%s", ctx->current_path);

        char accumulated_path[VD_FILE_MAX_PATH_LEN + 1] = "";

        char *token = strtok(temp_path, "/");
        while (token != NULL) {
            strcat(accumulated_path, "/");
            strcat(accumulated_path, token);

            lv_obj_t *s_arrow = lv_label_create(ctx->breadcrumb_box);
            lv_label_set_text(s_arrow, ">");
            lv_obj_set_style_text_font(s_arrow, &anuphan_12, 0);
            lv_obj_set_style_text_color(s_arrow, lv_color_hex(0x9CA3AF), 0);

            lv_obj_t *b_seg = lv_button_create(ctx->breadcrumb_box);
            lv_obj_remove_style_all(b_seg);
            lv_obj_set_height(b_seg, 28);
            lv_obj_set_style_pad_hor(b_seg, 4, 0);
            lv_obj_set_flex_flow(b_seg, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(b_seg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            char *seg_path_dyn = strdup(accumulated_path);
            lv_obj_set_user_data(b_seg, seg_path_dyn);
            lv_obj_add_event_cb(b_seg, _breadcrumb_btn_cb, LV_EVENT_CLICKED, ctx);
            lv_obj_add_event_cb(b_seg, _vd_menu_btn_group_delete_cb, LV_EVENT_DELETE, seg_path_dyn);

            bool is_leaf = (strcmp(accumulated_path, ctx->current_path) == 0);
            lv_obj_t *lbl_seg = lv_label_create(b_seg);
            lv_label_set_text(lbl_seg, token);
            lv_obj_set_style_text_font(lbl_seg, &anuphan_med_14, 0);
            lv_obj_set_style_text_color(lbl_seg, is_leaf ? VD_MENU_COLOR_TITLE : VD_MENU_COLOR_SUBTITLE, 0);
            lv_obj_set_style_pad_top(lbl_seg, 2, 0);

            token = strtok(NULL, "/");
        }
    }

    // 2. ดึงรายการไฟล์
    lv_obj_clean(ctx->file_list_box);
    _vd_files_clear_items_cache(ctx);

    if (ctx->cbs.list_dir) {
        ctx->cbs.list_dir(ctx->current_drive_id, ctx->current_path, &ctx->cached_items, &ctx->cached_item_count, ctx->user_data);
    }

    if (ctx->cached_item_count > VD_FILE_MAX_DIR_ITEMS) {
        ctx->cached_item_count = VD_FILE_MAX_DIR_ITEMS;
    }

    if (ctx->cached_item_count == 0) {
        lv_obj_t *empty = lv_label_create(ctx->file_list_box);
        lv_label_set_text(empty, "โฟลเดอร์นี้ว่างเปล่า");
        lv_obj_set_style_text_font(empty, &anuphan_med_14, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x9CA3AF), 0);
        lv_obj_set_style_pad_ver(empty, 30, 0);
        lv_obj_center(empty);
        return;
    }

    // เรนเดอร์รายการแถวไฟล์ (ไม่มี Subtitle, มี Long Press Callback)
    /*
    for (uint32_t i = 0; i < ctx->cached_item_count; i++) {
        vd_file_item_t *item = &ctx->cached_items[i];
        char size_buf[32] = {0};

        if (item->type != VD_FILE_TYPE_DIR) {
            _vd_file_format_size(item->size_kb, size_buf, sizeof(size_buf));
        }

        lv_obj_t *row = vd_menu_file_row_create(
            ctx->file_list_box,
            item->name,
            NULL, // ไม่มี Subtitle
            (item->type == VD_FILE_TYPE_DIR) ? NULL : size_buf,
            (item->type == VD_FILE_TYPE_DIR),
            false,
            _file_item_click_cb,
            _file_item_long_press_cb, // ผูก Long Press Callback
            ctx
        );
        lv_obj_set_user_data(row, item);
    }*/

        /* ── คำนวณว่าหน้านี้ครอบคลุม index ไหนบ้าง ── */
    uint32_t total = ctx->cached_item_count;
    uint32_t pages = (total + VD_FILE_ITEMS_PER_PAGE - 1) / VD_FILE_ITEMS_PER_PAGE;
    if (pages == 0) pages = 1;
    if (ctx->page >= pages) ctx->page = pages - 1;      /* กันหน้าค้างเกิน หลังรีเฟรช */

    ctx->page_start = ctx->page * VD_FILE_ITEMS_PER_PAGE;
    ctx->page_end   = ctx->page_start + VD_FILE_ITEMS_PER_PAGE;
    if (ctx->page_end > total) ctx->page_end = total;

    if (pages > 1) {
        lv_obj_remove_flag(ctx->pager_box, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(ctx->pager_label, "หน้า %lu/%lu  (%lu-%lu จาก %lu)",
                              (unsigned long)(ctx->page + 1), (unsigned long)pages,
                              (unsigned long)(ctx->page_start + 1),
                              (unsigned long)ctx->page_end, (unsigned long)total);
        lv_obj_set_style_opa(ctx->pager_prev, ctx->page == 0 ? LV_OPA_30 : LV_OPA_COVER, 0);
        lv_obj_set_style_opa(ctx->pager_next,
                             ctx->page + 1 >= pages ? LV_OPA_30 : LV_OPA_COVER, 0);
    } else {
        lv_obj_add_flag(ctx->pager_box, LV_OBJ_FLAG_HIDDEN);
    }

    /* ── ทยอยสร้างแถวของหน้านี้ ── */
    if (ctx->fill_timer) { lv_timer_delete(ctx->fill_timer); ctx->fill_timer = NULL; }
    ctx->rendered_rows = ctx->page_start;
    ctx->fill_timer = lv_timer_create(_file_rows_fill_cb, 10, ctx);
    lv_timer_ready(ctx->fill_timer);
}

/* รีเฟรชหน้าไฟล์โดย "คงตำแหน่งเดิม" ไว้
 *  back_to_drive_list = true  -> บังคับถอยกลับไปหน้าเลือกไดรฟ์
 *                               (ใช้ตอนถอด USB ขณะอยู่ในไดรฟ์นั้น)
 *  ⚠️ ต้องเรียกบนเธรด LVGL และ "ห้าม" เรียกจากข้างใน event callback
 *     (ให้ผ่าน lv_async_call เสมอ) เพราะมันลบ object ที่กำลัง dispatch อยู่ */
void vd_menu_page_files_refresh(bool back_to_drive_list)
{
    _vd_files_ctx_t *ctx = s_active_files_ctx;
    if (!ctx) return;                       /* ไม่ได้เปิดหน้านี้อยู่ ไม่ต้องทำอะไร */

    if (back_to_drive_list) {
        if (ctx->current_drive_id)   { free(ctx->current_drive_id);   ctx->current_drive_id = NULL; }
        if (ctx->current_drive_name) { free(ctx->current_drive_name); ctx->current_drive_name = NULL; }
        ctx->current_path[0] = '\0';
    }

    lv_indev_reset(NULL, NULL);             /* ตัดการอ้างอิง object เก่าจากระบบสัมผัส */
    _render_files_view(ctx);
}

bool vd_menu_page_files_is_open(void) { return s_active_files_ctx != NULL; }

/* Event คืนแรมทั้งหมดเมื่อหน้าจอถูกทำลาย */
static void _vd_files_page_cleanup_cb(lv_event_t *e) {
    _vd_files_ctx_t *ctx = (_vd_files_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;

    if (ctx->fill_timer) { lv_timer_delete(ctx->fill_timer); ctx->fill_timer = NULL; }

    if (ctx->current_drive_id) free(ctx->current_drive_id);
    if (ctx->current_drive_name) free(ctx->current_drive_name);
    if (ctx->current_path) free(ctx->current_path);

    if (s_active_files_ctx == ctx) s_active_files_ctx = NULL;   /* ← เพิ่ม */

    _vd_files_clear_drives_cache(ctx);
    _vd_files_clear_items_cache(ctx);

    free(ctx);
}

lv_obj_t *vd_menu_page_files_create(vd_menu_content_t *content,
                                    const vd_file_browser_cbs_t *cbs,
                                    void *user_data) {
    if (!content) return NULL;

    vd_menu_content_add_title(content, "เรียกดูไฟล์");

    vd_menu_sub_frame_t *sub_frame = vd_menu_sub_frame_create(content, NULL);
    lv_obj_t *card = vd_menu_sub_frame_get_card(sub_frame);

    _vd_files_ctx_t *ctx = (_vd_files_ctx_t *)calloc(1, sizeof(_vd_files_ctx_t));
    if (!ctx) return card;

    if (cbs) ctx->cbs = *cbs;
    ctx->user_data = user_data;

    ctx->current_path = (char *)malloc(VD_FILE_MAX_PATH_LEN + 1);
    ctx->current_path[0] = '\0';

    s_active_files_ctx = ctx;                                   /* ← เพิ่ม */

    lv_obj_add_event_cb(card, _vd_files_page_cleanup_cb, LV_EVENT_DELETE, ctx);

    ctx->drive_grid_box = lv_obj_create(card);
    lv_obj_remove_style_all(ctx->drive_grid_box);
    lv_obj_set_size(ctx->drive_grid_box, lv_pct(100), LV_SIZE_CONTENT);
    //lv_obj_set_flex_flow(ctx->drive_grid_box, LV_FLEX_FLOW_ROW);
    //lv_obj_set_style_pad_column(ctx->drive_grid_box, 12, 0);

    lv_obj_set_flex_flow(ctx->drive_grid_box, LV_FLEX_FLOW_ROW_WRAP);  /* :3642 แก้ */
    lv_obj_set_style_pad_row(ctx->drive_grid_box, 12, 0);              /* เพิ่มใหม่ */

    ctx->browser_box = lv_obj_create(card);
    lv_obj_remove_style_all(ctx->browser_box);
    lv_obj_set_size(ctx->browser_box, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->browser_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->browser_box, 14, 0);
    lv_obj_add_flag(ctx->browser_box, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *top_bar = lv_obj_create(ctx->browser_box);
    lv_obj_remove_style_all(top_bar);
    lv_obj_set_size(top_bar, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // ★ จัด Breadcrumb ให้อยู่กึ่งกลางแนวตั้งเป๊ะๆ
    ctx->breadcrumb_box = lv_obj_create(top_bar);
    lv_obj_remove_style_all(ctx->breadcrumb_box);
    lv_obj_set_size(ctx->breadcrumb_box, LV_SIZE_CONTENT, 34);
    lv_obj_set_flex_flow(ctx->breadcrumb_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->breadcrumb_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctx->breadcrumb_box, 4, 0);

    lv_obj_t *btn_new_dir = lv_button_create(top_bar);
    lv_obj_remove_style_all(btn_new_dir);
    lv_obj_set_height(btn_new_dir, 34);
    lv_obj_set_style_pad_hor(btn_new_dir, 14, 0);
    lv_obj_set_style_bg_color(btn_new_dir, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn_new_dir, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn_new_dir, 1, 0);
    lv_obj_set_style_border_color(btn_new_dir, VD_MENU_COLOR_CARD_BORDER, 0);
    lv_obj_set_style_radius(btn_new_dir, 8, 0);
    lv_obj_set_flex_flow(btn_new_dir, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_new_dir, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_new_dir, 6, 0);
    lv_obj_set_clickable(btn_new_dir, true);
    lv_obj_add_event_cb(btn_new_dir, _btn_new_dir_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *ic_dir = lv_label_create(btn_new_dir);
    lv_label_set_text(ic_dir, "\uf07b");
    lv_obj_set_style_text_font(ic_dir, &font_awesome_12, 0);
    lv_obj_set_style_text_color(ic_dir, VD_MENU_COLOR_TITLE, 0);

    lv_obj_t *lbl_dir = lv_label_create(btn_new_dir);
    lv_label_set_text(lbl_dir, "โฟลเดอร์ใหม่");
    lv_obj_set_style_text_font(lbl_dir, &anuphan_med_14, 0);
    lv_obj_set_style_text_color(lbl_dir, VD_MENU_COLOR_TITLE, 0);

    ctx->file_list_box = lv_obj_create(ctx->browser_box);
    //lv_obj_remove_style_all(ctx->file_list_box);
    //lv_obj_set_size(ctx->file_list_box, lv_pct(100), LV_SIZE_CONTENT);
    //lv_obj_set_flex_flow(ctx->file_list_box, LV_FLEX_FLOW_COLUMN);

    lv_obj_remove_style_all(ctx->file_list_box);
    lv_obj_set_width(ctx->file_list_box, lv_pct(100));
    lv_obj_set_height(ctx->file_list_box, 380);          /* ← ความสูงคงที่ ปรับตามจอ */
    lv_obj_set_flex_flow(ctx->file_list_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(ctx->file_list_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ctx->file_list_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ctx->file_list_box, LV_SCROLLBAR_MODE_AUTO);

        /* ── แถบเปลี่ยนหน้า: ◀  หน้า 1/20 (รายการ 1-100 จาก 2000)  ▶ ── */
    ctx->pager_box = lv_obj_create(ctx->browser_box);
    lv_obj_remove_style_all(ctx->pager_box);
    lv_obj_set_size(ctx->pager_box, lv_pct(100), 40);
    lv_obj_set_flex_flow(ctx->pager_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->pager_box, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctx->pager_box, 16, 0);
    lv_obj_add_flag(ctx->pager_box, LV_OBJ_FLAG_HIDDEN);   /* โชว์เฉพาะตอนเกิน 1 หน้า */

    ctx->pager_prev = lv_button_create(ctx->pager_box);
    lv_obj_remove_style_all(ctx->pager_prev);
    lv_obj_set_size(ctx->pager_prev, 44, 32);
    lv_obj_set_style_radius(ctx->pager_prev, 8, 0);
    lv_obj_set_style_bg_opa(ctx->pager_prev, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ctx->pager_prev, lv_color_hex(0xF1F5F9), 0);
    lv_obj_set_user_data(ctx->pager_prev, (void *)(intptr_t)-1);
    lv_obj_add_event_cb(ctx->pager_prev, _pager_btn_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *lp = lv_label_create(ctx->pager_prev);
    lv_label_set_text(lp, "\uf053");                      /* fa-chevron-left */
    lv_obj_set_style_text_font(lp, &font_awesome_12, 0);
    lv_obj_center(lp);

    ctx->pager_label = lv_label_create(ctx->pager_box);
    lv_obj_set_style_text_font(ctx->pager_label, &anuphan_12, 0);
    lv_obj_set_style_text_color(ctx->pager_label, lv_color_hex(0x6B7280), 0);
    lv_label_set_text(ctx->pager_label, "");

    ctx->pager_next = lv_button_create(ctx->pager_box);
    lv_obj_remove_style_all(ctx->pager_next);
    lv_obj_set_size(ctx->pager_next, 44, 32);
    lv_obj_set_style_radius(ctx->pager_next, 8, 0);
    lv_obj_set_style_bg_opa(ctx->pager_next, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ctx->pager_next, lv_color_hex(0xF1F5F9), 0);
    lv_obj_set_user_data(ctx->pager_next, (void *)(intptr_t)1);
    lv_obj_add_event_cb(ctx->pager_next, _pager_btn_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *ln = lv_label_create(ctx->pager_next);
    lv_label_set_text(ln, "\uf054");                      /* fa-chevron-right */
    lv_obj_set_style_text_font(ln, &font_awesome_12, 0);
    lv_obj_center(ln);

    _render_files_view(ctx);
    return card;
}
