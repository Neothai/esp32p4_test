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
#include <stdlib.h>
#include <stdint.h>

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
    uint8_t           prev_btn;

    /* --- mapping ของเมาส์ที่ได้จาก report descriptor (ดู parse_mouse_desc) --- */
    priv_mouse_map_t  mmap;

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

/* ========================================================================
 *  ตัวแกะ HID report descriptor สำหรับเมาส์
 *
 *  ทำไมต้องมี: เมาส์ยุคใหม่ (2.4G / dual-mode BT) จำนวนมาก "ประกาศ" ว่า
 *  รองรับ boot protocol แต่พอสั่ง SET_PROTOCOL(boot) แล้ว "ไม่ทำตาม"
 *  ยังส่ง report แบบ report-protocol ซึ่งมี Report ID นำหน้า 1 ไบต์
 *  และแกน X/Y เป็น 12/16 บิต ไม่ใช่ 8 บิต
 *
 *  ผล: ถ้าใช้ layout ของ boot (b[0]=ปุ่ม b[1]=dx b[2]=dy) จะอ่านเพี้ยนหมด
 *      -> กดปุ่มขวา (0x02) ไปโผล่เป็น dx=+2 เคอร์เซอร์วิ่งไปขวา  ← อาการที่เจอ
 *
 *  วิธีแก้: ไม่ไปบังคับ boot กับเมาส์ แต่อ่าน descriptor จริงแล้วคำนวณ
 *  bit offset ของปุ่ม / X / Y / ล้อ เอาเอง
 * ======================================================================== */

static int32_t bits_get(const uint8_t *d, int n, uint16_t off, uint8_t size, bool sign)
{
    if (size == 0 || size > 32) return 0;
    if ((off + size + 7) / 8 > (uint16_t)n) return 0;          /* เลยท้าย report */

    uint32_t v = 0;
    for (uint8_t i = 0; i < size; i++) {
        uint16_t b = (uint16_t)(off + i);
        if (d[b >> 3] & (1u << (b & 7))) v |= (1u << i);
    }
    if (sign && size < 32 && (v & (1u << (size - 1))))
        v |= ~((1u << size) - 1u);                             /* sign extend */
    return (int32_t)v;
}

/** แกะ descriptor หา offset ของ Button / X / Y / Wheel
 *  คืน true ถ้าเจอทั้ง X และ Y (ถือว่าใช้ได้) */
static bool parse_mouse_desc(const uint8_t *d, int len, priv_mouse_map_t *m)
{
    memset(m, 0, sizeof(*m));

    uint16_t usage_page = 0;
    uint8_t  rep_size = 0, rep_count = 0, rep_id = 0;
    uint16_t bit_off = 0;
    uint8_t  usages[16]; uint8_t nusage = 0;
    uint16_t usage_min = 0, usage_max = 0;
    bool     in_mouse = false, got_x = false, got_y = false;
    int      depth = 0;

    int i = 0;
    while (i < len) {
        uint8_t b0 = d[i++];
        uint8_t bsize = b0 & 0x03; if (bsize == 3) bsize = 4;
        uint8_t btype = (uint8_t)((b0 >> 2) & 0x03);
        uint8_t btag  = (uint8_t)(b0 >> 4);

        if (b0 == 0xFE) {                     /* long item — ข้าม */
            if (i >= len) break;
            uint8_t dsize = d[i];
            i += 2 + dsize;
            continue;
        }
        if (i + bsize > len) break;

        uint32_t val = 0;
        for (uint8_t k = 0; k < bsize; k++) val |= ((uint32_t)d[i + k]) << (8 * k);
        i += bsize;

        if (btype == 1) {                     /* ---- Global ---- */
            switch (btag) {
                case 0x0: usage_page = (uint16_t)val; break;
                case 0x7: rep_size   = (uint8_t)val;  break;
                case 0x8:                                   /* Report ID */
                    rep_id  = (uint8_t)val;
                    bit_off = 0;                            /* เริ่มนับใหม่ต่อ report */
                    break;
                case 0x9: rep_count  = (uint8_t)val;  break;
                default: break;
            }
        } else if (btype == 2) {              /* ---- Local ---- */
            switch (btag) {
                case 0x0: if (nusage < sizeof(usages)) usages[nusage++] = (uint8_t)val; break;
                case 0x1: usage_min = (uint16_t)val; break;
                case 0x2: usage_max = (uint16_t)val; break;
                default: break;
            }
        } else if (btype == 0) {              /* ---- Main ---- */
            if (btag == 0xA) {                /* Collection */
                depth++;
                if (depth == 1 && usage_page == 0x01 && nusage && usages[0] == 0x02)
                    in_mouse = true;          /* Generic Desktop / Mouse */
                nusage = 0;
            } else if (btag == 0xC) {         /* End Collection */
                if (--depth <= 0) { depth = 0; if (got_x && got_y) break; in_mouse = false; }
                nusage = 0;
            } else if (btag == 0x8) {         /* Input */
                bool constant = (val & 0x01) != 0;
                uint16_t span = (uint16_t)(rep_size * rep_count);

                if (in_mouse && !constant) {
                    if (usage_page == 0x09 && !m->btn_cnt) {        /* Button page */
                        m->btn_off = bit_off;
                        m->btn_cnt = (uint8_t)((usage_max >= usage_min)
                                   ? (usage_max - usage_min + 1) : rep_count);
                        if (m->btn_cnt > rep_count) m->btn_cnt = rep_count;
                        if (m->btn_cnt > 8) m->btn_cnt = 8;
                    } else if (usage_page == 0x01 || usage_page == 0x0C) {
                        /* usage ถูกไล่ให้ทีละช่องตามลำดับที่ประกาศ */
                        for (uint8_t u = 0; u < nusage && u < rep_count; u++) {
                            uint16_t off = (uint16_t)(bit_off + u * rep_size);
                            switch (usages[u]) {
                                case 0x30: m->x_off = off; m->x_size = rep_size; got_x = true; break;
                                case 0x31: m->y_off = off; m->y_size = rep_size; got_y = true; break;
                                case 0x38: m->w_off = off; m->w_size = rep_size; break;
                                default: break;
                            }
                        }
                    }
                    if (m->report_id == 0) m->report_id = rep_id;
                }
                bit_off = (uint16_t)(bit_off + span);
                nusage = 0; usage_min = usage_max = 0;
            } else {                           /* Output / Feature */
                nusage = 0; usage_min = usage_max = 0;
            }
        }
    }

    m->report_id  = rep_id ? m->report_id : 0;
    m->total_bits = bit_off;
    m->valid      = got_x && got_y;
    return m->valid;
}

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

    if (h->kind == ESP32_USBH_HID_KIND_MOUSE && h->mmap.valid) {
        /* --- โหมด report protocol: ใช้ offset จาก descriptor จริง --- */
        const uint8_t *p = b;
        int            m = n;
        if (h->mmap.report_id) {
            if (n < 1 || b[0] != h->mmap.report_id) {
                /* ⚠️ ดองเกิลไร้สายมักส่งหลาย report ID ปนมาบน endpoint เดียวกัน
                 *    (เช่น ID 2 = ปุ่มมัลติมีเดีย, ID 3 = สถานะแบตเตอรี่)
                 *    ถ้าปล่อยให้โพสต์เป็น kind = MOUSE ทั้งที่ค่าทั้งก้อนเป็นศูนย์
                 *    ฝั่ง LVGL จะเห็นเป็น "ปล่อยปุ่ม" สลับกับ "กดปุ่ม" รัว ๆ
                 *    = อาการกดปุ่มแล้วแอปกระตุก/รวน  -> ต้องกันไม่ให้ไปถึง UI */
                ev.hid.kind = ESP32_USBH_HID_KIND_OTHER;
                goto skip_parse;
            }
            p = b + 1; m = n - 1;
        }
        int32_t dx = bits_get(p, m, h->mmap.x_off, h->mmap.x_size, true);
        int32_t dy = bits_get(p, m, h->mmap.y_off, h->mmap.y_size, true);
        int32_t wh = h->mmap.w_size ? bits_get(p, m, h->mmap.w_off, h->mmap.w_size, true) : 0;
        int32_t bt = h->mmap.btn_cnt ? bits_get(p, m, h->mmap.btn_off, h->mmap.btn_cnt, false) : 0;

        if (dx >  32767) dx =  32767;  
        if (dx < -32768) dx = -32768;
        if (dy >  32767) dy =  32767;  
        if (dy < -32768) dy = -32768;
        if (wh >    127) wh =    127;  
        if (wh <   -128) wh =   -128;

        /* คาบ 2 ms = 500 report/วินาที ต่ออุปกรณ์ ส่วนใหญ่ "ไม่มีอะไรเปลี่ยน"
         * ถ้าโพสต์หมดจะถม event queue (ลึกแค่ ESP32_USBH_EVENT_QUEUE_LEN)
         * จนเหตุการณ์อื่นตกคิว -> UI กระตุก  ตัดทิ้งตั้งแต่ตรงนี้ */
        if (dx == 0 && dy == 0 && wh == 0 && (uint8_t)bt == h->prev_btn) return;
        h->prev_btn = (uint8_t)bt;

        ev.hid.mouse.buttons = (uint8_t)bt;
        ev.hid.mouse.dx      = (int16_t)dx;
        ev.hid.mouse.dy      = (int16_t)dy;
        ev.hid.mouse.wheel   = (int8_t)wh;
    } else if (h->kind == ESP32_USBH_HID_KIND_MOUSE && n >= 3) {
        if (b[0] == h->prev_btn && (int8_t)b[1] == 0 && (int8_t)b[2] == 0 &&
            (n < 4 || (int8_t)b[3] == 0)) return;
        h->prev_btn = b[0];

        /* --- โหมด boot protocol: layout ตายตัว --- */
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

skip_parse:
    priv_event_post(&ev);
}

/* ========================================================================
 *  task อ่าน report
 * ======================================================================== */

static void hid_task(void *arg)
{
    hdev_t   *h = (hdev_t *)arg;
    uint32_t  timeouts = 0;
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

        /* อุปกรณ์ FS/LS ที่อยู่หลัง hub ต้องใช้ split transaction ซึ่งกิน
         * ช่อง microframe มาก การ poll ถี่ ๆ (bInterval 1-2 ms) ทำให้ชน
         * กับ periodic schedule ของ hub จนแชนเนลค้าง — บีบขั้นต่ำเป็น 8 ms
         * (125 Hz ยังลื่นเกินพอสำหรับเมาส์บน UI) */
        if (hid->hport->parent && h->speed != ESP32_USBH_SPEED_HIGH &&
            p < PRIV_HID_SPLIT_MIN_POLL_MS) {
            ESP_LOGI(TAG, "%s: อยู่หลัง hub -> ขยับ poll จาก %u เป็น %u ms "
                          "(กัน split transaction ชนกัน)",
                     h->devname, (unsigned)p, PRIV_HID_SPLIT_MIN_POLL_MS);
            p = PRIV_HID_SPLIT_MIN_POLL_MS;
        }
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

        memset(&h->mmap, 0, sizeof(h->mmap));

        if (h->kind == ESP32_USBH_HID_KIND_MOUSE) {
            /* ── เมาส์: ใช้ report protocol + แกะ descriptor ──
             *
             * เหตุผล: เมาส์ 2.4G / dual-mode จำนวนมากประกาศ boot subclass
             * แต่ไม่ทำตาม SET_PROTOCOL(boot) ยังส่ง report แบบมี Report ID
             * นำหน้าอยู่ดี -> ถ้าเชื่อ layout ของ boot จะอ่านเพี้ยนทั้งก้อน
             * (กดปุ่มขวาแล้วเคอร์เซอร์วิ่งไปขวา เพราะ 0x02 ไปตกที่ช่อง dx)
             *
             * การอ่าน descriptor จริงใช้ได้กับทุกเมาส์ และยังได้ของแถม:
             * แกน 16 บิต (เลื่อนเร็วไม่ตัน) + ปุ่มข้าง + ล้อแนวนอน
             */
            /* ⚠️ usbh_hid_get_report_descriptor() ส่งบัฟเฟอร์นี้ "ตรง ๆ" เข้า
             *    usbh_control_transfer() ซึ่ง assert ว่าต้อง align ตาม
             *    CONFIG_USB_ALIGN_SIZE (= 64) — malloc() ธรรมดาให้แค่ 4/8 ไบต์
             *    -> ASSERT FAIL @ usb_hc_dwc2.c:983 */
            uint8_t *rd = (uint8_t *)heap_caps_aligned_alloc(
                              CONFIG_USB_ALIGN_SIZE, PRIV_HID_DESC_MAX,
                              MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_CACHE_ALIGNED);
            if (rd) {
                int rl = usbh_hid_get_report_descriptor(hid, rd, PRIV_HID_DESC_MAX);
                if (rl > 0 && parse_mouse_desc(rd, rl, &h->mmap)) {
                    h->boot = false;
                    hid_set_protocol_fixed(hid, HID_PROTOCOL_REPORT);
                    ESP_LOGI(TAG, "%s: report protocol | id=%u X@%u/%ub Y@%u/%ub "
                                  "W@%u/%ub ปุ่ม@%u x%u",
                             h->devname, h->mmap.report_id,
                             h->mmap.x_off, h->mmap.x_size,
                             h->mmap.y_off, h->mmap.y_size,
                             h->mmap.w_off, h->mmap.w_size,
                             h->mmap.btn_off, h->mmap.btn_cnt);
                } else {
                    ESP_LOGW(TAG, "%s: แกะ report descriptor ไม่ได้ (%d ไบต์) "
                                  "-> ถอยไปใช้ boot protocol", h->devname, rl);
                }
                heap_caps_free(rd);
            } else {
                ESP_LOGW(TAG, "%s: จองบัฟเฟอร์ descriptor ไม่ได้ -> ใช้ boot protocol",
                         h->devname);
            }
        }

#if ESP32_USBH_HID_FORCE_BOOT
        if (h->subclass == HID_SUBCLASS_BOOTIF && !h->mmap.valid) {
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

        /* ⚠️⚠️ ต้องเป็น "blocking" (timeout > 0) เท่านั้น — ห้ามใช้ async (timeout = 0)
         *
         * เหตุผล อยู่ที่ dwc2_urb_waitup() (usb_hc_dwc2.c:1129):
         *     if (urb->timeout) usb_osal_sem_give(chan->waitsem);   <- blocking
         *     else              dwc2_chan_free(chan);               <- async
         *
         * โหมด async จะ "คืนแชนเนลตั้งแต่อยู่ใน ISR" (chan->urb = NULL, :498-500)
         * ทั้งที่ ISR เพิ่งเคลียร์ HCINT ไปเฉพาะบิตที่อ่านมาตอนต้นฟังก์ชัน
         * ถ้ามีบิตใหม่ถูกเซ็ตระหว่างนั้น (เกิดง่ายมากกับ interrupt EP คาบ 2 ms)
         * HAINT จะยังค้าง -> ISR ถูกเรียกซ้ำ -> urb = chan->urb = NULL
         * -> urb->actual_length  =  Load access fault MTVAL 0x0C @ :1260
         *
         * โหมด blocking ปลอดภัยเพราะ dwc2_chan_free() ถูกเรียกจาก "task"
         * หลัง ISR จบไปเรียบร้อยแล้ว (usb_hc_dwc2.c:1085)
         *
         * แล้ว timeout ล่ะ? ตั้งให้ยาวจนไม่มีวันหมด (30 วินาที) เพราะทางออก
         * errout_timeout: จะเรียก usbh_kill_urb() ซึ่งก็ไปจบที่ dwc2_chan_free()
         * อีกเหมือนกัน -> นั่นคือต้นเหตุของบั๊ก "เสียบผ่าน hub แล้วแครช" รอบก่อน
         * การปลุก task ตอนถอดสาย ใช้ usbh_kill_urb() จาก usbh_hid_stop() แทน
         * (ตอนนั้น urb->timeout ยังไม่ถูกเคลียร์ -> เข้าทาง sem_give ที่ปลอดภัย)
         */
        usbh_int_urb_fill(urb, hid->hport, hid->intin, h->buf, len,
                          PRIV_HID_URB_TIMEOUT_MS, NULL, NULL);
        int ret = usbh_submit_urb(urb);

        if (!h->alive) break;

        if (ret == 0) {
            timeouts = 0;
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

        } else if (ret == -USB_ERR_NAK) {
            vTaskDelay(pdMS_TO_TICKS(h->poll_override ? h->poll_override : h->poll_ms));

        } else if (ret == -USB_ERR_SHUTDOWN || ret == -USB_ERR_NOTCONN) {
            break;                                   /* ถอดสาย / ถูก kill */

        } else if (ret == -USB_ERR_TIMEOUT) {
            /* แชนเนลค้าง — CherryUSB kill URB ให้แล้ว แค่ยิงใหม่ก็กลับมาทำงาน
             * (ต้องมีแพตช์ NULL guard ใน usb_hc_dwc2.c ก่อน ไม่งั้นแครช) */
            h->error_count++;
            if (++timeouts % PRIV_HID_TIMEOUT_LOG_EVERY == 1) {
                ESP_LOGW(TAG, "%s: URB ค้าง %lu ครั้ง -> ยิงใหม่ "
                              "(มักเกิดตอน LVGL ยึด CPU/บัสนาน)",
                         h->devname, (unsigned long)timeouts);
            }
            urb->data_toggle = 0;                 /* เริ่ม toggle ใหม่ กัน DTERR */
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;

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
    if (!h->buf) {
        h->buf = priv_alloc_dma(ESP32_USBH_HID_REPORT_SIZE,
                     ESP32_USBH_HID_REPORT_SIZE,   /* เล็กมาก ไม่ลดขนาด */
                     NULL, "HID report");
        if (!h->buf) { ESP_LOGE(TAG, "จอง buffer ไม่พอ"); h->used = false; return; }
    }

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

    /* ปลุก task ที่กำลังบล็อกรอ report อยู่
     *
     * ปลอดภัยเพราะตอนนี้ urb->timeout ยังเป็นค่าเดิม (ไม่ใช่ 0)
     * usbh_kill_urb() จึงเข้าทาง  dwc2_halt() + usb_osal_sem_give()
     * ไม่ใช่ dwc2_chan_free() ใน ISR  (ดูคอมเมนต์ยาวใน hid_task)
     * การคืนแชนเนลจะไปเกิดในบริบทของ task เองหลัง sem_take คืนค่า */
    if (hid_class && hid_class->intin_urb.hcpriv) {
        usbh_kill_urb(&hid_class->intin_urb);
    }

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

    if (h->buf) { priv_mem_free(h->buf); h->buf = NULL; }
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
        if (!s_h[i].last_mtx) return ESP_ERR_NO_MEM;
        /* buf จองตอนเสียบอุปกรณ์จริง (ดู usbh_hid_run) */
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
