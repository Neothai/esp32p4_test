/*
 * esp32_usbh_hid.c — HID host (เมาส์ / คีย์บอร์ด / อื่น ๆ)
 *
 * ทำไมใช้ "polling task" แทน async callback:
 *   usbh_submit_urb() จะบล็อกก็ต่อเมื่อ urb->timeout > 0
 *   (usb_hc_dwc2.c:1076 -> usb_osal_sem_take(chan->waitsem, urb->timeout))
 *   ถ้าใช้โหมด async คอมพลีตคอลแบ็กจะรันใน ISR ซึ่งห้ามทำงานหนัก
 *   -> ใช้โหมดบล็อกใน task ของเราเอง แล้วส่ง event ออกไปให้ event task
 *      ผู้ใช้จึงไม่มีทางถูกเรียกจาก ISR เลย
 *
 * NAK: interrupt IN ที่ไม่มีข้อมูลคืน -USB_ERR_NAK ทันที (usb_hc_dwc2.c:1234)
 *      ฮาร์ดแวร์ไม่ retry ให้ ต้องหน่วงตาม bInterval เอง
 */

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "usbh_core.h"
#include "usbh_hid.h"
#include "usb_hid.h"

#include "esp32_usbh_hid.h"
#include "esp32_usb_private.h"

#if ESP32_USBH_ENABLE_HID

#ifndef HID_PROTOCOL_REPORT
#define HID_PROTOCOL_REPORT 0x01
#endif

static const char *TAG = PRIV_TAG_HID;
#define NHID ESP32_USBH_HID_MAX_DEVICES

typedef struct {
    volatile bool     used;
    volatile bool     alive;
    volatile bool     running;

    struct usbh_hid  *hid;
    esp32_usbh_hid_kind_t kind;
    bool              boot;
    uint8_t           subclass, protocol;
    uint8_t           poll_ms, poll_override;
    uint8_t           bInterval;
    uint16_t          mps;
    uint8_t           ep_addr;
    uint8_t           intf;
    esp32_usbh_speed_t speed;

    uint16_t          vid, pid;
    char              product[ESP32_USBH_STR_LEN];
    char              devname[CONFIG_USBHOST_DEV_NAMELEN];

    uint8_t          *buf;              /* DMA buffer ต่ออุปกรณ์ */
    uint8_t           last[ESP32_USBH_HID_REPORT_SIZE];
    uint8_t           last_len;
    SemaphoreHandle_t last_mtx;

    uint8_t           prev_keys[6];
    uint8_t           prev_mod;

    uint32_t          report_count, error_count;
} hdev_t;

static hdev_t            s_h[NHID];
static SemaphoreHandle_t s_tbl_mtx;
static volatile bool     s_running;

/* ========================================================================
 *  ตารางแปลง keycode
 * ======================================================================== */

static const char kc_lower[] =
    "\0\0\0\0" "abcdefghijklmnopqrstuvwxyz" "1234567890"
    "\n\x1b\b\t " "-=[]\\#;'`,./";
static const char kc_upper[] =
    "\0\0\0\0" "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "!@#$%^&*()"
    "\n\x1b\b\t " "_+{}|~:\"~<>?";

char esp32_usbh_hid_keycode_to_char(uint8_t kc, bool shift)
{
    if (kc > 0x38) return 0;
    char c = (shift ? kc_upper : kc_lower)[kc];
    return c ? c : 0;
}

const char *esp32_usbh_hid_keycode_name(uint8_t kc)
{
    switch (kc) {
        case 0x28: return "ENTER";     case 0x29: return "ESC";
        case 0x2A: return "BACKSPACE"; case 0x2B: return "TAB";
        case 0x2C: return "SPACE";     case 0x39: return "CAPSLOCK";
        case 0x3A: return "F1";  case 0x3B: return "F2";  case 0x3C: return "F3";
        case 0x3D: return "F4";  case 0x3E: return "F5";  case 0x3F: return "F6";
        case 0x40: return "F7";  case 0x41: return "F8";  case 0x42: return "F9";
        case 0x43: return "F10"; case 0x44: return "F11"; case 0x45: return "F12";
        case 0x46: return "PRTSCR";
        case 0x4A: return "HOME";  case 0x4B: return "PGUP"; case 0x4C: return "DEL";
        case 0x4D: return "END";   case 0x4E: return "PGDN";
        case 0x4F: return "RIGHT"; case 0x50: return "LEFT";
        case 0x51: return "DOWN";  case 0x52: return "UP";
        default:   return NULL;
    }
}

/* ========================================================================
 *  class request ที่แก้บั๊ก wIndex ของ CherryUSB
 *  usbh_hid.c:125/167/185 ใส่ wIndex = 0 ตายตัว ทั้งที่ควรเป็น hid->intf
 *  อุปกรณ์ composite จะโดนส่ง request ผิด interface
 * ======================================================================== */

static int hid_ctrl(struct usbh_hid *hid, uint8_t dir, uint8_t req,
                    uint16_t val, uint8_t *data, uint16_t len)
{
    if (!hid || !hid->hport) return -USB_ERR_INVAL;
    if (!priv_ep0_lock(PRIV_EP0_MUTEX_WAIT_MS)) return -USB_ERR_BUSY;

    struct usb_setup_packet *s = hid->hport->setup;
    s->bmRequestType = dir | USB_REQUEST_CLASS | USB_REQUEST_RECIPIENT_INTERFACE;
    s->bRequest = req;
    s->wValue   = val;
    s->wIndex   = hid->intf;        /* <- ของเดิมใส่ 0 */
    s->wLength  = len;

    int r = usbh_control_transfer(hid->hport, s, data);
    priv_ep0_unlock();
    return r;
}

static int hid_set_protocol_fixed(struct usbh_hid *hid, uint8_t proto)
{
    return hid_ctrl(hid, USB_REQUEST_DIR_OUT, HID_REQUEST_SET_PROTOCOL, proto, NULL, 0);
}

/* ========================================================================
 *  ถอดความ report
 * ======================================================================== */

static void emit_report(hdev_t *h, const uint8_t *b, int n)
{
    esp32_usbh_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.id = ESP32_USBH_EV_HID_REPORT;

    ev.dev.valid     = true;
    ev.dev.dev_class = ESP32_USBH_CLASS_HID;
    ev.dev.speed     = h->speed;
    ev.dev.vid       = h->vid;
    ev.dev.pid       = h->pid;
    ev.dev.interface = h->intf;
    snprintf(ev.dev.product, sizeof(ev.dev.product), "%s", h->product);
    snprintf(ev.dev.devname, sizeof(ev.dev.devname), "%s", h->devname);

    ev.hid.index         = (uint8_t)(h - s_h);
    ev.hid.kind          = h->kind;
    ev.hid.boot_protocol = h->boot;
    ev.hid.raw_len       = (n > ESP32_USBH_HID_REPORT_SIZE)
                         ? ESP32_USBH_HID_REPORT_SIZE : (uint8_t)n;
    memcpy(ev.hid.raw, b, ev.hid.raw_len);

    if (h->kind == ESP32_USBH_HID_KIND_MOUSE && n >= 3) {
        ev.hid.mouse.buttons = b[0];
        ev.hid.mouse.dx      = (int8_t)b[1];
        ev.hid.mouse.dy      = (int8_t)b[2];
        ev.hid.mouse.wheel   = (n >= 4) ? (int8_t)b[3] : 0;
    } else if (h->kind == ESP32_USBH_HID_KIND_KEYBOARD && n >= 8) {
        ev.hid.kbd.modifier = b[0];
        memcpy(ev.hid.kbd.keys, &b[2], 6);
        memcpy(h->prev_keys, &b[2], 6);
        h->prev_mod = b[0];
    }

    priv_event_post(&ev);
}

/* ========================================================================
 *  task อ่าน report
 * ======================================================================== */

static void hid_task(void *arg)
{
    hdev_t *h = (hdev_t *)arg;
    h->running = true;

    vTaskDelay(pdMS_TO_TICKS(PRIV_ENUM_SETTLE_MS));

    struct usbh_hid *hid = h->hid;
    if (!h->alive || !hid || !hid->intin) goto out;

    {
        struct usb_interface_descriptor *idesc =
            &hid->hport->config.intf[hid->intf].altsetting[0].intf_desc;

        h->subclass  = idesc->bInterfaceSubClass;
        h->protocol  = idesc->bInterfaceProtocol;
        h->bInterval = hid->intin->bInterval;
        h->mps       = hid->intin->wMaxPacketSize & 0x7FF;
        h->ep_addr   = hid->intin->bEndpointAddress;
        h->intf      = hid->intf;
        h->vid       = hid->hport->device_desc.idVendor;
        h->pid       = hid->hport->device_desc.idProduct;

        switch (hid->hport->speed) {
            case USB_SPEED_LOW:  h->speed = ESP32_USBH_SPEED_LOW;  break;
            case USB_SPEED_FULL: h->speed = ESP32_USBH_SPEED_FULL; break;
            case USB_SPEED_HIGH: h->speed = ESP32_USBH_SPEED_HIGH; break;
            default:             h->speed = ESP32_USBH_SPEED_UNKNOWN; break;
        }

        /* HS: bInterval เป็นเลขชี้กำลังของ microframe, FS/LS: เป็น ms ตรง ๆ */
        uint32_t p;
        if (h->speed == ESP32_USBH_SPEED_HIGH) {
            p = (h->bInterval > 0) ? ((1u << (h->bInterval - 1)) * 125u) / 1000u : 1u;
        } else {
            p = (h->bInterval > 0) ? h->bInterval : 10u;
        }
        if (p < 1)  p = 1;
        if (p > 32) p = 32;
        h->poll_ms = (uint8_t)p;

        if (h->subclass == HID_SUBCLASS_BOOTIF) {
            if (h->protocol == HID_PROTOCOL_KEYBOARD) h->kind = ESP32_USBH_HID_KIND_KEYBOARD;
            else if (h->protocol == HID_PROTOCOL_MOUSE) h->kind = ESP32_USBH_HID_KIND_MOUSE;
            else h->kind = ESP32_USBH_HID_KIND_OTHER;
        } else {
            h->kind = ESP32_USBH_HID_KIND_OTHER;
        }

        /* ดึงชื่อรุ่น + ลงตารางอุปกรณ์ (control transfer -> ต้องอยู่นอกเธรด hub) */
        esp32_usbh_devinfo_t info;
        priv_devtable_add(hid->hport, hid->intf, ESP32_USBH_CLASS_HID,
                          h->devname, &info);
        snprintf(h->product, sizeof(h->product), "%s", info.product);

#if ESP32_USBH_HID_FORCE_BOOT
        if (h->subclass == HID_SUBCLASS_BOOTIF) {
            int r = hid_set_protocol_fixed(hid, HID_PROTOCOL_BOOT);
            h->boot = (r >= 0);
            if (r < 0) ESP_LOGW(TAG, "%s: SET_PROTOCOL(boot) ล้มเหลว %d", h->devname, r);
            /* SET_IDLE(0,0) = ส่ง report เฉพาะตอนค่าเปลี่ยน (wIndex ของตัวนี้ถูกอยู่แล้ว) */
            usbh_hid_set_idle(hid, 0, 0);
        }
#endif

        ESP_LOGI(TAG, "%s | %s | %s | intf=%u sub=%u proto=%u EP=%02X Mps=%u "
                      "bInterval=%u -> poll %ums | %s",
                 h->devname,
                 h->kind == ESP32_USBH_HID_KIND_KEYBOARD ? "คีย์บอร์ด" :
                 h->kind == ESP32_USBH_HID_KIND_MOUSE    ? "เมาส์" : "HID อื่น",
                 h->product[0] ? h->product : "(ไม่มีชื่อ)",
                 h->intf, h->subclass, h->protocol, h->ep_addr, h->mps,
                 h->bInterval, h->poll_ms, esp32_usbh_speed_str(h->speed));

        esp32_usbh_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.id = ESP32_USBH_EV_HID_ATTACHED;
        ev.dev.valid = true;
        ev.dev.dev_class = ESP32_USBH_CLASS_HID;
        ev.dev.vid = h->vid; ev.dev.pid = h->pid;
        ev.dev.speed = h->speed; ev.dev.interface = h->intf;
        snprintf(ev.dev.product, sizeof(ev.dev.product), "%s", h->product);
        snprintf(ev.dev.devname, sizeof(ev.dev.devname), "%s", h->devname);
        ev.hid.index = (uint8_t)(h - s_h);
        ev.hid.kind  = h->kind;
        ev.hid.boot_protocol = h->boot;
        priv_event_post(&ev);
    }

    while (h->alive) {
        struct usbh_urb *urb = &hid->intin_urb;
        uint32_t len = h->mps;
        if (len > ESP32_USBH_HID_REPORT_SIZE) len = ESP32_USBH_HID_REPORT_SIZE;

        usbh_int_urb_fill(urb, hid->hport, hid->intin, h->buf, len,
                          PRIV_HID_URB_TIMEOUT_MS, NULL, NULL);
        int ret = usbh_submit_urb(urb);

        if (ret == 0) {
            int n = urb->actual_length;
            if (n > 0) {
                h->report_count++;
                xSemaphoreTake(h->last_mtx, portMAX_DELAY);
                h->last_len = (n > ESP32_USBH_HID_REPORT_SIZE)
                            ? ESP32_USBH_HID_REPORT_SIZE : (uint8_t)n;
                memcpy(h->last, h->buf, h->last_len);
                xSemaphoreGive(h->last_mtx);
                emit_report(h, h->buf, n);
            }
            vTaskDelay(pdMS_TO_TICKS(h->poll_override ? h->poll_override : h->poll_ms));

        } else if (ret == -USB_ERR_NAK || ret == -USB_ERR_TIMEOUT) {
            vTaskDelay(pdMS_TO_TICKS(h->poll_override ? h->poll_override : h->poll_ms));

        } else if (ret == -USB_ERR_SHUTDOWN || ret == -USB_ERR_NOTCONN) {
            break;                                   /* ถอดสาย */

        } else if (ret == -USB_ERR_STALL) {
            ESP_LOGW(TAG, "%s: endpoint STALL -> หยุด", h->devname);
            h->error_count++;
            break;
        } else {
            h->error_count++;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

out:
    h->running = false;
    vTaskDelete(NULL);
}

/* ========================================================================
 *  hook ของ CherryUSB — รันบนเธรด hub ห้ามบล็อกนาน
 * ======================================================================== */

void usbh_hid_run(struct usbh_hid *hid_class)
{
    if (!s_running) return;

    xSemaphoreTake(s_tbl_mtx, portMAX_DELAY);
    int k = -1;
    for (int i = 0; i < NHID; i++) if (!s_h[i].used) { k = i; break; }
    if (k < 0) {
        xSemaphoreGive(s_tbl_mtx);
        ESP_LOGE(TAG, "HID เต็ม (เพิ่ม ESP32_USBH_HID_MAX_DEVICES)");
        return;
    }

    hdev_t *h = &s_h[k];
    uint8_t *keep_buf = h->buf;
    SemaphoreHandle_t keep_mtx = h->last_mtx;
    memset(h, 0, sizeof(*h));
    h->buf      = keep_buf;
    h->last_mtx = keep_mtx;
    h->hid      = hid_class;
    h->used     = true;
    h->alive    = true;
    h->intf     = hid_class->intf;
    snprintf(h->devname, sizeof(h->devname), "%s",
             hid_class->hport->config.intf[hid_class->intf].devname);
    xSemaphoreGive(s_tbl_mtx);

    /* k < ESP32_USBH_HID_MAX_DEVICES (<=16) จึงเป็นเลข 1-2 หลักเสมอ
     * แต่ GCC มองว่า "%d" ยาวได้ 11 หลัก -> ขยาย buffer ให้พอกับกรณีแย่สุด
     * ง่ายกว่าและปลอดภัยกว่าการไปไล่ปิด -Wformat-truncation */
    char tn[24];
    snprintf(tn, sizeof(tn), "usbh_hid%d", k);
    if (priv_task_create(hid_task, tn, ESP32_USBH_HID_TASK_STACK, h,
                         ESP32_USBH_HID_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "สร้าง task ไม่สำเร็จ");
        h->used = false;
    }
}

void usbh_hid_stop(struct usbh_hid *hid_class)
{
    int k = -1;
    xSemaphoreTake(s_tbl_mtx, portMAX_DELAY);
    for (int i = 0; i < NHID; i++) if (s_h[i].used && s_h[i].hid == hid_class) { k = i; break; }
    xSemaphoreGive(s_tbl_mtx);
    if (k < 0) return;

    hdev_t *h = &s_h[k];
    h->alive = false;

    /* ต้องรอให้ task ออกจริง เพราะ usbh_hid_disconnect() จะ
     * usbh_hid_class_free(hid_class) ทันทีหลังจากนี้ (usbh_hid.c:283) */
    for (int t = 0; t < PRIV_STOP_JOIN_TIMEOUT_MS / PRIV_STOP_JOIN_STEP_MS; t++) {
        if (!h->running) break;
        vTaskDelay(pdMS_TO_TICKS(PRIV_STOP_JOIN_STEP_MS));
    }
    if (h->running) ESP_LOGE(TAG, "%s: task ไม่ยอมจบ!", h->devname);

    esp32_usbh_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.id = ESP32_USBH_EV_HID_DETACHED;
    ev.dev.valid = true;
    ev.dev.dev_class = ESP32_USBH_CLASS_HID;
    ev.dev.vid = h->vid; ev.dev.pid = h->pid;
    snprintf(ev.dev.devname, sizeof(ev.dev.devname), "%s", h->devname);
    ev.hid.index = (uint8_t)k;
    ev.hid.kind  = h->kind;
    priv_event_post(&ev);

    priv_devtable_remove(hid_class->hport, hid_class->intf);

    h->used = false;
    h->hid  = NULL;
}

/* ========================================================================
 *  lifecycle
 * ======================================================================== */

esp_err_t priv_hid_start(void)
{
    memset(s_h, 0, sizeof(s_h));
    s_tbl_mtx = xSemaphoreCreateMutex();
    if (!s_tbl_mtx) return ESP_ERR_NO_MEM;

    for (int i = 0; i < NHID; i++) {
        s_h[i].last_mtx = xSemaphoreCreateMutex();
        s_h[i].buf = heap_caps_aligned_alloc(CONFIG_USB_ALIGN_SIZE,
                        ESP32_USBH_HID_REPORT_SIZE,
                        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_CACHE_ALIGNED);
        if (!s_h[i].buf || !s_h[i].last_mtx) return ESP_ERR_NO_MEM;
    }

    s_running = true;
    ESP_LOGI(TAG, "โมดูล HID พร้อม | สูงสุด %d interface | boot_protocol=%d",
             NHID, ESP32_USBH_HID_FORCE_BOOT);
    return ESP_OK;
}

void priv_hid_stop(void)
{
    s_running = false;
    for (int i = 0; i < NHID; i++) {
        s_h[i].alive = false;
        for (int t = 0; t < 100 && s_h[i].running; t++) vTaskDelay(pdMS_TO_TICKS(5));
        if (s_h[i].buf)      { heap_caps_free(s_h[i].buf); s_h[i].buf = NULL; }
        if (s_h[i].last_mtx) { vSemaphoreDelete(s_h[i].last_mtx); s_h[i].last_mtx = NULL; }
        s_h[i].used = false;
    }
    if (s_tbl_mtx) { vSemaphoreDelete(s_tbl_mtx); s_tbl_mtx = NULL; }
}

/* ========================================================================
 *  API สาธารณะ
 * ======================================================================== */

int esp32_usbh_hid_get_count(void)
{
    int n = 0;
    for (int i = 0; i < NHID; i++) if (s_h[i].used) n++;
    return n;
}

esp_err_t esp32_usbh_hid_get_info(int index, esp32_usbh_hid_info_t *out)
{
    if (index < 0 || index >= NHID || !out) return ESP_ERR_INVALID_ARG;
    hdev_t *h = &s_h[index];
    memset(out, 0, sizeof(*out));
    if (!h->used) return ESP_ERR_NOT_FOUND;

    out->present       = true;
    out->kind          = h->kind;
    out->boot_protocol = h->boot;
    out->interface     = h->intf;
    out->ep_addr       = h->ep_addr;
    out->ep_mps        = h->mps;
    out->bInterval     = h->bInterval;
    out->poll_ms       = h->poll_override ? h->poll_override : h->poll_ms;
    out->speed         = h->speed;
    out->vid           = h->vid;
    out->pid           = h->pid;
    out->report_count  = h->report_count;
    out->error_count   = h->error_count;
    snprintf(out->product, sizeof(out->product), "%s", h->product);
    snprintf(out->devname, sizeof(out->devname), "%s", h->devname);
    return ESP_OK;
}

int esp32_usbh_hid_get_index_by_kind(esp32_usbh_hid_kind_t kind)
{
    for (int i = 0; i < NHID; i++) if (s_h[i].used && s_h[i].kind == kind) return i;
    return -1;
}

int esp32_usbh_hid_get_last_report(int index, uint8_t *buf, size_t cap)
{
    if (index < 0 || index >= NHID || !buf) return -1;
    hdev_t *h = &s_h[index];
    if (!h->used || !h->last_mtx) return -1;

    xSemaphoreTake(h->last_mtx, portMAX_DELAY);
    int n = h->last_len;
    if ((size_t)n > cap) n = (int)cap;
    memcpy(buf, h->last, n);
    xSemaphoreGive(h->last_mtx);
    return n;
}

int esp32_usbh_hid_get_report_descriptor(int index, uint8_t *buf, size_t cap)
{
    if (index < 0 || index >= NHID || !buf) return -1;
    hdev_t *h = &s_h[index];
    if (!h->used || !h->hid) return -1;

    if (!priv_ep0_lock(PRIV_EP0_MUTEX_WAIT_MS)) return -1;
    int r = usbh_hid_get_report_descriptor(h->hid, buf, cap);
    priv_ep0_unlock();
    return (r < 0) ? r : (int)((h->hid->report_size < cap) ? h->hid->report_size : cap);
}

esp_err_t esp32_usbh_hid_set_leds(int index, uint8_t led_bits)
{
    if (index < 0 || index >= NHID) return ESP_ERR_INVALID_ARG;
    hdev_t *h = &s_h[index];
    if (!h->used || !h->hid) return ESP_ERR_NOT_FOUND;
    if (h->kind != ESP32_USBH_HID_KIND_KEYBOARD) return ESP_ERR_NOT_SUPPORTED;

    /* ต้องเป็น buffer สำหรับ DMA */
    static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t led[CONFIG_USB_ALIGN_SIZE];
    led[0] = led_bits & 0x07;

    /* SET_REPORT(Output, id 0) — wValue = (type<<8)|id, type 2 = Output */
    int r = hid_ctrl(h->hid, USB_REQUEST_DIR_OUT, HID_REQUEST_SET_REPORT,
                     (2 << 8) | 0, led, 1);
    return (r < 0) ? ESP_FAIL : ESP_OK;
}

esp_err_t esp32_usbh_hid_set_protocol(int index, bool boot)
{
    if (index < 0 || index >= NHID) return ESP_ERR_INVALID_ARG;
    hdev_t *h = &s_h[index];
    if (!h->used || !h->hid) return ESP_ERR_NOT_FOUND;

    int r = hid_set_protocol_fixed(h->hid, boot ? HID_PROTOCOL_BOOT : HID_PROTOCOL_REPORT);
    if (r < 0) return ESP_FAIL;
    h->boot = boot;
    return ESP_OK;
}

esp_err_t esp32_usbh_hid_set_poll_interval(int index, uint8_t ms)
{
    if (index < 0 || index >= NHID) return ESP_ERR_INVALID_ARG;
    if (ms > 32) { s_h[index].poll_override = 32; return ESP_ERR_INVALID_ARG; }
    s_h[index].poll_override = ms;      /* 0 = กลับไปใช้ bInterval */
    return ESP_OK;
}

#endif /* ESP32_USBH_ENABLE_HID */
