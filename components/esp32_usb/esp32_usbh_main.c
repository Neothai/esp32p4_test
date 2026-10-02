/*
 * esp32_usbh_main.c — แกนกลาง USB Host
 *
 * โครงสร้างเธรด
 *   [ISR ของ DWC2]        -> ไม่เรียก callback ผู้ใช้เด็ดขาด
 *   [เธรด hub prio 24]    -> usbh_*_run/stop + event handler ของ CherryUSB
 *                            ทำแค่ "คัดลอกข้อมูล + ส่งเข้าคิว" แล้วคืนทันที
 *   [event task prio 6]   -> เรียก callback ของผู้ใช้  <-- ปลอดภัย บล็อกได้
 *   [supervisor task]     -> รีสตาร์ท host / วน VBUS (งานที่บล็อกนาน)
 */

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"

#include "usbh_core.h"
#include "esp32_usbh_main.h"
#if ESP32_USBH_ENABLE_MSC
#include "esp32_usbh_msc.h"
#endif
#include "esp32_usb_private.h"

static const char *TAG = PRIV_TAG_MAIN;

#if ESP32_USBH_BUSID == 0
#  define PRIV_USB_BASE ESP_USB_HS0_BASE
#else
#  define PRIV_USB_BASE ESP_USB_FS0_BASE
#endif

/* ========================================================================
 *  สถานะภายใน
 * ======================================================================== */

typedef enum { SUP_NONE = 0, SUP_HOST_RESTART, SUP_VBUS_CYCLE } sup_cmd_t;

static struct {
    volatile esp32_usbh_state_t state;

    QueueHandle_t   evt_q;
    QueueHandle_t   sup_q;
    TaskHandle_t    evt_task;
    TaskHandle_t    sup_task;

    SemaphoreHandle_t api_mtx;    /* ป้องกัน init/deinit/restart ซ้อนกัน */
    SemaphoreHandle_t dev_mtx;    /* ป้องกันตารางอุปกรณ์ */
    SemaphoreHandle_t ep0_mtx;    /* ป้องกัน ep0_request_buffer ที่แชร์ทั้งบัส */

    esp32_usbh_event_cb_t cb;
    void                 *cb_ctx;
    SemaphoreHandle_t     cb_mtx;

    esp32_usbh_devinfo_t  dev[ESP32_USBH_MAX_DEVICES];

    esp32_usbh_stats_t    stats;
    portMUX_TYPE          stats_lock;

    volatile uint32_t     enum_pending;     /* CONNECTED ที่ยังไม่ CONFIGURED */
    volatile bool         restart_busy;
    volatile int64_t      last_restart_us;
    volatile bool         fetch_strings;
    volatile bool         auto_mount;

    uint32_t              hw_max_sectors;
} S;

static bool s_inited = false;

/* ========================================================================
 *  helper
 * ======================================================================== */

BaseType_t priv_task_create(TaskFunction_t fn, const char *name, uint32_t stack,
                            void *arg, UBaseType_t prio, TaskHandle_t *out)
{
#if ESP32_USBH_TASK_CORE < 0
    return xTaskCreate(fn, name, stack, arg, prio, out);
#else
    return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, out,
                                   ESP32_USBH_TASK_CORE);
#endif
}

bool priv_ep0_lock(uint32_t timeout_ms)
{
    if (!S.ep0_mtx) return false;
    return xSemaphoreTakeRecursive(S.ep0_mtx, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void priv_ep0_unlock(void)
{
    if (S.ep0_mtx) xSemaphoreGiveRecursive(S.ep0_mtx);
}

static void stats_inc(uint32_t *field)
{
    portENTER_CRITICAL(&S.stats_lock);
    (*field)++;
    portEXIT_CRITICAL(&S.stats_lock);
}

static esp32_usbh_speed_t map_speed(uint8_t s)
{
    switch (s) {
        case USB_SPEED_LOW:  return ESP32_USBH_SPEED_LOW;
        case USB_SPEED_FULL: return ESP32_USBH_SPEED_FULL;
        case USB_SPEED_HIGH: return ESP32_USBH_SPEED_HIGH;
        default:             return ESP32_USBH_SPEED_UNKNOWN;
    }
}

const char *esp32_usbh_speed_str(esp32_usbh_speed_t s)
{
    switch (s) {
        case ESP32_USBH_SPEED_LOW:  return "LS";
        case ESP32_USBH_SPEED_FULL: return "FS";
        case ESP32_USBH_SPEED_HIGH: return "HS";
        default:                    return "?";
    }
}

const char *esp32_usbh_class_str(esp32_usbh_class_t c)
{
    switch (c) {
        case ESP32_USBH_CLASS_MSC: return "MSC";
        case ESP32_USBH_CLASS_HID: return "HID";
        case ESP32_USBH_CLASS_HUB: return "HUB";
        default:                   return "?";
    }
}

const char *esp32_usbh_event_str(esp32_usbh_event_id_t id)
{
    switch (id) {
        case ESP32_USBH_EV_DEVICE_ATTACHED: return "DEVICE_ATTACHED";
        case ESP32_USBH_EV_DEVICE_DETACHED: return "DEVICE_DETACHED";
        case ESP32_USBH_EV_ENUM_FAILED:     return "ENUM_FAILED";
        case ESP32_USBH_EV_HOST_RESTARTED:  return "HOST_RESTARTED";
        case ESP32_USBH_EV_MSC_MOUNTED:     return "MSC_MOUNTED";
        case ESP32_USBH_EV_MSC_UNMOUNTED:   return "MSC_UNMOUNTED";
        case ESP32_USBH_EV_MSC_IO_ERROR:    return "MSC_IO_ERROR";
        case ESP32_USBH_EV_HID_ATTACHED:    return "HID_ATTACHED";
        case ESP32_USBH_EV_HID_DETACHED:    return "HID_DETACHED";
        case ESP32_USBH_EV_HID_REPORT:      return "HID_REPORT";
        default:                            return "?";
    }
}

/* ========================================================================
 *  คิวเหตุการณ์
 * ======================================================================== */

bool priv_event_post(const esp32_usbh_event_t *ev)
{
    if (!S.evt_q) return false;
    /* ห้ามบล็อก — ฟังก์ชันนี้ถูกเรียกจากเธรด hub ที่ priority 24 */
    if (xQueueSend(S.evt_q, ev, 0) != pdTRUE) {
        stats_inc(&S.stats.event_drop_count);
        return false;
    }
    return true;
}

bool priv_event_post_isr(const esp32_usbh_event_t *ev, BaseType_t *hpw)
{
    if (!S.evt_q) return false;
    if (xQueueSendFromISR(S.evt_q, ev, hpw) != pdTRUE) {
        /* ใน ISR ใช้ critical section แบบ ISR-safe ไม่ได้กับ portENTER_CRITICAL
         * ปกติ -> เพิ่มตัวนับแบบ atomic ง่าย ๆ พอ */
        S.stats.event_drop_count++;
        return false;
    }
    return true;
}

/* ==========================================================================
 *  ตัวช่วยจองหน่วยความจำ — ใช้ร่วมกันทั้งไลบรารี
 *
 *  นโยบาย
 *   - ก้อนที่ "ไม่ใช่ DMA" (FATFS object, แคช, ตาราง): ถ้าจองแล้วแรมภายใน
 *     จะเหลือต่ำกว่า ESP32_USBH_INTERNAL_RESERVE_KB ให้ย้ายไป PSRAM
 *     ปลอดภัยเต็มที่ เพราะ CPU เข้าถึงผ่านแคชตามปกติ
 *
 *   - ก้อนที่ "เป็น DMA" (bounce buffer, HID report buffer): ไม่ย้ายไป PSRAM
 *     โดยปริยาย เพราะ ESP32-P4 ไม่มี cache-coherent interconnect
 *     ถ้าแรมภายในตึง จะ "ลดขนาดลงทีละครึ่ง" แทน — ช้าลงแต่ยังทำงานได้
 *     (เปิด ESP32_USBH_ALLOW_PSRAM_DMA = 1 ได้ถ้าทดสอบเองแล้ว)
 * ========================================================================== */

#define MEM_RESERVE_BYTES ((size_t)ESP32_USBH_INTERNAL_RESERVE_KB * 1024)

size_t priv_internal_free(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void *priv_alloc(size_t size, const char *what)
{
    size_t freemem = priv_internal_free();

    if (freemem > size && (freemem - size) >= MEM_RESERVE_BYTES) {
        void *p = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (p) return p;
    }

    /* แรมภายในตึง -> ลอง PSRAM */
    void *p = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) {
        ESP_LOGW(TAG, "%s: แรมภายในเหลือ %u KB (กันไว้ %d KB) -> จอง %u ไบต์บน PSRAM แทน",
                 what ? what : "alloc", (unsigned)(freemem / 1024),
                 ESP32_USBH_INTERNAL_RESERVE_KB, (unsigned)size);
        return p;
    }

    /* PSRAM ก็ไม่มี -> ยอมกินแรมภายในที่เหลือ ดีกว่าล้มเหลว */
    p = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) ESP_LOGE(TAG, "%s: จอง %u ไบต์ไม่สำเร็จทั้ง internal และ PSRAM",
                     what ? what : "alloc", (unsigned)size);
    return p;
}

void *priv_alloc_dma(size_t want, size_t min, size_t *out_size, const char *what)
{
    const uint32_t caps_int = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_CACHE_ALIGNED;

    for (size_t sz = want; sz >= min; sz /= 2) {
        size_t freemem = priv_internal_free();

        /* เผื่อให้ระบบเสมอ ยกเว้นตอนที่เหลือขนาดต่ำสุดแล้ว — ก้อนนั้นต้องได้ */
        bool room_ok = (freemem > sz) && ((freemem - sz) >= MEM_RESERVE_BYTES);
        if (room_ok || sz == min) {
            void *p = heap_caps_aligned_alloc(CONFIG_USB_ALIGN_SIZE, sz, caps_int);
            if (p) {
                if (sz != want)
                    ESP_LOGW(TAG, "%s: แรมภายในเหลือ %u KB -> ลดขนาดจาก %u เหลือ %u ไบต์ "
                                  "(ความเร็วจะลดลง แต่ยังใช้งานได้)",
                             what ? what : "dma", (unsigned)(freemem / 1024),
                             (unsigned)want, (unsigned)sz);
                if (out_size) *out_size = sz;
                return p;
            }
        }
        if (sz == min) break;
    }

#if ESP32_USBH_ALLOW_PSRAM_DMA
    /* ทางเลือกสุดท้าย: PSRAM (ต้องมั่นใจว่า USB-OTG ของบอร์ดเข้าถึงได้จริง
     * และ CONFIG_USB_DCACHE_ENABLE เปิดอยู่เพื่อให้ CherryUSB sync แคชให้) */
    void *p = heap_caps_aligned_alloc(CONFIG_USB_ALIGN_SIZE, min,
                  MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM | MALLOC_CAP_CACHE_ALIGNED);
    if (p) {
        ESP_LOGW(TAG, "%s: จองบัฟเฟอร์ DMA %u ไบต์บน PSRAM "
                      "(เปิด ESP32_USBH_ALLOW_PSRAM_DMA ไว้)", what, (unsigned)min);
        if (out_size) *out_size = min;
        return p;
    }
#endif

    ESP_LOGE(TAG, "%s: จองบัฟเฟอร์ DMA ไม่สำเร็จแม้ขนาดต่ำสุด %u ไบต์",
             what ? what : "dma", (unsigned)min);
    if (out_size) *out_size = 0;
    return NULL;
}

void priv_mem_free(void *p)
{
    if (p) heap_caps_free(p);
}

void priv_mem_report(const char *stage)
{
    ESP_LOGI(TAG, "แรม [%s] | internal %u KB (ก้อนใหญ่สุด %u KB) | DMA %u KB | PSRAM %u KB",
             stage ? stage : "",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

static void evt_task(void *arg)
{
    (void)arg;
    esp32_usbh_event_t ev;

    for (;;) {
        if (xQueueReceive(S.evt_q, &ev, portMAX_DELAY) != pdTRUE) continue;

        esp32_usbh_event_cb_t cb = NULL;
        void *ctx = NULL;

        xSemaphoreTake(S.cb_mtx, portMAX_DELAY);
        cb  = S.cb;
        ctx = S.cb_ctx;
        xSemaphoreGive(S.cb_mtx);

        if (cb) {
            /* ผู้ใช้บล็อกได้ตามสบาย ที่นี่ไม่ใช่ ISR และไม่ใช่เธรด hub */
            cb(&ev, ctx);
        }
    }
}

/* ========================================================================
 *  ตารางอุปกรณ์
 * ======================================================================== */

void priv_fetch_strings(struct usbh_hubport *hport,
                        char *mfr, char *prod, char *serial, size_t cap)
{
    mfr[0] = prod[0] = serial[0] = '\0';

    if (!S.fetch_strings || !hport) return;

    /* ต้องถือ ep0 mutex เพราะ usbh_get_string_desc() ใช้
     * ep0_request_buffer[busid] ซึ่งเป็น buffer ร่วมของทั้งบัส */
    if (!priv_ep0_lock(PRIV_EP0_MUTEX_WAIT_MS)) return;

    uint8_t tmp[128];
    struct { uint8_t idx; char *dst; } jobs[3] = {
        { hport->device_desc.iManufacturer, mfr    },
        { hport->device_desc.iProduct,      prod   },
        { hport->device_desc.iSerialNumber, serial },
    };

    for (int i = 0; i < 3; i++) {
        if (jobs[i].idx == 0) continue;
        memset(tmp, 0, sizeof(tmp));
        /* ส่ง index จริงจาก device descriptor
         * (CherryUSB เองใช้ค่าคงที่ USB_STRING_*_INDEX ซึ่งผิดกับบางอุปกรณ์) */
        int r = usbh_get_string_desc(hport, jobs[i].idx, tmp, sizeof(tmp) - 1);
        if (r < 0) continue;              /* STALL ก็ปล่อยว่าง ไม่ทำให้พัง */
        tmp[sizeof(tmp) - 1] = '\0';
        snprintf(jobs[i].dst, cap, "%s", (const char *)tmp);
    }

    priv_ep0_unlock();
}

static void fill_devinfo(esp32_usbh_devinfo_t *d, struct usbh_hubport *hport,
                         uint8_t intf, esp32_usbh_class_t cls, const char *devname)
{
    memset(d, 0, sizeof(*d));
    d->valid      = true;
    d->dev_class  = cls;
    d->speed      = map_speed(hport->speed);
    d->vid        = hport->device_desc.idVendor;
    d->pid        = hport->device_desc.idProduct;
    d->bcd_device = hport->device_desc.bcdDevice;
    d->dev_addr   = hport->dev_addr;
    d->hub_index  = hport->parent ? hport->parent->index : 0;
    d->hub_port   = hport->port;
    d->interface  = intf;
    d->tier       = hport->depth;
    if (devname) snprintf(d->devname, sizeof(d->devname), "%s", devname);
}

void priv_devtable_add(struct usbh_hubport *hport, uint8_t intf,
                       esp32_usbh_class_t cls, const char *devname,
                       esp32_usbh_devinfo_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!hport) return;

    esp32_usbh_devinfo_t info;
    fill_devinfo(&info, hport, intf, cls, devname);
    /* ดึงสตริงก่อนเข้า mutex ของตาราง เพราะเป็น control transfer ที่ช้า */
    priv_fetch_strings(hport, info.manufacturer, info.product, info.serial,
                       ESP32_USBH_STR_LEN);

    xSemaphoreTake(S.dev_mtx, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < ESP32_USBH_MAX_DEVICES; i++) {
        if (S.dev[i].valid &&
            S.dev[i].hub_index == info.hub_index &&
            S.dev[i].hub_port  == info.hub_port &&
            S.dev[i].interface == info.interface) { slot = i; break; }
        if (slot < 0 && !S.dev[i].valid) slot = i;
    }
    if (slot >= 0) S.dev[slot] = info;
    xSemaphoreGive(S.dev_mtx);

    if (out) *out = info;

    if (slot < 0) {
        ESP_LOGW(TAG, "ตารางอุปกรณ์เต็ม (เพิ่ม ESP32_USBH_MAX_DEVICES)");
        return;
    }

    stats_inc(&S.stats.attach_count);

    esp32_usbh_event_t ev = { .id = ESP32_USBH_EV_DEVICE_ATTACHED, .dev = info };
    priv_event_post(&ev);
}

void priv_devtable_remove(struct usbh_hubport *hport, uint8_t intf)
{
    if (!hport) return;
    uint8_t hi = hport->parent ? hport->parent->index : 0;
    uint8_t hp = hport->port;

    esp32_usbh_devinfo_t gone = { 0 };
    bool found = false;

    xSemaphoreTake(S.dev_mtx, portMAX_DELAY);
    for (int i = 0; i < ESP32_USBH_MAX_DEVICES; i++) {
        if (!S.dev[i].valid) continue;
        if (S.dev[i].hub_index == hi && S.dev[i].hub_port == hp &&
            (intf == 0xFF || S.dev[i].interface == intf)) {
            gone  = S.dev[i];
            found = true;
            S.dev[i].valid = false;
            if (intf != 0xFF) break;
        }
    }
    xSemaphoreGive(S.dev_mtx);

    if (found) {
        stats_inc(&S.stats.detach_count);
        esp32_usbh_event_t ev = { .id = ESP32_USBH_EV_DEVICE_DETACHED, .dev = gone };
        priv_event_post(&ev);
    }
}

void priv_devtable_clear(void)
{
    xSemaphoreTake(S.dev_mtx, portMAX_DELAY);
    memset(S.dev, 0, sizeof(S.dev));
    xSemaphoreGive(S.dev_mtx);
}

/* ========================================================================
 *  VBUS
 * ======================================================================== */

static void vbus_apply(bool on)
{
#if ESP32_USBH_VBUS_GPIO >= 0
    int lvl = ESP32_USBH_VBUS_ACTIVE_HIGH ? (on ? 1 : 0) : (on ? 0 : 1);
    gpio_set_level((gpio_num_t)ESP32_USBH_VBUS_GPIO, lvl);
    ESP_LOGI(TAG, "VBUS -> %s", on ? "ON" : "OFF");
#else
    (void)on;
#endif
}

static void vbus_init(void)
{
#if ESP32_USBH_VBUS_GPIO >= 0
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << ESP32_USBH_VBUS_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    vbus_apply(true);
#endif
}

esp_err_t esp32_usbh_set_vbus(bool on)
{
#if ESP32_USBH_VBUS_GPIO >= 0
    vbus_apply(on);
    return ESP_OK;
#else
    (void)on;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

/* ========================================================================
 *  เพดานฮาร์ดแวร์จาก GHWCFG3
 * ======================================================================== */

/*
 * ⚠️ ต้องเรียก "หลัง" usbh_initialize() เท่านั้น
 *
 * บน ESP32-P4 บล็อก USB-OTG HS ถูก gate ทั้งสัญญาณนาฬิกาและ reset อยู่ตอนบูต
 * การอ่าน register ก่อนที่ไดรเวอร์จะปลด gate = bus error ทันที
 * (Load access fault, MTVAL = 0x5000004C ซึ่งก็คือ GHWCFG3 นี่เอง)
 * CherryUSB ปลด gate ให้ใน usb_hc_init() ที่ถูกเรียกจาก usbh_initialize()
 */
static uint32_t read_hw_max_sectors(void)
{
    volatile uint32_t *g3 =
        (volatile uint32_t *)((uintptr_t)PRIV_USB_BASE + PRIV_DWC2_GHWCFG3_OFFSET);
    uint32_t v = *g3;

    /* ถ้าอ่านได้ค่าที่เป็นไปไม่ได้ แปลว่าบล็อกยังไม่ตื่น -> ใช้ค่าปลอดภัยแทน */
    if (v == 0 || v == 0xFFFFFFFFu) {
        ESP_LOGW(TAG, "อ่าน GHWCFG3 ได้ 0x%08lx (ไม่สมเหตุสมผล) ใช้ค่าเริ่มต้น %d sectors",
                 (unsigned long)v, PRIV_HARD_MAX_SEC_PER_CMD);
        return PRIV_HARD_MAX_SEC_PER_CMD;
    }

    uint32_t xfer_w   = (v >> 0) & 0xF;
    uint32_t pkt_w    = (v >> 4) & 0x7;
    uint32_t max_xfer = (1u << (xfer_w + 11)) - 1u;
    uint32_t max_pkt  = (1u << (pkt_w  + 4))  - 1u;
    uint32_t bytes    = (max_pkt * 512u < max_xfer) ? (max_pkt * 512u) : max_xfer;

    ESP_LOGI(TAG, "DWC2 GHWCFG3=0x%08lx  max_xfer=%lu B  max_pkt=%lu  -> %lu sectors/URB",
             (unsigned long)v, (unsigned long)max_xfer,
             (unsigned long)max_pkt, (unsigned long)(bytes / 512u));

    return bytes / 512u;
}

uint32_t esp32_usbh_get_hw_max_sectors(void)
{
    return S.hw_max_sectors ? S.hw_max_sectors : PRIV_HARD_MAX_SEC_PER_CMD;
}

/* ========================================================================
 *  event handler ของ CherryUSB  (รันบนเธรด hub prio 24 — ห้ามบล็อก)
 * ======================================================================== */

static void cherry_event_handler(uint8_t busid, uint8_t hub_index, uint8_t hub_port,
                                 uint8_t intf, uint8_t event)
{
    (void)busid; (void)intf;

    switch (event) {
        case USBH_EVENT_DEVICE_CONNECTED:
            S.enum_pending++;
            if (S.enum_pending >= ESP32_USBH_ENUM_FAIL_LIMIT) {
                S.enum_pending = 0;
                /* CherryUSB ไม่มี event แจ้ง "enumerate fail" (hub.c:652 แค่ log)
                 * จึงนับ CONNECTED ที่ไม่ตามด้วย CONFIGURED แทน */
                esp32_usbh_event_t ev = { .id = ESP32_USBH_EV_ENUM_FAILED };
                ev.dev.hub_index = hub_index;
                ev.dev.hub_port  = hub_port;
                priv_event_post(&ev);
                stats_inc(&S.stats.enum_fail_count);
                priv_request_host_restart("enumerate ล้มเหลวติดกัน");
            }
            break;

        case USBH_EVENT_DEVICE_CONFIGURED:
            S.enum_pending = 0;
            break;

        case USBH_EVENT_DEVICE_DISCONNECTED:
            /* ตารางถูกลบจริงใน *_stop() ของแต่ละคลาส ซึ่งมี hport ให้ใช้ */
            break;

        default:
            break;
    }
}

/* ========================================================================
 *  supervisor — งานที่บล็อกนาน
 * ======================================================================== */

#if ESP32_USBH_PANIC_REBOOT_MS > 0
static void panic_reboot_cb(void *arg)
{
    (void)arg;
    ESP_LOGE(TAG, "usbh_deinitialize() ค้างเกิน %d ms -> esp_restart()",
             ESP32_USBH_PANIC_REBOOT_MS);
    esp_restart();
}
#endif

static void do_host_restart(void)
{
    if (S.restart_busy) return;
    S.restart_busy = true;
    S.state = ESP32_USBH_STATE_RESTARTING;

    ESP_LOGE(TAG, "=== รีสตาร์ท USB host stack (DWC2 soft reset) ===");

#if ESP32_USBH_ENABLE_MSC
    priv_msc_force_unmount_all();
#endif
    priv_devtable_clear();

#if ESP32_USBH_PANIC_REBOOT_MS > 0
    esp_timer_handle_t wd = NULL;
    const esp_timer_create_args_t a = { .callback = panic_reboot_cb, .name = "usbwd" };
    if (esp_timer_create(&a, &wd) == ESP_OK) {
        esp_timer_start_once(wd, (uint64_t)ESP32_USBH_PANIC_REBOOT_MS * 1000);
    }
#endif

    /* usbh_hub_deinitialize() รอ hub_sem แบบ "ไม่มี timeout"
     * ถ้าเธรด hub ค้าง จะค้างตรงนี้ -> ต้องมี panic timer คุม */
    usbh_deinitialize(ESP32_USBH_BUSID);

#if ESP32_USBH_PANIC_REBOOT_MS > 0
    if (wd) { esp_timer_stop(wd); esp_timer_delete(wd); }
#endif

    vTaskDelay(pdMS_TO_TICKS(200));

    int ret = usbh_initialize(ESP32_USBH_BUSID, (uintptr_t)PRIV_USB_BASE,
                              cherry_event_handler);
    ESP_LOGE(TAG, "=== usbh_initialize -> %d ===", ret);

    S.enum_pending    = 0;
    S.last_restart_us = esp_timer_get_time();
    S.restart_busy    = false;
    S.state           = ESP32_USBH_STATE_RUNNING;
    stats_inc(&S.stats.host_restart_count);

    esp32_usbh_event_t ev = { .id = ESP32_USBH_EV_HOST_RESTARTED };
    priv_event_post(&ev);
}

static void sup_task(void *arg)
{
    (void)arg;
    sup_cmd_t c;

    for (;;) {
        if (xQueueReceive(S.sup_q, &c, portMAX_DELAY) != pdTRUE) continue;

        /* กลืนคำสั่งซ้ำที่ค้างในคิวทิ้ง */
        sup_cmd_t d;
        while (xQueueReceive(S.sup_q, &d, 0) == pdTRUE) { }

        if (c == SUP_VBUS_CYCLE) {
#if ESP32_USBH_VBUS_GPIO >= 0
            ESP_LOGE(TAG, "=== วน VBUS ===");
#if ESP32_USBH_ENABLE_MSC
            priv_msc_force_unmount_all();
#endif
            vbus_apply(false);
            vTaskDelay(pdMS_TO_TICKS(1000));
            vbus_apply(true);
            stats_inc(&S.stats.vbus_cycle_count);
            vTaskDelay(pdMS_TO_TICKS(3000));
#else
            do_host_restart();
#endif
        } else if (c == SUP_HOST_RESTART) {
            do_host_restart();
        }
    }
}

void priv_request_host_restart(const char *reason)
{
    int64_t now = esp_timer_get_time();
    if (now - S.last_restart_us < (int64_t)ESP32_USBH_RESTART_COOLDOWN_MS * 1000) {
        ESP_LOGW(TAG, "ขอ restart แต่ติด cooldown (%s)", reason ? reason : "");
        return;
    }
    if (S.restart_busy || !S.sup_q) return;

    ESP_LOGW(TAG, "ขอ restart host: %s", reason ? reason : "");
    sup_cmd_t c = (ESP32_USBH_VBUS_GPIO >= 0) ? SUP_VBUS_CYCLE : SUP_HOST_RESTART;
    xQueueSend(S.sup_q, &c, 0);
}

/* ========================================================================
 *  API
 * ======================================================================== */

esp_err_t esp32_usbh_init(const esp32_usbh_cfg_t *cfg)
{
    if (s_inited) return ESP_ERR_INVALID_STATE;

    memset(&S, 0, sizeof(S));
    S.stats_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    S.state      = ESP32_USBH_STATE_STARTING;

    esp32_usbh_cfg_t c = ESP32_USBH_CFG_DEFAULT();
    if (cfg) c = *cfg;
    S.cb            = c.event_cb;
    S.cb_ctx        = c.event_ctx;
    S.auto_mount    = c.auto_mount;
    S.fetch_strings = c.fetch_strings;

    S.api_mtx = xSemaphoreCreateMutex();
    S.dev_mtx = xSemaphoreCreateMutex();
    S.cb_mtx  = xSemaphoreCreateMutex();
    S.ep0_mtx = xSemaphoreCreateRecursiveMutex();
    S.evt_q   = xQueueCreate(ESP32_USBH_EVENT_QUEUE_LEN, sizeof(esp32_usbh_event_t));
    S.sup_q   = xQueueCreate(4, sizeof(sup_cmd_t));

    if (!S.api_mtx || !S.dev_mtx || !S.cb_mtx || !S.ep0_mtx || !S.evt_q || !S.sup_q) {
        ESP_LOGE(TAG, "สร้าง primitive ไม่สำเร็จ");
        goto fail;
    }

    if (priv_task_create(evt_task, "usbh_evt", ESP32_USBH_EVENT_TASK_STACK, NULL,
                         ESP32_USBH_EVENT_TASK_PRIO, &S.evt_task) != pdPASS) goto fail;
    if (priv_task_create(sup_task, "usbh_sup", 4096, NULL,
                         ESP32_USBH_EVENT_TASK_PRIO + 1, &S.sup_task) != pdPASS) goto fail;

    vbus_init();

    /* เรียก start ของแต่ละโมดูล — นอกจากจะเริ่มงานแล้ว
     * ยังเป็นการ "บังคับ linker ให้ดึง object file" เข้ามา
     * ไม่งั้น weak symbol usbh_msc_run()/usbh_hid_run() จะไม่ถูก override */
#if ESP32_USBH_ENABLE_MSC
    if (priv_msc_start() != ESP_OK) { ESP_LOGE(TAG, "เริ่มโมดูล MSC ไม่สำเร็จ"); goto fail; }
#endif
#if ESP32_USBH_ENABLE_HID
    if (priv_hid_start() != ESP_OK) { ESP_LOGE(TAG, "เริ่มโมดูล HID ไม่สำเร็จ"); goto fail; }
#endif

    int ret = usbh_initialize(ESP32_USBH_BUSID, (uintptr_t)PRIV_USB_BASE,
                              cherry_event_handler);
    if (ret != 0) {
        ESP_LOGE(TAG, "usbh_initialize ล้มเหลว: %d", ret);
        goto fail;
    }

    s_inited = true;
    S.state  = ESP32_USBH_STATE_RUNNING;

    /* อ่าน GHWCFG3 ได้ "หลัง" usbh_initialize() เท่านั้น — ก่อนหน้านี้บล็อก USB
     * ยังถูก clock-gate อยู่ แตะ register เมื่อไหร่ = Load access fault */
    S.hw_max_sectors = read_hw_max_sectors();

    if (ESP32_USBH_MSC_MAX_SEC_PER_CMD > (int)S.hw_max_sectors) {
        ESP_LOGE(TAG, "ESP32_USBH_MSC_MAX_SEC_PER_CMD=%d เกินเพดานฮาร์ดแวร์ %lu "
                      "-> PKTCNT จะล้นเป็น 0 แล้ว transfer ค้าง! ปรับลงให้อัตโนมัติ",
                 ESP32_USBH_MSC_MAX_SEC_PER_CMD, (unsigned long)S.hw_max_sectors);
#if ESP32_USBH_ENABLE_MSC
        esp32_usbh_msc_set_max_sectors_per_cmd((uint32_t)S.hw_max_sectors);
#endif
    }

    ESP_LOGI(TAG, "พร้อมใช้งาน | bus=%d msc=%d hid=%d | sec/cmd=%d (เพดาน %lu) | "
                  "exthubs=%d msc_class=%d hid_class=%d | strings=%d",
             ESP32_USBH_BUSID, ESP32_USBH_ENABLE_MSC, ESP32_USBH_ENABLE_HID,
             ESP32_USBH_MSC_MAX_SEC_PER_CMD, (unsigned long)S.hw_max_sectors,
             CONFIG_USBHOST_MAX_EXTHUBS, CONFIG_USBHOST_MAX_MSC_CLASS,
             CONFIG_USBHOST_MAX_HID_CLASS, (int)S.fetch_strings);

    priv_mem_report("หลัง init");
    return ESP_OK;

fail:
    S.state = ESP32_USBH_STATE_STOPPED;
    if (S.evt_task) { vTaskDelete(S.evt_task); S.evt_task = NULL; }
    if (S.sup_task) { vTaskDelete(S.sup_task); S.sup_task = NULL; }
    if (S.evt_q)   { vQueueDelete(S.evt_q);   S.evt_q = NULL; }
    if (S.sup_q)   { vQueueDelete(S.sup_q);   S.sup_q = NULL; }
    if (S.api_mtx) { vSemaphoreDelete(S.api_mtx); S.api_mtx = NULL; }
    if (S.dev_mtx) { vSemaphoreDelete(S.dev_mtx); S.dev_mtx = NULL; }
    if (S.cb_mtx)  { vSemaphoreDelete(S.cb_mtx);  S.cb_mtx = NULL; }
    if (S.ep0_mtx) { vSemaphoreDelete(S.ep0_mtx); S.ep0_mtx = NULL; }
    return ESP_FAIL;
}

esp_err_t esp32_usbh_deinit(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(S.api_mtx, portMAX_DELAY);
    S.state = ESP32_USBH_STATE_STOPPING;

#if ESP32_USBH_ENABLE_HID
    priv_hid_stop();
#endif
#if ESP32_USBH_ENABLE_MSC
    priv_msc_stop();
#endif

    usbh_deinitialize(ESP32_USBH_BUSID);
    priv_devtable_clear();

    if (S.evt_task) { vTaskDelete(S.evt_task); S.evt_task = NULL; }
    if (S.sup_task) { vTaskDelete(S.sup_task); S.sup_task = NULL; }
    if (S.evt_q)   { vQueueDelete(S.evt_q);   S.evt_q = NULL; }
    if (S.sup_q)   { vQueueDelete(S.sup_q);   S.sup_q = NULL; }

    S.state  = ESP32_USBH_STATE_STOPPED;
    s_inited = false;

    xSemaphoreGive(S.api_mtx);

    vSemaphoreDelete(S.dev_mtx); S.dev_mtx = NULL;
    vSemaphoreDelete(S.cb_mtx);  S.cb_mtx  = NULL;
    vSemaphoreDelete(S.ep0_mtx); S.ep0_mtx = NULL;
    vSemaphoreDelete(S.api_mtx); S.api_mtx = NULL;

    ESP_LOGI(TAG, "ปิดเรียบร้อย");
    return ESP_OK;
}

esp_err_t esp32_usbh_restart(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    sup_cmd_t c = SUP_HOST_RESTART;
    S.last_restart_us = 0;                 /* ผู้ใช้สั่งเอง ข้าม cooldown */
    return xQueueSend(S.sup_q, &c, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t esp32_usbh_set_event_cb(esp32_usbh_event_cb_t cb, void *ctx)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(S.cb_mtx, portMAX_DELAY);
    S.cb     = cb;
    S.cb_ctx = ctx;
    xSemaphoreGive(S.cb_mtx);
    return ESP_OK;
}

esp_err_t esp32_usbh_set_log_level(esp_log_level_t level)
{
    esp_log_level_set(PRIV_TAG_MAIN, level);
    esp_log_level_set(PRIV_TAG_MSC,  level);
    esp_log_level_set(PRIV_TAG_HID,  level);
    return ESP_OK;
}

esp_err_t esp32_usbh_set_fetch_strings(bool enable)
{
    S.fetch_strings = enable;
    return ESP_OK;
}

esp32_usbh_state_t esp32_usbh_get_state(void) { return S.state; }

int esp32_usbh_get_device_count(void)
{
    if (!s_inited) return 0;
    int n = 0;
    xSemaphoreTake(S.dev_mtx, portMAX_DELAY);
    for (int i = 0; i < ESP32_USBH_MAX_DEVICES; i++) if (S.dev[i].valid) n++;
    xSemaphoreGive(S.dev_mtx);
    return n;
}

esp_err_t esp32_usbh_get_device_info(int index, esp32_usbh_devinfo_t *out)
{
    if (!s_inited || !out || index < 0) return ESP_ERR_INVALID_ARG;

    esp_err_t r = ESP_ERR_NOT_FOUND;
    int n = 0;
    xSemaphoreTake(S.dev_mtx, portMAX_DELAY);
    for (int i = 0; i < ESP32_USBH_MAX_DEVICES; i++) {
        if (!S.dev[i].valid) continue;
        if (n == index) { *out = S.dev[i]; r = ESP_OK; break; }
        n++;
    }
    xSemaphoreGive(S.dev_mtx);
    return r;
}

esp_err_t esp32_usbh_get_device_by_vidpid(uint16_t vid, uint16_t pid,
                                          esp32_usbh_devinfo_t *out)
{
    if (!s_inited || !out) return ESP_ERR_INVALID_ARG;

    esp_err_t r = ESP_ERR_NOT_FOUND;
    xSemaphoreTake(S.dev_mtx, portMAX_DELAY);
    for (int i = 0; i < ESP32_USBH_MAX_DEVICES; i++) {
        if (S.dev[i].valid && S.dev[i].vid == vid && S.dev[i].pid == pid) {
            *out = S.dev[i]; r = ESP_OK; break;
        }
    }
    xSemaphoreGive(S.dev_mtx);
    return r;
}

esp_err_t esp32_usbh_get_stats(esp32_usbh_stats_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&S.stats_lock);
    *out = S.stats;
    portEXIT_CRITICAL(&S.stats_lock);
    return ESP_OK;
}

/* ให้โมดูลลูกอ่านค่า runtime */
bool priv_auto_mount_enabled(void) { return S.auto_mount; }
