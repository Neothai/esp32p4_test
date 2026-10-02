#ifndef VD_MENU_H
#define VD_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* --- ค่าคงที่ประจำโมดูล (VD_MENU_*) --- */
#define VD_MENU_SIDEBAR_DEFAULT_WIDTH_PCT  26
#define VD_MENU_ITEM_HEIGHT               38
#define VD_MENU_ICON_WIDTH                24

#define VD_MENU_PAD_TOP                   18  // เพิ่มระยะห่างขอบบน
#define VD_MENU_PAD_LEFT                  14  // เพิ่มระยะห่างขอบซ้าย
#define VD_MENU_PAD_RIGHT                 12
#define VD_MENU_PAD_BOTTOM                14
#define VD_MENU_PAD_ROW                    3  // ระยะห่างระหว่างปุ่มในกลุ่ม

#define VD_MENU_COLOR_BG                  lv_color_hex(0xF8FAFC)
#define VD_MENU_COLOR_BORDER              lv_color_hex(0xE2E8F0)
#define VD_MENU_COLOR_DIVIDER             lv_color_hex(0xE2E8F0)
#define VD_MENU_COLOR_ACTIVE_BG           lv_color_hex(0xE2E8F0)
#define VD_MENU_COLOR_TEXT_NORMAL         lv_color_hex(0x475569)
#define VD_MENU_COLOR_TEXT_ACTIVE         lv_color_hex(0x0F172A)
#define VD_MENU_COLOR_EXIT_TEXT           lv_color_hex(0xEF4444) // สีแดงสำหรับปุ่มออก

#define VD_MENU_EXIT_ITEM_ID              0xFFFFFFFF

/* ── ค่าคงที่ฝั่ง Content & Sub-frame (VD_MENU_*) ── */
#define VD_MENU_COLOR_CONTENT_BG          lv_color_hex(0xF8FAF9)
#define VD_MENU_COLOR_CARD_BG             lv_color_hex(0xFFFFFF)
#define VD_MENU_COLOR_CARD_BORDER         lv_color_hex(0xE2E8F0)
#define VD_MENU_COLOR_ROW_DIVIDER         lv_color_hex(0xEAEFF5)
#define VD_MENU_COLOR_TITLE               lv_color_hex(0x0F172A)
#define VD_MENU_COLOR_SUBTITLE            lv_color_hex(0x64748B)
#define VD_MENU_COLOR_ACCENT              lv_color_hex(0x047857) // สีเขียวมรกตเน้นฝั่งขวา
#define VD_MENU_COLOR_BADGE_BG            lv_color_hex(0xF1F5F9)

/* ── สีกำหนดเฉพาะของระบบเครือข่าย ── */
#define VD_MENU_COLOR_NET_ONLINE_BG   lv_color_hex(0xECFDF5)
#define VD_MENU_COLOR_NET_ONLINE_TXT  lv_color_hex(0x047857)
#define VD_MENU_COLOR_NET_OFFLINE_BG  lv_color_hex(0xFEF2F2)
#define VD_MENU_COLOR_NET_OFFLINE_TXT lv_color_hex(0xDC2626)

/* ════════════════════════════════════════════════════════════
   ค่าคงที่สีประจำช่องจำหน่าย (Slot Status Colors)
   ════════════════════════════════════════════════════════════ */
/* 1. สถานะ: พร้อมขาย (OK - สีเขียว) */
#define VD_SLOT_COLOR_OK_BG             lv_color_hex(0xFFFFFF)
#define VD_SLOT_COLOR_OK_BORDER         lv_color_hex(0xE5E7EB)
#define VD_SLOT_COLOR_OK_BADGE_BG       lv_color_hex(0xECFDF5)
#define VD_SLOT_COLOR_OK_BADGE_TXT      lv_color_hex(0x047857)
#define VD_SLOT_COLOR_OK_DOT            lv_color_hex(0x10B981)

/* 2. สถานะ: สินค้าเหลือน้อย (Low Stock - สีเหลืองส้ม) */
#define VD_SLOT_COLOR_LOW_BG            lv_color_hex(0xFFFBEB)
#define VD_SLOT_COLOR_LOW_BORDER        lv_color_hex(0xFDE68A)
#define VD_SLOT_COLOR_LOW_BADGE_BG      lv_color_hex(0xFEF3C7)
#define VD_SLOT_COLOR_LOW_BADGE_TXT     lv_color_hex(0xB45309)
#define VD_SLOT_COLOR_LOW_DOT           lv_color_hex(0xF59E0B)

/* 3. สถานะ: ขายหมด (Out of Stock - สีแดง) */
#define VD_SLOT_COLOR_OUT_BG            lv_color_hex(0xFEF2F2)
#define VD_SLOT_COLOR_OUT_BORDER        lv_color_hex(0xFECACA)
#define VD_SLOT_COLOR_OUT_BADGE_BG      lv_color_hex(0xFEE2E2)
#define VD_SLOT_COLOR_OUT_BADGE_TXT     lv_color_hex(0xB91C1C)
#define VD_SLOT_COLOR_OUT_DOT           lv_color_hex(0xEF4444)

/* 4. สถานะ: ปิดขาย (Disabled - สีเทา) */
#define VD_SLOT_COLOR_OFF_BG            lv_color_hex(0xF8FAFC)
#define VD_SLOT_COLOR_OFF_BORDER        lv_color_hex(0xE2E8F0)
#define VD_SLOT_COLOR_OFF_BADGE_BG      lv_color_hex(0xF3F4F6)
#define VD_SLOT_COLOR_OFF_BADGE_TXT     lv_color_hex(0x6B7280)
#define VD_SLOT_COLOR_OFF_DOT           lv_color_hex(0x9CA3AF)

/* 5. สีข้อความและเส้นขอบทั่วไป */
#define VD_SLOT_COLOR_CODE_TXT          lv_color_hex(0x9CA3AF) // สีรหัสช่อง A1, B2
#define VD_SLOT_COLOR_PRICE_TXT         lv_color_hex(0x047857) // สีราคาขาย (เขียวมรกต)
#define VD_SLOT_COLOR_SELECTED_BORDER   lv_color_hex(0x047857) // สีกรอบเมื่อช่องถูกเลือก

/* ── สีประจำหน้าต่างลอยแก้ไขช่องจำหน่าย ── */
#define VD_MODAL_COLOR_BACKDROP         lv_color_hex(0x0F172A)
#define VD_MODAL_COLOR_BG               lv_color_hex(0xFFFFFF)
#define VD_MODAL_COLOR_BORDER           lv_color_hex(0xE5E7EB)
#define VD_MODAL_COLOR_DIVIDER          lv_color_hex(0xF3F4F6)
#define VD_MODAL_COLOR_BTN_PRIMARY      lv_color_hex(0x047857)
#define VD_MODAL_COLOR_BTN_PRIMARY_HOV  lv_color_hex(0x03634A)
#define VD_MODAL_COLOR_BTN_DISABLED     lv_color_hex(0xCBD5E1)

/* 5. สีข้อความและเส้นขอบทั่วไป */
#define VD_SLOT_COLOR_CODE_TXT          lv_color_hex(0x9CA3AF) // สีรหัสช่อง A1, B2
#define VD_SLOT_COLOR_PRICE_TXT         lv_color_hex(0x047857) // สีราคาขายปกติ (เขียวมรกต)
#define VD_SLOT_COLOR_PRICE_OLD_TXT     lv_color_hex(0x9CA3AF) // ★ สีราคาเดิมที่ถูกขีดฆ่า (เทา)
#define VD_SLOT_COLOR_PRICE_DISC_TXT    lv_color_hex(0xDC2626) // ★ สีราคาลดพิเศษ (แดง)
#define VD_SLOT_COLOR_SELECTED_BORDER   lv_color_hex(0x047857) // สีกรอบเมื่อช่องถูกเลือก

/* สถานะของแต่ละช่อง */
typedef enum {
    VD_SLOT_STATUS_OK = 0, // พร้อมขาย
    VD_SLOT_STATUS_LOW,    // เหลือน้อย
    VD_SLOT_STATUS_OUT,    // ขายหมด
    VD_SLOT_STATUS_OFF     // ปิดการขาย
} vd_slot_status_t;

/* ข้อมูลประจำแต่ละช่อง */
/* ข้อมูลประจำแต่ละช่องจำหน่าย */
typedef struct {
    char        code[8];       // รหัสช่อง เช่น "A1", "D6"
    char        product[128];  // ชื่อสินค้า
    char        img[256];      // พาธไฟล์รูปปก เช่น "/flash/สินค้า/ชาไทย.png" หรือ "0:/prod_img/..."
    uint32_t    price;         // ราคาเต็ม (บาท)
    bool        discount_on;   // เปิดใช้งานส่วนลดหรือไม่
    uint32_t    discount;      // ยอดส่วนลด (บาท)
    uint16_t    stock;         // สต็อกคงเหลือ
    uint16_t    capacity;      // ความจุเต็มช่อง
    uint16_t    low_threshold; // เกณฑ์เตือนเหลือน้อยเฉพาะช่อง
    bool        disabled;      // ปิดใช้งานช่องหรือไม่
} vd_slot_data_t;

/* Forward Declaration */
typedef struct vd_menu_sidebar_t   vd_menu_sidebar_t;
typedef struct vd_menu_content_t   vd_menu_content_t;
typedef struct vd_menu_sub_frame_t vd_menu_sub_frame_t;

/**
 * @brief ฟังก์ชัน Callback เมื่อผู้ใช้แตะปุ่มใน Sidebar Menu
 * @param sidebar พอยน์เตอร์โครงสร้าง Sidebar
 * @param item_id หมายเลข ID ประจำปุ่ม (หรือ VD_MENU_EXIT_ITEM_ID เมื่อเป็นปุ่มออก)
 * @param title ชื่อข้อความของเมนู
 * @param user_data ข้อมูลเสริมที่ผูกไว้
 */
typedef void (*vd_menu_cb_t)(vd_menu_sidebar_t *sidebar, uint32_t item_id, const char *title, void *user_data);


/* Callback เมื่อกดยืนยันบันทึกข้อมูลช่องจำหน่าย */
typedef void (*vd_slot_save_cb_t)(vd_slot_data_t *slot, void *user_data);

/**
 * @brief Callback สำหรับกำหนด Image Source ให้กับ Image Widget ด้วยตนเอง
 * @param img_obj วิดเจ็ต lv_image ที่ต้องตั้งค่า
 * @param src_path ข้อความพาธ หรือตัวระบุรูปภาพจากช่องกรอก
 * @param user_data ข้อมูลส่งต่อ
 */
typedef void (*vd_slot_image_set_cb_t)(lv_obj_t *img_obj, const char *src_path, void *user_data);

/* เพิ่มฟิลด์ on_set_image ลงในชุด Callback ของ Modal */
typedef struct {
    void (*on_save)(vd_slot_data_t *slot, void *user_data);
    void (*on_motor_test)(const char *slot_code, void *user_data);
    void (*on_close)(void *user_data);
    vd_slot_image_set_cb_t on_set_image; // ★ Callback กำหนดภาพด้วยตนเอง
} vd_slot_modal_cbs_t;

/* ════════════════════════════════════════════════════════════
   ระบบเรียกดูไฟล์แบบไดนามิก (Dynamic File Browser API)
   ════════════════════════════════════════════════════════════ */

#define VD_FILE_MAX_PATH_LEN    512 // ความยาว Path สูงสุด
#define VD_FILE_MAX_DIR_ITEMS   2000   // เดิม 256
#define VD_FILE_ITEMS_PER_PAGE  100    // ← เพิ่มใหม่: จำนวนแถวต่อหนึ่งหน้า

/* ประเภทไฟล์ */
typedef enum {
    VD_FILE_TYPE_DIR = 0, // โฟลเดอร์
    VD_FILE_TYPE_IMG,     // รูปภาพ (.png, .jpg)
    VD_FILE_TYPE_VID,     // วิดีโอ (.avi, .mp4)
    VD_FILE_TYPE_TXT,     // ข้อความ (.txt, .json, .log)
    VD_FILE_TYPE_FILE     // ไฟล์ไบนารี/ทั่วไป
} vd_file_type_t;

/* ข้อมูลไฟล์/โฟลเดอร์ 1 รายการ */
typedef struct {
    char          *name;      // ชื่อไฟล์/โฟลเดอร์ (จองแรมตามความยาวจริง)
    vd_file_type_t type;      // ชนิดไฟล์
    uint32_t       size_kb;   // ขนาดเป็น KB
} vd_file_item_t;

/* ข้อมูลไดรฟ์จัดเก็บ 1 ไดรฟ์ */
typedef struct {
    char     *id;        // เช่น "flash", "sd", "usb"
    char     *name;      // เช่น "หน่วยความจำภายใน", "การ์ด SD"
    uint32_t  used_mb;   // พื้นที่ใช้งานไปแล้ว (MB)
    uint32_t  total_mb;  // ความจุทั้งหมด (MB)
} vd_drive_info_t;

/* โครงสร้างตัวเลือกในเมนูกดค้าง (Context Menu Item) ที่ลงทะเบียนจากภายนอก */
typedef struct {
    uint32_t    action_id;  // ID อ้างอิงส่งกลับใน Callback
    const char *icon;       // รหัสไอคอน FontAwesome (เช่น "\uf06e", "\uf2ed")
    const char *title;      // ชื่อเมนู (เช่น "เปิดดู", "คัดลอก", "ลบ")
    bool        is_danger;  // true = สีแดง (เช่น เมนูลบ)
} vd_file_action_t;

/* ชุด Callback ให้ User Space ส่งข้อมูลเข้ามาในระบบ */
typedef struct {
    bool (*get_drives)(vd_drive_info_t **out_drives, uint32_t *out_count, void *user_data);
    bool (*list_dir)(const char *drive_id, const char *path, vd_file_item_t **out_items, uint32_t *out_count, void *user_data);
    void (*on_file_click)(const char *drive_id, const char *full_path, const vd_file_item_t *item, void *user_data);
    void (*on_new_folder)(const char *drive_id, const char *current_path, void *user_data);

    /* ★ เมนูกดค้าง (Context Menu) ที่ลงทะเบียนจากภายนอก */
    const vd_file_action_t *actions;       // อาร์เรย์คำสั่ง
    uint32_t                action_count;  // จำนวนคำสั่ง
    void (*on_action_click)(uint32_t action_id, const char *drive_id, const char *full_path, const vd_file_item_t *item, void *user_data);
} vd_file_browser_cbs_t;

/* --- ฟังก์ชันจัดการ Sidebar Menu --- */

/**
 * @brief สร้างแถบเมนูด้านข้าง (Sidebar Menu List)
 * @param parent Parent Object (เช่น menu_page)
 * @param width_px ความกว้างของ Sidebar (-1 หากต้องการใช้ค่าเริ่มต้น 26%)
 * @param height_px ความสูงของ Sidebar (-1 หากต้องการเต็มจอ 100%)
 * @param cb Callback ฟังก์ชันเมื่อแตะปุ่มเมนู
 * @param user_data พอยน์เตอร์ข้อมูลเพิ่มเติม
 * @return vd_menu_sidebar_t*
 */
vd_menu_sidebar_t *vd_menu_sidebar_list_create(lv_obj_t *parent, int32_t width_px, int32_t height_px,
                                              vd_menu_cb_t cb, void *user_data);

/**
 * @brief เพิ่มรายการปุ่มเมนูลงในส่วนรายการเลื่อน
 * @param sidebar พอยน์เตอร์โครงสร้าง Sidebar
 * @param id หมายเลขประจำปุ่มเมนู (ต้องไม่ซ้ำกัน)
 * @param icon ข้อความรหัสไอคอน (เช่น "\uf007")
 * @param title ข้อความชื่อเมนูภาษาไทย
 * @param is_active กำหนดให้เป็นสถานะเลือกอยู่เริ่มต้นหรือไม่
 * @return lv_obj_t* ปุ่มเมนูที่ถูกสร้าง
 */
lv_obj_t *vd_menu_sidebar_list_add_item(vd_menu_sidebar_t *sidebar, uint32_t id,
                                       const char *icon, const char *title, bool is_active);

/**
 * @brief เพิ่มเส้นคั่นระหว่างกลุ่มเมนู (Divider)
 * @param sidebar พอยน์เตอร์โครงสร้าง Sidebar
 */
void vd_menu_sidebar_list_add_divider(vd_menu_sidebar_t *sidebar);

/**
 * @brief สั่งเปลี่ยนสถานะปุ่มที่เลือก (Active State)
 * @param sidebar พอยน์เตอร์โครงสร้าง Sidebar
 * @param id หมายเลข ID ของปุ่มที่ต้องการให้ Active
 */
void vd_menu_sidebar_list_set_active(vd_menu_sidebar_t *sidebar, uint32_t id);

/**
 * @brief ดึงหมายเลข ID ของปุ่มที่กำลังเลือกอยู่ปัจจุบัน
 * @param sidebar พอยน์เตอร์โครงสร้าง Sidebar
 * @return uint32_t item_id (หรือ 0 หากไม่มีปุ่มใด Active)
 */
uint32_t vd_menu_sidebar_list_get_active(const vd_menu_sidebar_t *sidebar);

/**
 * @brief ตั้งค่าหรือเปิดใช้ปุ่มออกจากหน้าตั้งค่าที่ตรึงอยู่ล่างสุด
 * @param sidebar พอยน์เตอร์โครงสร้าง Sidebar
 * @param icon รหัสไอคอน (เช่น "\uf2f5" หรือ NULL เพื่อใช้ไอคอนออกเริ่มต้น)
 * @param title ชื่อปุ่ม (เช่น "ออกจากหน้าตั้งค่า")
 */
void vd_menu_sidebar_list_set_exit_btn(vd_menu_sidebar_t *sidebar, const char *icon, const char *title);

/**
 * @brief ทำลายและคืนหน่วยความจำของ Sidebar
 * @param sidebar พอยน์เตอร์โครงสร้าง Sidebar
 */
void vd_menu_sidebar_list_delete(vd_menu_sidebar_t *sidebar);

/* ════════════════════════════════════════════════════════════
   2. ฟังก์ชันจัดการพื้นที่ Content และ Sub-frame ด้านขวา
   ════════════════════════════════════════════════════════════ */

/**
 * @brief สร้างพื้นที่แสดงผลเนื้อหาหลักฝั่งขวา (Main Content Area)
 * @param parent หน้าต่างหลัก (เช่น menu_page)
 * @return vd_menu_content_t*
 */
vd_menu_content_t *vd_menu_content_create(lv_obj_t *parent);

/**
 * @brief สร้างหัวข้อหลักบนสุดของหน้า (Main Title เช่น "ตั้งค่าทั่วไป")
 * @param content โครงสร้างพื้นที่เนื้อหาฝั่งขวา
 * @param title_text ข้อความหัวข้อหลัก
 * @return lv_obj_t* Label ที่ถูกสร้าง
 */
lv_obj_t *vd_menu_content_add_title(vd_menu_content_t *content, const char *title_text);

/**
 * @brief สร้างการ์ดกรอบสีขาวสำหรับหมวดหมู่ย่อย (sub_frame) พร้อมหัวข้อการ์ด (frame_title)
 * @param content โครงสร้างพื้นที่เนื้อหาฝั่งขวา
 * @param frame_title ชื่อหมวดหมู่บนการ์ด (เช่น "ข้อมูลเครื่อง")
 * @return vd_menu_sub_frame_t*
 */
vd_menu_sub_frame_t *vd_menu_sub_frame_create(vd_menu_content_t *content, const char *frame_title);

/**
 * @brief สร้างแถวรายการตั้งค่า (sub_content) และคืนค่าคอนเทนเนอร์ฝั่งขวา (Slot อิสระ)
 * @param sub_frame การ์ดหมวดหมู่ที่ต้องการบรรจุแถวนี้
 * @param content_title ชื่อรายการตั้งค่า (เช่น "ชื่อเครื่อง")
 * @param content_subtitle คำอธิบายย่อยสีเทา (ใส่ NULL ได้หากไม่มี)
 * @return lv_obj_t* พอยน์เตอร์กล่องฝั่งขวา เพื่อนำ Widget ชนิดใดก็ได้มาสร้างต่อ
 */
lv_obj_t *vd_menu_sub_content_create(vd_menu_sub_frame_t *sub_frame,
                                     const char *content_title,
                                     const char *content_subtitle);

/* ── Convenience Helpers (สร้าง Widget ฝั่งขวาแบบบรรทัดเดียว) ── */

/**
 * @brief สร้างแถวพร้อมช่องกรอกข้อความ (Text Input Area)
 */
lv_obj_t *vd_menu_sub_content_add_text_input(vd_menu_sub_frame_t *sub_frame,
                                            const char *title,
                                            const char *subtitle,
                                            const char *default_text,
                                            uint32_t max_len);

/**
 * @brief สร้างแถวพร้อมป้ายข้อความค่าคงที่ (Static Value Badge เช่น Serial No. หรือเวอร์ชัน)
 */
lv_obj_t *vd_menu_sub_content_add_static_badge(vd_menu_sub_frame_t *sub_frame,
                                              const char *title,
                                              const char *subtitle,
                                              const char *badge_text);

/**
 * @brief สร้างแถวพร้อมสวิตช์ Toggle เปิด/ปิด
 */
lv_obj_t *vd_menu_sub_content_add_switch(vd_menu_sub_frame_t *sub_frame,
                                        const char *title,
                                        const char *subtitle,
                                        bool default_checked,
                                        lv_event_cb_t event_cb,
                                        void *user_data);

/**
 * @brief สร้างแถว Slider แนวนอนแบบเต็มความกว้าง 100% (Full Width)
 * @param sub_frame การ์ดเป้าหมายที่ต้องการบรรจุ
 * @param icon รหัสไอคอนนำหน้า (เช่น "\uf185" หรือส่ง NULL หากไม่ใช้)
 * @param title ชื่อการตั้งค่า (เช่น "ความสว่างหน้าจอ")
 * @param subtitle คำแนะนำด้านล่างแถบเลื่อน (ส่ง NULL หากไม่มี)
 * @param min ค่าต่ำสุด
 * @param max ค่าสูงสุด
 * @param default_val ค่าเริ่มต้น
 * @param unit หน่วยต่อท้ายตัวเลข (เช่น "%", " dB")
 * @param event_cb Callback เมื่อมีการเลื่อนปรับค่า
 * @return lv_obj_t* Slider Object
 */
lv_obj_t *vd_menu_sub_content_add_slider(vd_menu_sub_frame_t *sub_frame,
                                        const char *icon,
                                        const char *title,
                                        const char *subtitle,
                                        int32_t min,
                                        int32_t max,
                                        int32_t default_val,
                                        const char *unit,
                                        lv_event_cb_t event_cb);

/**
 * @brief สร้างแถวรายการพร้อมกลุ่มปุ่มตัวเลือก (Segmented Button Group) แบบเลือกได้ค่าเดียว
 * @param sub_frame การ์ดเป้าหมายที่ต้องการบรรจุ
 * @param title ชื่อหัวข้อการตั้งค่าฝั่งซ้าย (เช่น "ภาษาบนหน้าจอตั้งค่า")
 * @param subtitle คำอธิบายย่อยสีเทา (เช่น "ภาษาของเมนูสำหรับผู้ดูแลเครื่อง")
 * @param choices อาเรย์ข้อความปุ่มตัวเลือก (เช่น const char *langs[] = {"ไทย", "English"})
 * @param choice_count จำนวนปุ่มทั้งหมดในอาเรย์
 * @param default_index หมายเลข Index ของปุ่มที่ต้องการให้ถูกเลือกเริ่มต้น (เริ่มจาก 0)
 * @param event_cb Callback ฟังก์ชันเมื่อผู้ใช้กดเปลี่ยนตัวเลือก
 * @param user_data ข้อมูลเสริมที่ต้องการส่งไปพร้อม Callback
 * @return lv_obj_t* คอนเทนเนอร์หลักของกลุ่มปุ่ม
 */
lv_obj_t *vd_menu_sub_content_add_button_group(vd_menu_sub_frame_t *sub_frame,
                                              const char *title,
                                              const char *subtitle,
                                              const char *choices[],
                                              uint32_t choice_count,
                                              uint32_t default_index,
                                              lv_event_cb_t event_cb,
                                              void *user_data);

/**
 * @brief สร้างแถวเลือกช่วงเวลาเริ่มต้น (ค่าเริ่มต้น: แสดง ชม. และ นาที, Step นาทีละ 1)
 */
lv_obj_t *vd_menu_sub_content_add_time_range(vd_menu_sub_frame_t *sub_frame,
                                            const char *title,
                                            const char *subtitle,
                                            const char *default_start,
                                            const char *default_end,
                                            lv_event_cb_t cb,
                                            void *user_data);

/**
 * @brief กำหนดหน่วยเวลาที่ต้องการให้แสดงผล (ชั่วโมง, นาที, วินาที)
 * @param range_box อ็อบเจกต์ช่วงเวลาที่ได้จาก vd_menu_sub_content_add_time_range
 * @param show_hour เปิด/ปิด การแสดงผลหลักชั่วโมง
 * @param show_min  เปิด/ปิด การแสดงผลหลักนาที
 * @param show_sec  เปิด/ปิด การแสดงผลหลักวินาที
 */
void vd_menu_time_range_set_unit(lv_obj_t *range_box, bool show_hour, bool show_min, bool show_sec);

/**
 * @brief กำหนดความละเอียดการเพิ่ม-ลดของแต่ละหน่วย (Step)
 * @param range_box อ็อบเจกต์ช่วงเวลาที่ได้จาก vd_menu_sub_content_add_time_range
 * @param hour_step สเต็ปของชั่วโมง (เช่น 1 = ทุก 1 ชม., 2 = ทีละ 2 ชม.)
 * @param min_step  สเต็ปของนาที (เช่น 1, 5, 10, 15 นาที)
 * @param sec_step  สเต็ปของวินาที (เช่น 1, 5, 10, 30 วินาที)
 */
void vd_menu_time_range_set_step(lv_obj_t *range_box, uint16_t hour_step, uint16_t min_step, uint16_t sec_step);

/**
 * @brief ล้างและคืนหน่วยความจำของ Content Area
 */
void vd_menu_content_delete(vd_menu_content_t *content);

/* ── วิดเจ็ตย่อยสำหรับเครือข่าย ── */

/**
 * @brief สร้างป้ายสถานะพร้อมไฟกระพริบ/จุดสี (Status Badge with Dot)
 * @param parent Parent container
 * @param is_online true = สีเขียว (ออนไลน์), false = สีแดง (ออฟไลน์)
 * @param text ข้อความกำกับ (เช่น "ออนไลน์", "เชื่อมต่อ")
 * @return lv_obj_t* คอนเทนเนอร์ของป้าย
 */
lv_obj_t *vd_menu_status_badge_create(lv_obj_t *parent, bool is_online, const char *text);

/**
 * @brief สร้างแท่งสัญญาณ 4 ขีด (Signal Bars)
 * @param parent Parent container
 * @param level ระดับสัญญาณ (0 = ไม่มีสัญญาณ, 1 ถึง 4)
 * @return lv_obj_t* คอนเทนเนอร์ของแท่งสัญญาณ
 */
lv_obj_t *vd_menu_signal_bars_create(lv_obj_t *parent, uint8_t level);

/* ── ฟังก์ชันอำนวยความสะดวกใน sub_frame ── */

/**
 * @brief สร้างแถวแสดงค่าพร้อมปุ่มไอคอนคัดลอก (สำหรับ IP Address, MAC Address)
 */
lv_obj_t *vd_menu_sub_content_add_copyable_badge(vd_menu_sub_frame_t *sub_frame,
                                                const char *title,
                                                const char *subtitle,
                                                const char *value_text,
                                                lv_event_cb_t copy_cb,
                                                void *user_data);

/**
 * @brief สร้างแถวปุ่ม Action พร้อมข้อความผลลัพธ์ (สำหรับ ทดสอบ Ping, ซิงค์ข้อมูล Cloud)
 */
lv_obj_t *vd_menu_sub_content_add_action_btn(vd_menu_sub_frame_t *sub_frame,
                                            const char *title,
                                            const char *subtitle,
                                            const char *btn_icon,
                                            const char *btn_text,
                                            const char *initial_result,
                                            lv_obj_t **result_label_out,
                                            lv_event_cb_t cb,
                                            void *user_data);

/**
 * @brief สร้างแถบวัดปริมาณข้อมูล (Progress / Usage Bar เช่น ข้อมูลเน็ตรายเดือน)
 */
lv_obj_t *vd_menu_sub_content_add_progress_bar(vd_menu_sub_frame_t *sub_frame,
                                              const char *title,
                                              const char *subtitle,
                                              uint32_t current_val,
                                              uint32_t max_val,
                                              const char *display_text);

/**
 * @brief สร้างเส้นคั่นกลุ่มระหว่างเครือข่ายที่เชื่อมต่ออยู่ กับกลุ่มเครือข่ายที่พร้อมใช้งาน
 * @param parent คอนเทนเนอร์รายการ Wi-Fi
 */
lv_obj_t *vd_menu_wifi_list_add_section_divider(lv_obj_t *parent);

/**
 * @brief สร้างแถวรายการเครือข่าย Wi-Fi สไตล์ Minimal Clean (แตะทั้งแถวเพื่อเชื่อมต่อ)
 * @param parent_list คอนเทนเนอร์รายการ Wi-Fi
 * @param ssid ชื่อเครือข่าย
 * @param is_secured มีรหัสผ่านหรือไม่ (แสดงไอคอนกุญแจ 12px)
 * @param is_connected กำหนดเป็นเครือข่ายที่เชื่อมต่ออยู่ปัจจุบันหรือไม่
 * @param signal_level ความแรงสัญญาณ (1 ถึง 4 ขีด)
 * @param click_cb Callback เมื่อผู้ใช้แตะที่แถวเครือข่ายนี้
 * @param user_data ข้อมูลส่งต่อ
 * @return lv_obj_t* แถวเครือข่ายที่ถูกสร้าง
 */
lv_obj_t *vd_menu_wifi_list_add_item(lv_obj_t *parent_list,
                                     const char *ssid,
                                     bool is_secured,
                                     bool is_connected,
                                     uint8_t signal_level,
                                     lv_event_cb_t click_cb,
                                     void *user_data);

/**
 * @brief ดึง lv_obj_t* ของการ์ด sub_frame เพื่อใช้สร้าง Widget หรือคอนเทนเนอร์ลูกข้างใน
 * @param sub_frame พอยน์เตอร์โครงสร้าง sub_frame
 * @return lv_obj_t* Card Object
 */
lv_obj_t *vd_menu_sub_frame_get_card(const vd_menu_sub_frame_t *sub_frame);

/**
 * @brief ล้างเนื้อหาและการ์ดทั้งหมดใน Content Area ทิ้งอย่างปลอดภัย พร้อมรีเซ็ต Scroll กลับบนสุด
 * @param content โครงสร้างพื้นที่เนื้อหาฝั่งขวา
 */
void vd_menu_content_clear(vd_menu_content_t *content);

/* --- ฟังก์ชันจัดการส่วนผังช่องจำหน่าย --- */

/**
 * @brief สร้างแถบคำอธิบายสีสถานะ (Legend) หัวการ์ด
 */
lv_obj_t *vd_menu_slot_legend_create(lv_obj_t *parent);

/* --- ฟังก์ชันจัดการและตั้งค่าช่องจำหน่าย --- */

/**
 * @brief กำหนดความจุสูงสุดของช่อง (Max Capacity)
 */
void vd_slot_set_capacity(vd_slot_data_t *slot, uint16_t capacity);

/**
 * @brief กำหนดเกณฑ์สินค้าเหลือน้อยเฉพาะช่อง (Custom Low Stock Threshold)
 */
void vd_slot_set_threshold(vd_slot_data_t *slot, uint16_t low_threshold);

/**
 * @brief สร้างผังช่องจำหน่ายแบบกำหนดจำนวนคอลัมน์ได้อิสระ พร้อมระบบเรนเดอร์ความเร็วสูง
 * @param parent คอนเทนเนอร์เป้าหมาย
 * @param slots อาร์เรย์ข้อมูลช่องจำหน่าย
 * @param total_slots จำนวนช่องทั้งหมด
 * @param cols จำนวนคอลัมน์ต่อ 1 แถว (เช่น 3, 4, 6)
 * @param default_low_threshold เกณฑ์เหลือน้อยเริ่มต้น (ใช้กรณีช่องนั้นไม่ได้ตั้งค่าเฉพาะตัว)
 * @param click_cb Callback เมื่อแตะที่ช่อง
 * @param user_data ข้อมูลส่งต่อ
 * @return lv_obj_t* Grid Container
 */
lv_obj_t *vd_menu_slot_grid_create(lv_obj_t *parent,
                                  const vd_slot_data_t *slots,
                                  uint32_t total_slots,
                                  uint8_t cols,
                                  uint16_t default_low_threshold,
                                  lv_event_cb_t click_cb,
                                  void *user_data);

/**
 * @brief สรุปสถานะช่องจำหน่ายโดยคำนวณจากเกณฑ์เฉพาะของแต่ละช่อง
 */
lv_obj_t *vd_menu_slot_stats_create(lv_obj_t *parent,
                                    const vd_slot_data_t *slots,
                                    uint32_t total_slots,
                                    uint16_t default_low_threshold);

/**
 * @brief สร้างแถวตัวปรับจำนวนแบบ Stepper (- [ค่า] +)
 */
lv_obj_t *vd_menu_sub_content_add_stepper(vd_menu_sub_frame_t *sub_frame,
                                         const char *title,
                                         const char *subtitle,
                                         int32_t default_val,
                                         const char *unit,
                                         lv_event_cb_t change_cb,
                                         void *user_data);

/**
 * @brief เปิดหน้าต่างลอยแก้ไขช่องจำหน่าย
 */
void vd_slot_edit_modal_open(vd_slot_data_t *slot,
                            uint16_t low_threshold,
                            const vd_slot_modal_cbs_t *cbs,
                            void *user_data);

/**
 * @brief ฟังก์ชันให้ฮาร์ดแวร์เรียกกลับมาอัปเดตผลการทดสอบมอเตอร์บนหน้าต่างลอย
 * @param success true = มอเตอร์หมุนปกติ, false = มอเตอร์ติดขัด/Error
 * @param message ข้อความแจ้งเตือน เช่น "หมุนปกติ • สำเร็จ" หรือ "มอเตอร์ติดขัด (Jam)"
 */
void vd_slot_modal_set_motor_result(bool success, const char *message);

/**
 * @brief สร้างการ์ดแสดงหน่วยความจำ/ไดรฟ์ (เช่น หน่วยความจำภายใน, การ์ด SD, USB)
 */
lv_obj_t *vd_menu_file_drive_tile_create(lv_obj_t *parent,
                                         const char *drive_name,
                                         uint32_t used_mb,
                                         uint32_t total_mb,
                                         lv_event_cb_t click_cb,
                                         void *user_data);

/**
 * @brief สร้างแถวรายการไฟล์หรือโฟลเดอร์ในหน้าเรียกดูไฟล์
 */
lv_obj_t *vd_menu_file_row_create(lv_obj_t *parent,
                                  const char *name,
                                  const char *meta_text,
                                  const char *size_text,
                                  bool is_directory,
                                  bool is_selected,
                                  lv_event_cb_t click_cb,
                                  lv_event_cb_t long_press_cb,
                                  void *user_data);

/* ฟังก์ชัน Utility ปลดปล่อยหน่วยความจำ */
void vd_file_items_free(vd_file_item_t *items, uint32_t count);
void vd_drives_free(vd_drive_info_t *drives, uint32_t count);

/**
 * @brief สร้างหน้าเรียกดูไฟล์แบบไดนามิก
 */
lv_obj_t *vd_menu_page_files_create(vd_menu_content_t *content,
                                    const vd_file_browser_cbs_t *cbs,
                                    void *user_data);

void vd_menu_page_files_refresh(bool back_to_drive_list);
bool vd_menu_page_files_is_open(void);

#ifdef __cplusplus
}
#endif

#endif /* VD_MENU_H */
