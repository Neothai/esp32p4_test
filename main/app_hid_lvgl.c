/*
 * app_hid_lvgl.c — เมาส์ / คีย์บอร์ด USB -> LVGL 9
 *
 * สถาปัตยกรรม (สำคัญที่สุดในไฟล์นี้)
 * ─────────────────────────────────────────────────────────────────────
 *
 *   usbh_hid task (prio 7)            เธรด LVGL (prio 12)
 *   ─────────────────────            ────────────────────
 *   อ่าน interrupt-IN
 *          │
 *   event task (prio 6)
 *          │  app_hid_lvgl_feed()
 *          ▼
 *     [ spinlock ]  s_m / s_kq  ◄────── read_cb ของ lv_indev
 *     เขียนเฉพาะตัวแปร                   อ่านออกไปให้ LVGL
 *     ห้ามเรียก lv_* เลย
 *
 * เหตุผล: lv_indev read_cb ถูกเรียกจากข้างใน lv_timer_handler บนเธรด LVGL
 *         ส่วน feed() มาจาก event task คนละเธรด -> ต้องมีตัวกั้น
 *         ใช้ portMUX (spinlock) เพราะ critical section สั้นมาก (<1 µs)
 *         ถ้าใช้ mutex จะเสี่ยง priority inversion กับเธรด LVGL
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_hid_lvgl.h"

static const char *TAG = "app_hid";

/* ===================================================================
 *  สถานะที่แชร์ข้ามเธรด — แตะได้เฉพาะในคริติคอลเซคชันเท่านั้น
 * =================================================================== */

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* --- เมาส์ --- */
typedef struct {
    int32_t x, y;              /* ตำแหน่งเคอร์เซอร์ปัจจุบัน (พิกเซล) */
    uint8_t buttons;           /* สถานะปุ่มล่าสุด bit0 L, bit1 R, bit2 M */
    bool    click_latch;       /* มีการ "กด" เกิดขึ้นตั้งแต่ LVGL อ่านครั้งก่อน */
    int32_t wheel;             /* ล้อสะสม (บวก = เลื่อนลง) */
    bool    present;
} mouse_state_t;

static mouse_state_t s_m;

/* --- คีย์บอร์ด: คิววงกลมของ "ปุ่มที่เพิ่งถูกกด" --- */
#define KQ_LEN 16
static uint32_t s_kq[KQ_LEN];
static uint8_t  s_kq_head, s_kq_tail;
static bool     s_kbd_present;

/* ใช้เทียบหา key ที่เพิ่งกดใหม่ (ฝั่ง event task เท่านั้น ไม่ต้องล็อก) */
static esp32_usbh_kbd_t s_prev_kbd;
static int              s_kbd_index  = -1;
static uint8_t          s_led_bits;
static bool             s_leds_dirty;

/* --- อย่างอื่น (ฝั่ง LVGL เท่านั้น) --- */
static lv_display_t *s_disp;
static lv_indev_t   *s_indev_mouse;
static lv_indev_t   *s_indev_kbd;
static lv_group_t   *s_group;
static lv_obj_t     *s_cursor;
static lv_obj_t     *s_scroll_target;
static uint8_t       s_sens = 4;
static int32_t       s_hor, s_ver;

/* ===================================================================
 *  คิวคีย์บอร์ด
 * =================================================================== */

static void kq_push(uint32_t key)
{
    uint8_t next = (uint8_t)((s_kq_head + 1) % KQ_LEN);
    if (next == s_kq_tail) return;            /* เต็ม — ทิ้งปุ่มใหม่ ดีกว่าทับของเก่า */
    s_kq[s_kq_head] = key;
    s_kq_head = next;
}

static bool kq_pop(uint32_t *out)
{
    if (s_kq_tail == s_kq_head) return false;
    *out = s_kq[s_kq_tail];
    s_kq_tail = (uint8_t)((s_kq_tail + 1) % KQ_LEN);
    return true;
}

static bool kq_empty(void) { return s_kq_tail == s_kq_head; }

/* ===================================================================
 *  แปลง HID usage id -> LVGL key
 *
 *  LVGL ใช้ ASCII ตรง ๆ สำหรับตัวอักษร และใช้ค่า 1..31 สำหรับปุ่มนำทาง
 *  (LV_KEY_UP = 17, LV_KEY_ENTER = 10 ฯลฯ ดู lv_group.h)
 * =================================================================== */
static uint32_t hid_to_lv_key(uint8_t kc, uint8_t modifier)
{
    bool shift = (modifier & 0x22) != 0;      /* bit1 LShift, bit5 RShift */
    bool ctrl  = (modifier & 0x11) != 0;

    switch (kc) {
        case 0x4F: return LV_KEY_RIGHT;
        case 0x50: return LV_KEY_LEFT;
        case 0x51: return LV_KEY_DOWN;
        case 0x52: return LV_KEY_UP;
        case 0x28: return LV_KEY_ENTER;       /* Enter */
        case 0x58: return LV_KEY_ENTER;       /* Keypad Enter */
        case 0x29: return LV_KEY_ESC;
        case 0x2A: return LV_KEY_BACKSPACE;
        case 0x4C: return LV_KEY_DEL;
        case 0x4A: return LV_KEY_HOME;
        case 0x4D: return LV_KEY_END;
        case 0x4B: return LV_KEY_PREV;        /* PageUp  -> โฟกัสก่อนหน้า */
        case 0x4E: return LV_KEY_NEXT;        /* PageDown-> โฟกัสถัดไป */
        case 0x2B: return shift ? LV_KEY_PREV : LV_KEY_NEXT;   /* Tab / Shift+Tab */
        default: break;
    }

    if (ctrl) return 0;                       /* Ctrl+X ฯลฯ ไม่ส่งเป็นตัวอักษร */

    /* ตัวอักษรปกติ — ให้ไลบรารีแปลงให้ (รองรับ shift แล้ว) */
    char ch = esp32_usbh_hid_keycode_to_char(kc, shift);
    return (ch > 0) ? (uint32_t)(uint8_t)ch : 0;
}

/* ===================================================================
 *  ฝั่งผู้ผลิต: เรียกจาก event task ของไลบรารี USB
 * =================================================================== */

static void feed_mouse(const esp32_usbh_mouse_t *m)
{
    /* ความไว: level 1..10 -> คูณ 0.5x .. 3x แบบจำนวนเต็ม */
    int32_t dx = ((int32_t)m->dx * s_sens + 2) / 4;
    int32_t dy = ((int32_t)m->dy * s_sens + 2) / 4;

    portENTER_CRITICAL(&s_mux);

    s_m.x += dx;
    s_m.y += dy;
    if (s_m.x < 0) s_m.x = 0;
    if (s_m.y < 0) s_m.y = 0;
    if (s_hor && s_m.x > s_hor - 1) s_m.x = s_hor - 1;
    if (s_ver && s_m.y > s_ver - 1) s_m.y = s_ver - 1;

    /* ถ้าคลิกเร็วมากจน LVGL อ่านไม่ทัน (กด+ปล่อยภายใน 2 ms)
     * latch ไว้เพื่อให้มีอย่างน้อยหนึ่งเฟรมที่เห็นสถานะ "กด" */
    if ((m->buttons & 0x01) && !(s_m.buttons & 0x01)) s_m.click_latch = true;

    s_m.buttons = m->buttons;
    s_m.wheel  -= m->wheel;          /* HID: ขึ้น = บวก; LVGL: ลง = บวก */

    portEXIT_CRITICAL(&s_mux);
}

static void feed_kbd(const esp32_usbh_kbd_t *k)
{
    /* หา key ที่ "เพิ่งถูกกด" = มีใน report ใหม่ แต่ไม่มีในอันเก่า */
    for (int i = 0; i < 6; i++) {
        uint8_t kc = k->keys[i];
        if (kc == 0 || kc == 0x01 /* ErrorRollOver */) continue;

        bool was_down = false;
        for (int j = 0; j < 6; j++) {
            if (s_prev_kbd.keys[j] == kc) { was_down = true; break; }
        }
        if (was_down) continue;

        /* ปุ่ม lock -> สลับไฟ LED (ทำทีหลัง นอกคริติคอลเซคชัน) */
        if (kc == 0x39) { s_led_bits ^= ESP32_USBH_KBD_LED_CAPSLOCK;   s_leds_dirty = true; continue; }
        if (kc == 0x53) { s_led_bits ^= ESP32_USBH_KBD_LED_NUMLOCK;    s_leds_dirty = true; continue; }
        if (kc == 0x47) { s_led_bits ^= ESP32_USBH_KBD_LED_SCROLLLOCK; s_leds_dirty = true; continue; }

        uint8_t mod = k->modifier;
        if (s_led_bits & ESP32_USBH_KBD_LED_CAPSLOCK) {
            /* Caps Lock มีผลกับ a-z เท่านั้น (0x04..0x1D) */
            if (kc >= 0x04 && kc <= 0x1D) mod ^= 0x02;
        }

        uint32_t key = hid_to_lv_key(kc, mod);
        if (!key) continue;

        portENTER_CRITICAL(&s_mux);
        kq_push(key);
        portEXIT_CRITICAL(&s_mux);
    }

    s_prev_kbd = *k;

    if (s_leds_dirty && s_kbd_index >= 0) {
        s_leds_dirty = false;
        esp32_usbh_hid_set_leds(s_kbd_index, s_led_bits);   /* control transfer — ช้าได้ */
    }
}

void app_hid_lvgl_feed(const esp32_usbh_event_t *ev)
{
    if (!ev) return;

    switch (ev->id) {

    case ESP32_USBH_EV_HID_REPORT:
        if (ev->hid.kind == ESP32_USBH_HID_KIND_MOUSE)         feed_mouse(&ev->hid.mouse);
        else if (ev->hid.kind == ESP32_USBH_HID_KIND_KEYBOARD) feed_kbd(&ev->hid.kbd);
        break;

    case ESP32_USBH_EV_HID_ATTACHED:
        if (ev->hid.kind == ESP32_USBH_HID_KIND_MOUSE) {
            portENTER_CRITICAL(&s_mux);
            if (!s_m.present) {               /* เสียบใหม่ -> เริ่มกลางจอ */
                s_m.x = s_hor / 2;
                s_m.y = s_ver / 2;
            }
            s_m.present = true;
            portEXIT_CRITICAL(&s_mux);
            ESP_LOGI(TAG, "เมาส์เสียบแล้ว: %s", ev->dev.product);
        } else if (ev->hid.kind == ESP32_USBH_HID_KIND_KEYBOARD) {
            s_kbd_index = ev->hid.index;
            s_led_bits  = 0;
            s_leds_dirty = true;
            memset(&s_prev_kbd, 0, sizeof(s_prev_kbd));
            portENTER_CRITICAL(&s_mux);
            s_kbd_present = true;
            portEXIT_CRITICAL(&s_mux);
            ESP_LOGI(TAG, "คีย์บอร์ดเสียบแล้ว: %s", ev->dev.product);
        }
        break;

    case ESP32_USBH_EV_HID_DETACHED:
        if (ev->hid.kind == ESP32_USBH_HID_KIND_MOUSE) {
            portENTER_CRITICAL(&s_mux);
            s_m.present = false;
            s_m.buttons = 0;                  /* อย่าให้ค้างเป็น "กดอยู่" */
            s_m.click_latch = false;
            portEXIT_CRITICAL(&s_mux);
        } else if (ev->hid.kind == ESP32_USBH_HID_KIND_KEYBOARD) {
            s_kbd_index = -1;
            portENTER_CRITICAL(&s_mux);
            s_kbd_present = false;
            s_kq_head = s_kq_tail = 0;        /* ล้างคิว กันปุ่มค้าง */
            portEXIT_CRITICAL(&s_mux);
        }
        break;

    default:
        break;
    }
}

/* ===================================================================
 *  ฝั่งผู้บริโภค: read_cb — รันบนเธรด LVGL เสมอ
 * =================================================================== */

static void mouse_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    LV_UNUSED(indev);

    portENTER_CRITICAL(&s_mux);
    int32_t x = s_m.x, y = s_m.y;
    bool    pressed = (s_m.buttons & 0x01) || s_m.click_latch;
    s_m.click_latch = false;                  /* ใช้แล้วเคลียร์ */
    int32_t wheel = s_m.wheel;
    s_m.wheel = 0;
    bool present = s_m.present;
    portEXIT_CRITICAL(&s_mux);

    data->point.x = x;
    data->point.y = y;
    data->state   = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    /* ล้อเมาส์: LVGL pointer indev ไม่รองรับ wheel จึงเลื่อนเอง */
    if (wheel) {
        lv_obj_t *t = s_scroll_target ? s_scroll_target : lv_screen_active();
        if (t) lv_obj_scroll_by(t, 0, -wheel * 40, LV_ANIM_OFF);
    }

    /* ซ่อนเคอร์เซอร์เมื่อไม่มีเมาส์ */
    if (s_cursor) {
        bool hidden = lv_obj_has_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
        if (present == hidden) {
            if (present) lv_obj_remove_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
            else         lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void kbd_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    LV_UNUSED(indev);

    /* LVGL ต้องเห็น PRESSED แล้วตามด้วย RELEASED ของปุ่มเดิม
     * จึงจะนับเป็นการกด 1 ครั้ง -> สลับสถานะทุกครั้งที่ถูกอ่าน */
    static uint32_t last_key;
    static bool     need_release;

    if (need_release) {
        need_release  = false;
        data->key     = last_key;
        data->state   = LV_INDEV_STATE_RELEASED;
        portENTER_CRITICAL(&s_mux);
        data->continue_reading = !kq_empty();
        portEXIT_CRITICAL(&s_mux);
        return;
    }

    uint32_t k;
    bool got, more;
    portENTER_CRITICAL(&s_mux);
    got  = kq_pop(&k);
    more = !kq_empty();
    portEXIT_CRITICAL(&s_mux);

    if (!got) { data->state = LV_INDEV_STATE_RELEASED; return; }

    last_key     = k;
    need_release = true;
    data->key    = k;
    data->state  = LV_INDEV_STATE_PRESSED;
    data->continue_reading = true;            /* อ่านต่อทันทีเพื่อปิดด้วย RELEASED */
    LV_UNUSED(more);
}

/* ===================================================================
 *  เคอร์เซอร์
 * =================================================================== */

static void cursor_create(void)
{
    /* วงกลมขอบขาว ไส้ดำโปร่ง — เห็นชัดทุกพื้นหลัง ไม่ต้องใช้ไฟล์รูป
     * ถ้าอยากได้ลูกศรจริง ให้ใช้ lv_image_create() + lv_image_set_src()
     * แล้วชี้ไปที่ LV_IMAGE_DECLARE ของไฟล์ C-array ที่แปลงไว้ */
    s_cursor = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_cursor);
    lv_obj_set_size(s_cursor, 16, 16);
    lv_obj_set_style_radius(s_cursor, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_cursor, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(s_cursor, LV_OPA_40, 0);
    lv_obj_set_style_border_color(s_cursor, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_cursor, 2, 0);
    lv_obj_set_style_border_opa(s_cursor, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(s_cursor, 6, 0);
    lv_obj_set_style_shadow_opa(s_cursor, LV_OPA_30, 0);
    lv_obj_remove_flag(s_cursor, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_cursor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);     /* โชว์ตอนเสียบเมาส์ */
}

/* ===================================================================
 *  init + API เบ็ดเตล็ด
 * =================================================================== */

esp_err_t app_hid_lvgl_init(lv_display_t *disp)
{
    if (!disp) return ESP_ERR_INVALID_ARG;
    if (s_indev_mouse) return ESP_OK;                  /* init ซ้ำ ไม่เป็นไร */

    s_disp = disp;
    s_hor  = lv_display_get_horizontal_resolution(disp);
    s_ver  = lv_display_get_vertical_resolution(disp);

    s_m.x = s_hor / 2;
    s_m.y = s_ver / 2;

    cursor_create();

    /* ── เมาส์ = pointer ── */
    s_indev_mouse = lv_indev_create();
    lv_indev_set_type(s_indev_mouse, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev_mouse, mouse_read_cb);
    lv_indev_set_display(s_indev_mouse, disp);
    lv_indev_set_cursor(s_indev_mouse, s_cursor);      /* LVGL ย้ายตามให้เอง */

    /* ── คีย์บอร์ด = keypad ── */
    s_group = lv_group_create();
    s_indev_kbd = lv_indev_create();
    lv_indev_set_type(s_indev_kbd, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_indev_kbd, kbd_read_cb);
    lv_indev_set_display(s_indev_kbd, disp);
    lv_indev_set_group(s_indev_kbd, s_group);

    ESP_LOGI(TAG, "ผูกเมาส์/คีย์บอร์ดเข้ากับ LVGL แล้ว (จอ %ldx%ld)",
             (long)s_hor, (long)s_ver);
    return ESP_OK;
}

lv_group_t *app_hid_lvgl_group(void) { return s_group; }

void app_hid_lvgl_set_scroll_target(lv_obj_t *obj) { s_scroll_target = obj; }

void app_hid_lvgl_show_cursor(bool show)
{
    if (!s_cursor) return;
    if (show) lv_obj_remove_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
}

void app_hid_lvgl_set_sensitivity(uint8_t level)
{
    if (level < 1)  level = 1;
    if (level > 10) level = 10;
    s_sens = level;
}

bool app_hid_lvgl_mouse_present(void)
{
    portENTER_CRITICAL(&s_mux);
    bool p = s_m.present;
    portEXIT_CRITICAL(&s_mux);
    return p;
}

bool app_hid_lvgl_keyboard_present(void)
{
    portENTER_CRITICAL(&s_mux);
    bool p = s_kbd_present;
    portEXIT_CRITICAL(&s_mux);
    return p;
}
