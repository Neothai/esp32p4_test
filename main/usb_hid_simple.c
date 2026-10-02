/*
 * usb_hid_simple.c — อ่าน report จากเมาส์/คีย์บอร์ด USB แล้ว print
 * ESP32-P4 + CherryUSB v1.6.1
 *
 * หลักการทำงาน (อ้างอิงซอร์ส v1.6.1 ที่ตรวจแล้ว):
 *
 *  - usbh_hid_run()/usbh_hid_stop() เป็น __WEAK (usbh_hid.c:546,551) -> override ได้
 *  - usbh_hid_run() ถูกเรียกจาก "เธรด hub" ซึ่งมี priority 24 (สูงสุด)
 *    => ห้ามบล็อก! ที่นี่จึงแค่ xTaskCreate แล้วคืนทันที
 *  - usbh_submit_urb() จะ "บล็อก" ก็ต่อเมื่อ urb->timeout > 0
 *    (usb_hc_dwc2.c:1076 -> usb_osal_sem_take(chan->waitsem, urb->timeout))
 *    => ใช้โหมดบล็อกในแท task ของเราเอง จะ printf ได้อิสระ ไม่ต้องยุ่งกับ ISR
 *  - interrupt IN endpoint ที่ไม่มีข้อมูล จะคืน -USB_ERR_NAK (-10) ทันที
 *    (usb_hc_dwc2.c:1234 — ฮาร์ดแวร์ไม่ retry ให้) => ต้อง delay ตาม bInterval เอง
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "usbh_core.h"
#include "usbh_hid.h"
#include "usb_hid.h"

#include "usb_hid_simple.h"

static const char *TAG = "USB_HID";

#ifndef CONFIG_USBHOST_MAX_HID_CLASS
#define CONFIG_USBHOST_MAX_HID_CLASS 4
#endif
#define MAXD CONFIG_USBHOST_MAX_HID_CLASS

#define RPT_BUF_SZ 64   /* ใหญ่พอสำหรับ interrupt EP ของ HID ทุกตัว */

typedef struct {
    struct usbh_hid *hid;
    volatile bool    used;
    volatile bool    alive;    /* false = สั่งให้ task ออก */
    volatile bool    running;  /* task ยังอยู่ */
    uint8_t          subclass;
    uint8_t          protocol; /* 1 = keyboard, 2 = mouse */
    uint8_t          poll_ms;
    char             name[CONFIG_USBHOST_DEV_NAMELEN];
    uint8_t          prev_keys[6];
    uint8_t          prev_mod;
} hid_dev_t;

static hid_dev_t s_dev[MAXD];

/* buffer สำหรับ DMA — ต้อง non-cacheable + align ตาม CONFIG_USB_ALIGN_SIZE (64 บน P4)
 * ไม่งั้น usbh_submit_urb() จะ assert เรื่อง alignment */
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t s_buf[MAXD][RPT_BUF_SZ];

/* ============================================================
 *  ตาราง HID Usage ID -> ตัวอักษร (Boot Keyboard)
 * ============================================================ */
static const char kc_lower[] =
    /* 0x00 */ "\0\0\0\0" "abcdefghijklmnopqrstuvwxyz"   /* 0x04-0x1D */
    /* 0x1E */ "1234567890"                              /* 0x1E-0x27 */
    /* 0x28 */ "\n\x1b\b\t "                             /* Enter Esc BS Tab Space */
    /* 0x2D */ "-=[]\\#;'`,./";                          /* 0x2D-0x38 */

static const char kc_upper[] =
    /* 0x00 */ "\0\0\0\0" "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    /* 0x1E */ "!@#$%^&*()"
    /* 0x28 */ "\n\x1b\b\t "
    /* 0x2D */ "_+{}|~:\"~<>?";

static char keycode_to_char(uint8_t kc, bool shift)
{
    const char *t = shift ? kc_upper : kc_lower;
    if (kc <= 0x38) {
        char c = t[kc];
        return c ? c : 0;
    }
    return 0;
}

static const char *keycode_name(uint8_t kc)
{
    switch (kc) {
        case 0x28: return "ENTER";
        case 0x29: return "ESC";
        case 0x2A: return "BACKSPACE";
        case 0x2B: return "TAB";
        case 0x2C: return "SPACE";
        case 0x39: return "CAPSLOCK";
        case 0x3A: return "F1";  case 0x3B: return "F2";  case 0x3C: return "F3";
        case 0x3D: return "F4";  case 0x3E: return "F5";  case 0x3F: return "F6";
        case 0x40: return "F7";  case 0x41: return "F8";  case 0x42: return "F9";
        case 0x43: return "F10"; case 0x44: return "F11"; case 0x45: return "F12";
        case 0x46: return "PRTSCR";
        case 0x4A: return "HOME";  case 0x4B: return "PGUP";
        case 0x4C: return "DEL";   case 0x4D: return "END";   case 0x4E: return "PGDN";
        case 0x4F: return "RIGHT"; case 0x50: return "LEFT";
        case 0x51: return "DOWN";  case 0x52: return "UP";
        default:   return NULL;
    }
}

static void print_modifiers(uint8_t m)
{
    if (!m) return;
    printf("  [");
    if (m & 0x01) printf("LCtrl ");
    if (m & 0x02) printf("LShift ");
    if (m & 0x04) printf("LAlt ");
    if (m & 0x08) printf("LGui ");
    if (m & 0x10) printf("RCtrl ");
    if (m & 0x20) printf("RShift ");
    if (m & 0x40) printf("RAlt ");
    if (m & 0x80) printf("RGui ");
    printf("]");
}

/* ============================================================
 *  class request — เลี่ยงบั๊ก wIndex ของ CherryUSB
 *
 *  usbh_hid.c:125 (set_protocol) และ :167/:185 (set/get_report)
 *  ใส่ setup->wIndex = 0 ตายตัว ทั้งที่ควรเป็น hid_class->intf
 *  อุปกรณ์ composite (คีย์บอร์ดที่มี interface ปุ่มมัลติมีเดียแยก)
 *  จะโดนส่ง request ผิด interface -> STALL หรือไม่มีผล
 * ============================================================ */
static int hid_set_protocol_fixed(struct usbh_hid *hid, uint8_t protocol)
{
    struct usb_setup_packet *setup;

    if (!hid || !hid->hport) return -USB_ERR_INVAL;
    setup = hid->hport->setup;

    setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_CLASS |
                           USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = HID_REQUEST_SET_PROTOCOL;
    setup->wValue   = protocol;
    setup->wIndex   = hid->intf;     /* <- ของเดิมใส่ 0 */
    setup->wLength  = 0;

    return usbh_control_transfer(hid->hport, setup, NULL);
}

/* ============================================================
 *  ถอดความ report
 * ============================================================ */
static void dump_raw(const char *name, const uint8_t *b, int n)
{
#if USB_HID_DUMP_RAW
    printf("[%s] raw(%d):", name, n);
    for (int i = 0; i < n; i++) printf(" %02X", b[i]);
    printf("\n");
#else
    (void)name; (void)b; (void)n;
#endif
}

static void decode_mouse(hid_dev_t *d, const uint8_t *b, int n)
{
    if (n < 3) return;

    uint8_t btn = b[0];
    int8_t  dx  = (int8_t)b[1];
    int8_t  dy  = (int8_t)b[2];
    int8_t  wh  = (n >= 4) ? (int8_t)b[3] : 0;

    printf("[%s] MOUSE  dx=%+4d dy=%+4d wheel=%+2d  btn=%c%c%c\n",
           d->name, dx, dy, wh,
           (btn & 0x01) ? 'L' : '-',
           (btn & 0x02) ? 'R' : '-',
           (btn & 0x04) ? 'M' : '-');
}

static void decode_keyboard(hid_dev_t *d, const uint8_t *b, int n)
{
    if (n < 8) return;

    uint8_t mod  = b[0];
    bool    shift = (mod & 0x22) != 0;      /* LShift | RShift */
    const uint8_t *keys = &b[2];

    /* หาปุ่มที่ "เพิ่งกด" (อยู่ใน report ใหม่ แต่ไม่อยู่ในอันเก่า) */
    for (int i = 0; i < 6; i++) {
        uint8_t kc = keys[i];
        if (kc == 0 || kc == 0x01 /* ErrorRollOver */) continue;

        bool was_down = false;
        for (int j = 0; j < 6; j++) {
            if (d->prev_keys[j] == kc) { was_down = true; break; }
        }
        if (was_down) continue;

        const char *nm = keycode_name(kc);
        char c = keycode_to_char(kc, shift);

        printf("[%s] KEY DOWN  0x%02X", d->name, kc);
        if (nm)                      printf("  <%s>", nm);
        else if (c >= 0x20 && c < 0x7F) printf("  '%c'", c);
        print_modifiers(mod);
        printf("\n");
    }

    /* หาปุ่มที่ "เพิ่งปล่อย" */
    for (int i = 0; i < 6; i++) {
        uint8_t kc = d->prev_keys[i];
        if (kc == 0 || kc == 0x01) continue;

        bool still_down = false;
        for (int j = 0; j < 6; j++) {
            if (keys[j] == kc) { still_down = true; break; }
        }
        if (!still_down) {
            const char *nm = keycode_name(kc);
            char c = keycode_to_char(kc, false);
            printf("[%s] KEY UP    0x%02X", d->name, kc);
            if (nm)                      printf("  <%s>", nm);
            else if (c >= 0x20 && c < 0x7F) printf("  '%c'", c);
            printf("\n");
        }
    }

    if (mod != d->prev_mod) {
        printf("[%s] MOD 0x%02X", d->name, mod);
        print_modifiers(mod);
        printf("\n");
    }

    memcpy(d->prev_keys, keys, 6);
    d->prev_mod = mod;
}

/* ============================================================
 *  task อ่าน report (หนึ่งตัวต่อหนึ่ง interface)
 * ============================================================ */
static void hid_task(void *arg)
{
    hid_dev_t *d   = (hid_dev_t *)arg;
    int        idx = (int)(d - s_dev);
    uint8_t   *buf = s_buf[idx];

    d->running = true;

    /* รอให้ enumeration นิ่งก่อน แล้วค่อยยิง control transfer */
    vTaskDelay(pdMS_TO_TICKS(50));

    struct usbh_hid *hid = d->hid;
    if (!d->alive || !hid || !hid->intin) goto out;

    /* ---- อ่านข้อมูล interface ---- */
    {
        struct usb_interface_descriptor *idesc =
            &hid->hport->config.intf[hid->intf].altsetting[0].intf_desc;

        d->subclass = idesc->bInterfaceSubClass;
        d->protocol = idesc->bInterfaceProtocol;

        uint8_t  bi  = hid->intin->bInterval;
        uint16_t mps = hid->intin->wMaxPacketSize & 0x7FF;

        /* HS: bInterval เป็น microframe exponent, FS/LS: เป็น ms ตรง ๆ */
        if (hid->hport->speed == USB_SPEED_HIGH) {
            uint32_t us = (bi > 0) ? (1u << (bi - 1)) * 125u : 1000u;
            d->poll_ms = (us / 1000u) ? (uint8_t)(us / 1000u) : 1;
        } else {
            d->poll_ms = (bi > 0) ? bi : 10;
        }
        if (d->poll_ms < 1)  d->poll_ms = 1;
        if (d->poll_ms > 32) d->poll_ms = 32;

        const char *kind = (d->subclass == HID_SUBCLASS_BOOTIF)
                         ? ((d->protocol == HID_PROTOCOL_KEYBOARD) ? "Boot Keyboard"
                         :  (d->protocol == HID_PROTOCOL_MOUSE)    ? "Boot Mouse"
                         :  "Boot (other)")
                         : "Generic HID";

        ESP_LOGI(TAG, "%s  <%s>  intf=%u  subclass=%u protocol=%u  "
                      "EP=%02X Mps=%u bInterval=%u -> poll %u ms  speed=%s",
                 d->name, kind, hid->intf, d->subclass, d->protocol,
                 hid->intin->bEndpointAddress, mps, bi, d->poll_ms,
                 (hid->hport->speed == USB_SPEED_HIGH) ? "HS" :
                 (hid->hport->speed == USB_SPEED_FULL) ? "FS" : "LS");

#if USB_HID_FORCE_BOOT_PROTOCOL
        if (d->subclass == HID_SUBCLASS_BOOTIF) {
            int r = hid_set_protocol_fixed(hid, HID_PROTOCOL_BOOT);
            if (r < 0) ESP_LOGW(TAG, "%s: SET_PROTOCOL(boot) failed: %d", d->name, r);
            else       ESP_LOGI(TAG, "%s: boot protocol OK", d->name);

            /* SET_IDLE(0,0) = ส่ง report เฉพาะตอนค่าเปลี่ยน ไม่ส่งซ้ำ ๆ
             * ตัวนี้ wIndex ถูกอยู่แล้วใน CherryUSB (usbh_hid.c:65) */
            r = usbh_hid_set_idle(hid, 0, 0);
            if (r < 0) ESP_LOGW(TAG, "%s: SET_IDLE failed: %d (ไม่เป็นไร)", d->name, r);
        }
#endif
    }

    /* ---- วนอ่าน report ---- */
    while (d->alive) {
        struct usbh_urb *urb = &hid->intin_urb;
        uint32_t len = hid->intin->wMaxPacketSize & 0x7FF;
        if (len > RPT_BUF_SZ) len = RPT_BUF_SZ;

        /* timeout > 0 => โหมดบล็อก, complete = NULL */
        usbh_int_urb_fill(urb, hid->hport, hid->intin, buf, len, 1000, NULL, NULL);

        int ret = usbh_submit_urb(urb);

        if (ret == 0) {
            int n = urb->actual_length;
            if (n > 0) {
                dump_raw(d->name, buf, n);

                if (d->subclass == HID_SUBCLASS_BOOTIF &&
                    d->protocol == HID_PROTOCOL_MOUSE) {
                    decode_mouse(d, buf, n);
                } else if (d->subclass == HID_SUBCLASS_BOOTIF &&
                           d->protocol == HID_PROTOCOL_KEYBOARD) {
                    decode_keyboard(d, buf, n);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(d->poll_ms));

        } else if (ret == -USB_ERR_NAK || ret == -USB_ERR_TIMEOUT) {
            /* ไม่มีข้อมูล = ปกติมาก ของ interrupt endpoint */
            vTaskDelay(pdMS_TO_TICKS(d->poll_ms));

        } else if (ret == -USB_ERR_SHUTDOWN || ret == -USB_ERR_NOTCONN) {
            ESP_LOGI(TAG, "%s: ถอดออกแล้ว (%d)", d->name, ret);
            break;

        } else if (ret == -USB_ERR_STALL) {
            ESP_LOGW(TAG, "%s: endpoint STALL -> หยุดอ่าน", d->name);
            break;

        } else {
            ESP_LOGW(TAG, "%s: urb error %d", d->name, ret);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

out:
    ESP_LOGI(TAG, "%s: task exit", d->name);
    d->running = false;
    vTaskDelete(NULL);
}

/* ============================================================
 *  hook ของ CherryUSB (override __WEAK)
 *  ทั้งสองตัวรันบน "เธรด hub" priority 24 -> ห้ามบล็อกนาน
 * ============================================================ */
void usbh_hid_run(struct usbh_hid *hid_class)
{
    int k = -1;
    for (int i = 0; i < MAXD; i++) {
        if (!s_dev[i].used) { k = i; break; }
    }
    if (k < 0) {
        ESP_LOGE(TAG, "ไม่มี slot ว่าง (เพิ่ม CONFIG_USBHOST_MAX_HID_CLASS)");
        return;
    }

    hid_dev_t *d = &s_dev[k];
    memset(d, 0, sizeof(*d));
    d->hid   = hid_class;
    d->used  = true;
    d->alive = true;

    const char *dn = hid_class->hport->config.intf[hid_class->intf].devname;
    snprintf(d->name, sizeof(d->name), "%s", (dn && dn[0]) ? dn : "hid?");

    char tn[16];
    snprintf(tn, sizeof(tn), "hid%d", k);

    if (xTaskCreate(hid_task, tn, USB_HID_TASK_STACK, d,
                    USB_HID_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "สร้าง task ไม่สำเร็จ");
        d->used = false;
    }
}

void usbh_hid_stop(struct usbh_hid *hid_class)
{
    for (int i = 0; i < MAXD; i++) {
        if (!s_dev[i].used || s_dev[i].hid != hid_class) continue;

        s_dev[i].alive = false;

        /* ต้องรอให้ task ออกจริงก่อนคืน เพราะ usbh_hid_disconnect() จะ
         * usbh_hid_class_free(hid_class) ทันทีหลังจากนี้ (usbh_hid.c:283)
         * ถ้าไม่รอ = use-after-free
         * urb ถูก usbh_kill_urb() ไปแล้วก่อนหน้า task จึงหลุดออกมาเร็ว */
        for (int t = 0; t < 100 && s_dev[i].running; t++) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (s_dev[i].running) {
            ESP_LOGE(TAG, "%s: task ไม่ยอมจบใน 500ms!", s_dev[i].name);
        }

        s_dev[i].used = false;
        s_dev[i].hid  = NULL;
        return;
    }
}

int usb_hid_device_count(void)
{
    int n = 0;
    for (int i = 0; i < MAXD; i++) if (s_dev[i].used) n++;
    return n;
}

/* ต้องถูกเรียกจาก app_main() ไม่งั้น linker จะทิ้งทั้ง object file นี้
 * แล้ว override ของ usbh_hid_run() จะไม่มีผล (ดูคำอธิบายใน .h) */
void usb_hid_simple_init(void)
{
    memset(s_dev, 0, sizeof(s_dev));
    ESP_LOGW(TAG, "usb_hid_simple linked OK — รองรับ %d อุปกรณ์, "
                  "boot_protocol=%d dump_raw=%d",
             MAXD, USB_HID_FORCE_BOOT_PROTOCOL, USB_HID_DUMP_RAW);
}
