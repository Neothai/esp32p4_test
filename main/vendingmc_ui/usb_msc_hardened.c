/*
 * usb_msc_hardened.c — USB MSC + FatFS ที่มี error recovery ครบ 4 ชั้น
 * ESP32-P4 + CherryUSB v1.6.1 + ESP-IDF (diskio_ops_t)
 *
 * ชั้นการกู้คืน:
 *   L0  clamp ขนาด transfer   ป้องกันไม่ให้เกิด error ตั้งแต่แรก
 *   L1  BOT Reset Recovery    Bulk-Only Reset + CLEAR_FEATURE(ENDPOINT_HALT) + reset data toggle
 *   L2  VBUS power cycle      บังคับให้อุปกรณ์ reset ตัวเอง (ต้องมี load switch)
 *   L3  Host stack restart    usbh_deinitialize() + usbh_initialize()  = DWC2 core soft reset
 *                             แทนการ esp_restart() ทั้งเครื่อง
 */

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_cache.h"
#include "driver/gpio.h"

#include "ff.h"
#include "diskio.h"

#include "usb_msc_hardened.h"
#include "usb_errno.h"
#include "usb_def.h"

static const char *TAG = "USB_MSC";

/* ============================================================
 *  สถานะ
 * ============================================================ */

#ifndef MAX_DRIVES
#  ifdef CONFIG_USBHOST_MAX_MSC_CLASS
#    define MAX_DRIVES CONFIG_USBHOST_MAX_MSC_CLASS
#  else
#    define MAX_DRIVES 2
#  endif
#endif

typedef struct {
    FATFS            fs;
    BYTE             pdrv;
    struct usbh_msc *msc;
    volatile bool    valid;      /* false = ห้ามแตะ msc อีก (ถอดแล้ว) */
    bool             mounted;
    char             path[8];
} drive_t;

static drive_t            s_drv[MAX_DRIVES];
static SemaphoreHandle_t  s_io_mtx;          /* กัน bounce buffer ชนกัน */
static usb_msc_stats_t    s_st;
static portMUX_TYPE       s_st_lock = portMUX_INITIALIZER_UNLOCKED;

/* bounce buffer: non-cacheable + align 64 */
#define BOUNCE_SECTORS 64                     /* 32 KB */
#define BOUNCE_BYTES   (BOUNCE_SECTORS * 512)
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t s_bounce[BOUNCE_BYTES];

/* ============================================================
 *  Supervisor (L2/L3) — แยก task เพราะ usbh_deinitialize()
 *  จะฆ่า hub thread จึงห้ามเรียกจาก hub thread หรือ worker ที่ค้างอยู่
 * ============================================================ */

typedef enum { SUP_NONE = 0, SUP_VBUS_CYCLE, SUP_HOST_RESTART } sup_cmd_t;

static QueueHandle_t s_sup_q;
static volatile uint32_t s_consec_fail;        /* I/O error ติดกันกี่ครั้ง */
static volatile uint32_t s_enum_fail;          /* connect แล้วไม่ configured กี่ครั้ง */
static volatile bool     s_restart_busy;

static void sup_request(sup_cmd_t c)
{
    if (s_sup_q) xQueueSend(s_sup_q, &c, 0);
}

/* ------------------------------------------------------------
 * Enumeration watchdog  — จุดที่ Test A พิสูจน์ว่าจำเป็น
 * ------------------------------------------------------------
 * CherryUSB v1.6.1 ไม่มี event สำหรับ "enumerate fail"
 * (hub.c:652 แค่ USB_LOG_ERR เฉย ๆ) แต่มี 2 event ที่พอใช้ได้:
 *   USBH_EVENT_DEVICE_CONNECTED   hub.c:584  ยิงตอนเห็นอุปกรณ์เสียบ
 *   USBH_EVENT_DEVICE_CONFIGURED  core:554   ยิงเมื่อ enumerate สำเร็จ
 * ถ้าเจอ CONNECTED ซ้ำ ๆ โดยไม่เคยถึง CONFIGURED = DWC2 ค้าง -> restart
 *
 * ต้องส่ง handler นี้เข้า usbh_initialize() ไม่ใช่ NULL
 * ฟังก์ชันนี้ถูกเรียกจาก hub thread -> ห้ามบล็อก (ข้างในมีแต่ xQueueSend timeout 0)
 */
void usb_msc_event_handler(uint8_t busid, uint8_t hub_index, uint8_t hub_port,
                           uint8_t intf, uint8_t event)
{
    (void)busid; (void)hub_index; (void)hub_port; (void)intf;

    switch (event) {
    case USBH_EVENT_DEVICE_CONNECTED:
        if (++s_enum_fail >= USB_MSC_ENUM_FAIL_BEFORE_RESTART && !s_restart_busy) {
            ESP_LOGE(TAG, "connect x%lu but never configured -> host wedged",
                     (unsigned long)s_enum_fail);
            sup_request(SUP_HOST_RESTART);
        }
        break;

    case USBH_EVENT_DEVICE_CONFIGURED:
        s_enum_fail = 0;              /* enumerate ผ่าน = host ยังดีอยู่ */
        break;

    default:
        break;
    }
}

/* ============================================================
 *  Event queue (hub thread -> worker)
 * ============================================================ */

typedef struct {
    enum { EV_ATTACH, EV_DETACH } type;
    struct usbh_msc *msc;
} msc_ev_t;

static QueueHandle_t s_ev_q;

/* ============================================================
 *  L1: BOT Error Recovery
 * ------------------------------------------------------------
 *  CherryUSB v1.6.1 ไม่มี recovery เลย — ตรวจแล้วใน
 *  class/msc/usbh_msc.c : usbh_bulk_cbw_csw_xfer() แค่
 *      USB_LOG_ERR("msc data transfer error: %d"); return nbytes;
 *  ตามสเปก USB BOT rev1.0 §5.3.4 host ต้องทำ Reset Recovery
 * ============================================================ */

static int ctrl_clear_ep_halt(struct usbh_hubport *hport, uint8_t ep_addr)
{
    struct usb_setup_packet *sp = hport->setup;   /* บัฟเฟอร์ที่ core จองไว้ให้ aligned แล้ว */
    sp->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_STANDARD |
                        USB_REQUEST_RECIPIENT_ENDPOINT;
    sp->bRequest = USB_REQUEST_CLEAR_FEATURE;
    sp->wValue   = USB_FEATURE_ENDPOINT_HALT;
    sp->wIndex   = ep_addr;
    sp->wLength  = 0;
    return usbh_control_transfer(hport, sp, NULL);
}

static int ctrl_bot_reset(struct usbh_msc *msc)
{
    struct usb_setup_packet *sp = msc->hport->setup;
    sp->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_CLASS |
                        USB_REQUEST_RECIPIENT_INTERFACE;
    sp->bRequest = 0xFF;              /* Bulk-Only Mass Storage Reset */
    sp->wValue   = 0;
    sp->wIndex   = msc->intf;
    sp->wLength  = 0;
    return usbh_control_transfer(msc->hport, sp, NULL);
}

/* คืน 0 = กู้สำเร็จ, <0 = กู้ไม่ได้ */
static int msc_bot_recover(struct usbh_msc *msc)
{
    if (!msc || !msc->hport || !msc->hport->connected) return -1;

    ESP_LOGW(TAG, "BOT reset recovery...");

    int r = ctrl_bot_reset(msc);
    if (r < 0) {
        ESP_LOGE(TAG, "  bulk-only reset failed: %d", r);
        return r;                      /* EP0 ก็ไม่ตอบ -> ต้องขึ้น L2/L3 */
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    /* ต้อง clear ทั้งสองทิศทาง แม้ตัวที่ไม่ได้ stall ก็ตาม (สเปกกำหนด) */
    if (msc->bulkin)  ctrl_clear_ep_halt(msc->hport, msc->bulkin->bEndpointAddress);
    if (msc->bulkout) ctrl_clear_ep_halt(msc->hport, msc->bulkout->bEndpointAddress);

    /* CLEAR_FEATURE(ENDPOINT_HALT) บังคับให้ toggle กลับเป็น DATA0 ทั้งสองฝั่ง
     * ฝั่ง host CherryUSB เก็บ toggle ไว้ใน urb -> ต้อง reset เองไม่งั้น desync */
    msc->bulkin_urb.data_toggle  = 0;
    msc->bulkout_urb.data_toggle = 0;

    vTaskDelay(pdMS_TO_TICKS(50));

    portENTER_CRITICAL(&s_st_lock); s_st.bot_resets++; portEXIT_CRITICAL(&s_st_lock);
    ESP_LOGW(TAG, "  recovery done");
    return 0;
}

/* ============================================================
 *  L0 + L1: ตัวห่อ SCSI ที่ clamp ขนาด + retry + recover
 * ============================================================ */

static inline bool msc_alive(struct usbh_msc *msc)
{
    return msc && msc->hport && msc->hport->connected;
}

static int scsi_one(struct usbh_msc *msc, bool wr,
                    uint32_t lba, uint8_t *buf, uint32_t nsec)
{
    for (int a = 0; a <= USB_MSC_IO_RETRY; a++) {
        if (!msc_alive(msc)) return -USB_ERR_NOTCONN;

        int r = wr ? usbh_msc_scsi_write10(msc, lba, buf, nsec)
                   : usbh_msc_scsi_read10 (msc, lba, buf, nsec);
        if (r == 0) {
            s_consec_fail = 0;
            return 0;
        }

        portENTER_CRITICAL(&s_st_lock); s_st.io_errors++; portEXIT_CRITICAL(&s_st_lock);
        ESP_LOGW(TAG, "%s LBA=%lu n=%lu failed err=%d (try %d/%d)",
                 wr ? "WRITE10" : "READ10", (unsigned long)lba,
                 (unsigned long)nsec, r, a + 1, USB_MSC_IO_RETRY + 1);

        /* อุปกรณ์หายจริง — ไม่ต้อง retry */
        if (r == -USB_ERR_NODEV || r == -USB_ERR_NOTCONN || r == -USB_ERR_SHUTDOWN)
            return r;

        if (a == USB_MSC_IO_RETRY) break;

        if (msc_bot_recover(msc) < 0) break;   /* กู้ไม่ได้ -> ขึ้นชั้นถัดไป */
    }

    /* ถึงตรงนี้ = กู้ด้วย L1 ไม่สำเร็จ */
    if (++s_consec_fail >= USB_MSC_FAIL_BEFORE_HOST_RESTART) {
        ESP_LOGE(TAG, "consecutive failures=%lu -> escalate",
                 (unsigned long)s_consec_fail);
        sup_request((USB_MSC_VBUS_EN_GPIO >= 0) ? SUP_VBUS_CYCLE : SUP_HOST_RESTART);
    }
    return -1;
}

/* ซอยเป็นก้อนละไม่เกิน USB_MSC_MAX_SEC_PER_CMD */
static int scsi_chunked(struct usbh_msc *msc, bool wr,
                        uint32_t lba, uint8_t *buf, uint32_t nsec, uint32_t bs)
{
    while (nsec) {
        uint32_t n = (nsec > USB_MSC_MAX_SEC_PER_CMD) ? USB_MSC_MAX_SEC_PER_CMD : nsec;
        int r = scsi_one(msc, wr, lba, buf, n);
        if (r != 0) return r;
        lba  += n;
        buf  += n * bs;
        nsec -= n;
    }
    return 0;
}

/* ============================================================
 *  diskio callbacks
 * ============================================================ */

static inline void hist_add(uint32_t *h, UINT count)
{
    int b = 0;
    while ((1u << b) < count && b < 8) b++;
    h[b]++;
}

static DSTATUS usb_init_cb(void *ctx) { (void)ctx; return 0; }

static DSTATUS usb_status_cb(void *ctx)
{
    struct usbh_msc *msc = (struct usbh_msc *)ctx;
    if (!msc_alive(msc)) return STA_NOINIT | STA_NODISK;   /* ← ของเดิมคืน 0 เสมอ */
    return 0;
}

static DRESULT rw_impl(void *ctx, BYTE *buff, LBA_t sector, UINT count, bool wr)
{
    struct usbh_msc *msc = (struct usbh_msc *)ctx;
    if (!msc_alive(msc)) return RES_NOTRDY;        /* ← ตัด fallback s_drv[0] ทิ้งแล้ว */

    uint32_t bs = msc->blocksize ? msc->blocksize : 512;

    /* ทางเร็ว: บัฟเฟอร์ align 64 + อยู่ใน RAM ที่ DMA ถึง */
    if (((uintptr_t)buff % 64 == 0) && esp_ptr_dma_capable(buff)) {
#if USB_MSC_DO_CACHE_SYNC
        /* ห้ามใส่ ESP_CACHE_MSYNC_FLAG_UNALIGNED กับทิศทาง M2C — IDF ปฏิเสธ
         * ("M2C direction doesn't allow ESP_CACHE_MSYNC_FLAG_UNALIGNED")
         * เพราะการ invalidate ช่วงที่ไม่ตรงขอบ cache line จะทิ้งข้อมูล dirty
         * ของ line ข้างเคียงไปด้วย
         *
         * ตรงนี้ไม่ต้องใช้ flag นั้นอยู่แล้ว เพราะเงื่อนไข if ด้านบนรับประกันว่า
         *   addr % 64 == 0  และ  size = count * 512 ซึ่งเป็นพหุคูณของ 64 */
        size_t sync_len = (size_t)count * bs;
        if (wr) {
            esp_cache_msync(buff, sync_len, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        }
#endif
        int r = scsi_chunked(msc, wr, (uint32_t)sector, buff, count, bs);
#if USB_MSC_DO_CACHE_SYNC
        if (!wr && r == 0) {
            esp_cache_msync(buff, sync_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        }
#endif
        return (r == 0) ? RES_OK : RES_ERROR;
    }

    /* ทางช้า: ผ่าน bounce buffer — ต้องล็อก ไม่งั้น 2 ไดรฟ์ชนกัน */
    DRESULT res = RES_OK;
    uint32_t maxs = BOUNCE_BYTES / bs;
    xSemaphoreTake(s_io_mtx, portMAX_DELAY);
    portENTER_CRITICAL(&s_st_lock);
    if (wr) s_st.wr_bounce++; else s_st.rd_bounce++;
    portEXIT_CRITICAL(&s_st_lock);

    UINT left = count; LBA_t sec = sector; BYTE *p = buff;
    while (left) {
        UINT n = (left > maxs) ? maxs : left;
        if (wr) memcpy(s_bounce, p, (size_t)n * bs);
        if (scsi_chunked(msc, wr, (uint32_t)sec, s_bounce, n, bs) != 0) {
            res = RES_ERROR; break;
        }
        if (!wr) memcpy(p, s_bounce, (size_t)n * bs);
        sec += n; p += (size_t)n * bs; left -= n;
    }
    xSemaphoreGive(s_io_mtx);
    return res;
}

static DRESULT usb_read_cb(void *ctx, BYTE *buff, LBA_t sector, UINT count)
{
    int64_t t0 = esp_timer_get_time();
    DRESULT r = rw_impl(ctx, buff, sector, count, false);
    portENTER_CRITICAL(&s_st_lock);
    s_st.rd_calls++; s_st.rd_sectors += count;
    s_st.rd_us += (uint64_t)(esp_timer_get_time() - t0);
    hist_add(s_st.hist_rd, count);
    portEXIT_CRITICAL(&s_st_lock);
    return r;
}

static DRESULT usb_write_cb(void *ctx, const BYTE *buff, LBA_t sector, UINT count)
{
    int64_t t0 = esp_timer_get_time();
    DRESULT r = rw_impl(ctx, (BYTE *)buff, sector, count, true);
    portENTER_CRITICAL(&s_st_lock);
    s_st.wr_calls++; s_st.wr_sectors += count;
    s_st.wr_us += (uint64_t)(esp_timer_get_time() - t0);
    hist_add(s_st.hist_wr, count);
    portEXIT_CRITICAL(&s_st_lock);
    return r;
}

/* SCSI SYNCHRONIZE CACHE (10) — ของเดิม CTRL_SYNC คืน RES_OK เฉย ๆ = โกหก FatFS */
static int scsi_sync_cache(struct usbh_msc *msc)
{
    /* CherryUSB ไม่ export API นี้ ใช้ READ10 ขนาด 1 sector เป็น barrier แทน
     * (อุปกรณ์ส่วนใหญ่จะ flush write cache ก่อนตอบ read)
     * ถ้าต้องการของจริง ให้ patch usbh_msc.c เพิ่ม usbh_msc_scsi_sync_cache10() */
    static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t tmp[512];
    return usbh_msc_scsi_read10(msc, 0, tmp, 1);
}

static DRESULT usb_ioctl_cb(void *ctx, BYTE cmd, void *buff)
{
    struct usbh_msc *msc = (struct usbh_msc *)ctx;
    if (!msc_alive(msc)) return RES_NOTRDY;

    switch (cmd) {
    case CTRL_SYNC:
        return (scsi_sync_cache(msc) == 0) ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = (LBA_t)msc->blocknum;  return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = (WORD)(msc->blocksize ? msc->blocksize : 512); return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 128;                   /* 64 KB erase block (ของเดิมใส่ 1) */
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

static const diskio_ops_t s_ops = {
    .init   = usb_init_cb,
    .status = usb_status_cb,
    .read   = usb_read_cb,
    .write  = usb_write_cb,
    .ioctl  = usb_ioctl_cb,
};

/* ============================================================
 *  สถิติ
 * ============================================================ */

void usb_msc_stats_reset(void)
{
    portENTER_CRITICAL(&s_st_lock); memset(&s_st, 0, sizeof(s_st)); portEXIT_CRITICAL(&s_st_lock);
}

void usb_msc_stats_get(usb_msc_stats_t *o)
{
    portENTER_CRITICAL(&s_st_lock); *o = s_st; portEXIT_CRITICAL(&s_st_lock);
}

void usb_msc_stats_dump(const char *tag)
{
    usb_msc_stats_t s; usb_msc_stats_get(&s);
    const char *t = tag ? tag : TAG;
    ESP_LOGI(t, "WR calls=%lu sec=%lu avg=%.1f bounce=%lu time=%.3fs",
             (unsigned long)s.wr_calls, (unsigned long)s.wr_sectors,
             s.wr_calls ? (double)s.wr_sectors / s.wr_calls : 0.0,
             (unsigned long)s.wr_bounce, s.wr_us / 1e6);
    ESP_LOGI(t, "   hist 1:%lu 2:%lu 4:%lu 8:%lu 16:%lu 32:%lu 64:%lu 128:%lu 256+:%lu",
             (unsigned long)s.hist_wr[0], (unsigned long)s.hist_wr[1],
             (unsigned long)s.hist_wr[2], (unsigned long)s.hist_wr[3],
             (unsigned long)s.hist_wr[4], (unsigned long)s.hist_wr[5],
             (unsigned long)s.hist_wr[6], (unsigned long)s.hist_wr[7],
             (unsigned long)s.hist_wr[8]);
    ESP_LOGI(t, "RD calls=%lu sec=%lu avg=%.1f bounce=%lu time=%.3fs",
             (unsigned long)s.rd_calls, (unsigned long)s.rd_sectors,
             s.rd_calls ? (double)s.rd_sectors / s.rd_calls : 0.0,
             (unsigned long)s.rd_bounce, s.rd_us / 1e6);
    ESP_LOGI(t, "HEALTH io_err=%lu bot_reset=%lu vbus_cycle=%lu host_restart=%lu",
             (unsigned long)s.io_errors, (unsigned long)s.bot_resets,
             (unsigned long)s.vbus_cycles, (unsigned long)s.host_restarts);
}

/* ============================================================
 *  Benchmark (ต้องเรียกจาก worker task เท่านั้น)
 * ============================================================ */

void usb_storage_benchmark(const char *drive_path, uint32_t total_mb, uint32_t chunk_kb)
{
    char path[32];
    snprintf(path, sizeof(path), "%s/bench_test.tmp", drive_path);

    size_t   chunk = chunk_kb * 1024;
    size_t   total = (size_t)total_mb * 1024 * 1024;
    uint32_t nch   = total / chunk;
    FIL      f;
    FRESULT  fr;
    UINT     n;
    bool     opened = false;

    usb_msc_stats_reset();

    /* ต้องเป็น MALLOC_CAP_CACHE_ALIGNED บน P4 ไม่งั้น DMA เห็นข้อมูลค้างใน cache */
    uint8_t *buf = heap_caps_aligned_alloc(64, chunk,
                        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_CACHE_ALIGNED);
    if (!buf) { ESP_LOGE(TAG, "alloc %lu KB failed", (unsigned long)chunk_kb); return; }
    for (size_t i = 0; i < chunk; i++) buf[i] = (uint8_t)i;

    ESP_LOGI(TAG, "bench %s  %luMB x %luKB  (max %d sec/cmd)",
             path, (unsigned long)total_mb, (unsigned long)chunk_kb,
             USB_MSC_MAX_SEC_PER_CMD);

    /* ---- WRITE ---- */
    fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) { ESP_LOGE(TAG, "f_open(w)=%d", fr); goto out; }
    opened = true;

    /* จองพื้นที่ต่อเนื่องล่วงหน้า ลด FAT update ระหว่างเขียน */
    if (f_expand(&f, total, 1) != FR_OK)
        ESP_LOGW(TAG, "f_expand ไม่สำเร็จ (พื้นที่ไม่ต่อเนื่อง) — ใช้วิธีปกติ");

    int64_t t0 = esp_timer_get_time();
    for (uint32_t i = 0; i < nch; i++) {
        fr = f_write(&f, buf, chunk, &n);
        if (fr != FR_OK || n != chunk) {
            ESP_LOGE(TAG, "f_write chunk %lu err=%d n=%u", (unsigned long)i, fr, n);
            goto out;
        }
    }
    f_close(&f); opened = false;
    double wt = (esp_timer_get_time() - t0) / 1e6;
    ESP_LOGI(TAG, ">> WRITE %.2f MB/s (%.3f s)", total_mb / wt, wt);

    /* ---- READ ---- */
    fr = f_open(&f, path, FA_READ);
    if (fr != FR_OK) { ESP_LOGE(TAG, "f_open(r)=%d", fr); goto out; }
    opened = true;

    uint32_t bad = 0;
    t0 = esp_timer_get_time();
    for (uint32_t i = 0; i < nch; i++) {
        fr = f_read(&f, buf, chunk, &n);
        if (fr != FR_OK || n != chunk) {
            ESP_LOGE(TAG, "f_read chunk %lu err=%d n=%u", (unsigned long)i, fr, n);
            goto out;
        }
#if USB_MSC_BENCH_VERIFY
        /* ตรวจหัว/กลาง/ท้ายของทุก chunk — จับบั๊ก cache coherency ได้ทันที
         * ถ้าเจอ mismatch แปลว่า CPU อ่านจาก cache เก่า ไม่เห็นข้อมูลที่ DMA เขียนมา */
        if (buf[0] != 0 || buf[63] != 63 ||
            buf[chunk / 2] != (uint8_t)(chunk / 2) ||
            buf[chunk - 1] != (uint8_t)(chunk - 1)) {
            if (bad++ < 3)
                ESP_LOGE(TAG, "DATA MISMATCH chunk %lu: [0]=%02x(exp 00) [%u]=%02x(exp %02x)",
                         (unsigned long)i, buf[0],
                         (unsigned)(chunk - 1), buf[chunk - 1], (uint8_t)(chunk - 1));
        }
#endif
    }
    f_close(&f); opened = false;
    double rt = (esp_timer_get_time() - t0) / 1e6;
    ESP_LOGI(TAG, ">> READ  %.2f MB/s (%.3f s)", total_mb / rt, rt);
#if USB_MSC_BENCH_VERIFY
    if (bad) ESP_LOGE(TAG, ">> VERIFY FAILED on %lu/%lu chunks", (unsigned long)bad,
                      (unsigned long)nch);
    else     ESP_LOGI(TAG, ">> VERIFY OK");
#endif

out:
    if (opened) f_close(&f);            /* ← ของเดิมหลุด cleanup ในหลาย error path */
    f_unlink(path);
    heap_caps_free(buf);
    usb_msc_stats_dump("USB_BENCH");
}

/* ============================================================
 *  attach / detach
 * ============================================================ */

bool usb_msc_is_mounted(void)
{
    for (int i = 0; i < MAX_DRIVES; i++) if (s_drv[i].mounted) return true;
    return false;
}

static void do_attach(struct usbh_msc *msc)
{
    if (usbh_msc_scsi_init(msc) != 0) {
        ESP_LOGE(TAG, "scsi_init failed");
        return;
    }
    ESP_LOGI(TAG, "SCSI ready: %lu blocks x %u B",
             (unsigned long)msc->blocknum, msc->blocksize);

    int k = -1;
    for (int i = 0; i < MAX_DRIVES; i++) if (!s_drv[i].valid) { k = i; break; }
    if (k < 0) { ESP_LOGE(TAG, "no free slot"); return; }

    BYTE pdrv = 0;
    if (diskio_register_driver(&s_ops, msc, &pdrv) != RES_OK) {
        ESP_LOGE(TAG, "diskio_register failed");
        return;
    }

    s_drv[k].msc   = msc;
    s_drv[k].pdrv  = pdrv;
    s_drv[k].valid = true;
    snprintf(s_drv[k].path, sizeof(s_drv[k].path), "%u:", pdrv);

    FRESULT fr = f_mount(&s_drv[k].fs, s_drv[k].path, 1);
    if (fr != FR_OK) {
        ESP_LOGE(TAG, "mount %s failed: %d", s_drv[k].path, fr);
        f_mount(NULL, s_drv[k].path, 0);
        diskio_unregister_driver(pdrv);
        s_drv[k].valid = false; s_drv[k].msc = NULL;
        return;
    }
    s_drv[k].mounted = true;
    s_consec_fail = 0;
    ESP_LOGI(TAG, "mounted on %s", s_drv[k].path);

    usb_storage_benchmark(s_drv[k].path, 128, 64);
}

static void do_detach(struct usbh_msc *msc)
{
    for (int i = 0; i < MAX_DRIVES; i++) {
        if (s_drv[i].msc != msc) continue;

        /* สำคัญ: ตั้ง valid=false ก่อน แล้วค่อย unmount
         * เพื่อให้ callback คืน RES_NOTRDY ทันที ไม่ไปแตะ msc ที่ถูก free แล้ว */
        s_drv[i].valid = false;
        f_mount(NULL, s_drv[i].path, 0);
        diskio_unregister_driver(s_drv[i].pdrv);
        s_drv[i].mounted = false;
        s_drv[i].msc = NULL;
        ESP_LOGI(TAG, "unmounted %s", s_drv[i].path);
        return;
    }
}

static void force_unmount_all(void)
{
    for (int i = 0; i < MAX_DRIVES; i++) {
        if (!s_drv[i].msc) continue;
        s_drv[i].valid = false;
        f_mount(NULL, s_drv[i].path, 0);
        diskio_unregister_driver(s_drv[i].pdrv);
        s_drv[i].mounted = false;
        s_drv[i].msc = NULL;
    }
}

/* ============================================================
 *  Worker task  (งานหนักทั้งหมดอยู่ที่นี่ ไม่ใช่ hub thread)
 * ============================================================ */

static void msc_worker(void *arg)
{
    (void)arg;
    msc_ev_t ev;
    for (;;) {
        if (xQueueReceive(s_ev_q, &ev, portMAX_DELAY) != pdTRUE) continue;
        if (ev.type == EV_ATTACH) do_attach(ev.msc);
        else                      do_detach(ev.msc);
    }
}

/* ============================================================
 *  Supervisor task (L2/L3)
 * ============================================================ */

static void vbus_set(bool on)
{
#if (USB_MSC_VBUS_EN_GPIO >= 0)
    gpio_set_level((gpio_num_t)USB_MSC_VBUS_EN_GPIO, on ? 1 : 0);
#else
    (void)on;
#endif
}

#if (USB_MSC_PANIC_REBOOT_MS > 0)
#include "esp_system.h"
static void panic_cb(void *arg)
{
    (void)arg;
    ESP_LOGE(TAG, "host restart hung -> esp_restart()");
    esp_restart();
}
#endif

static void usb_host_restart(void)
{
    ESP_LOGE(TAG, "=== restarting USB host stack (DWC2 soft reset) ===");
    s_restart_busy = true;
    force_unmount_all();

#if (USB_MSC_PANIC_REBOOT_MS > 0)
    /* กันกรณี usbh_hub_deinitialize() ค้างรอ hub_sem ตลอดกาล
     * (เกิดได้ถ้า hub thread ติดใน dwc2_halt busy-wait ภายใน critical section) */
    esp_timer_handle_t pt = NULL;
    const esp_timer_create_args_t pa = { .callback = panic_cb, .name = "usbpanic" };
    if (esp_timer_create(&pa, &pt) == ESP_OK)
        esp_timer_start_once(pt, (uint64_t)USB_MSC_PANIC_REBOOT_MS * 1000);
#endif

    usbh_deinitialize(USB_MSC_BUSID);
    vTaskDelay(pdMS_TO_TICKS(300));

    /* ต้องส่ง event handler เข้าไปใหม่ทุกครั้ง ไม่งั้น watchdog ตาย */
    int r = usbh_initialize(USB_MSC_BUSID, (uintptr_t)ESP_USB_HS0_BASE,
                            usb_msc_event_handler);
    ESP_LOGE(TAG, "=== usbh_initialize -> %d ===", r);

#if (USB_MSC_PANIC_REBOOT_MS > 0)
    if (pt) { esp_timer_stop(pt); esp_timer_delete(pt); }
#endif

    portENTER_CRITICAL(&s_st_lock); s_st.host_restarts++; portEXIT_CRITICAL(&s_st_lock);
    s_consec_fail = 0;
    s_enum_fail   = 0;
    s_restart_busy = false;
}

static void sup_task(void *arg)
{
    (void)arg;
    sup_cmd_t c;
    for (;;) {
        if (xQueueReceive(s_sup_q, &c, portMAX_DELAY) != pdTRUE) continue;

        /* กลืน request ที่ค้างอยู่ทิ้ง กันทำซ้ำ */
        sup_cmd_t d; while (xQueueReceive(s_sup_q, &d, 0) == pdTRUE) {}

        if (c == SUP_VBUS_CYCLE) {
            ESP_LOGE(TAG, "=== VBUS power cycle ===");
            force_unmount_all();
            vbus_set(false);
            vTaskDelay(pdMS_TO_TICKS(1000));   /* ต้องนานพอให้ cap คายประจุ */
            vbus_set(true);
            portENTER_CRITICAL(&s_st_lock); s_st.vbus_cycles++; portEXIT_CRITICAL(&s_st_lock);
            s_consec_fail = 0;

            /* ให้โอกาส re-enumerate 5 วินาที ถ้ายังไม่ขึ้นค่อย restart host */
            vTaskDelay(pdMS_TO_TICKS(5000));
            if (!usb_msc_is_mounted()) usb_host_restart();
        } else if (c == SUP_HOST_RESTART) {
            usb_host_restart();
        }
    }
}

/* ============================================================
 *  hook ของ CherryUSB — ต้องคืนทันที ห้ามบล็อก!
 * ============================================================ */

void usbh_msc_run(struct usbh_msc *msc_class)
{
    msc_ev_t ev = { .type = EV_ATTACH, .msc = msc_class };
    if (s_ev_q) xQueueSend(s_ev_q, &ev, 0);
}

void usbh_msc_stop(struct usbh_msc *msc_class)
{
    /* ตัดการเข้าถึงทันทีใน hub thread เพื่อกัน use-after-free
     * ส่วนงาน unmount ที่ช้าค่อยไปทำใน worker */
    for (int i = 0; i < MAX_DRIVES; i++)
        if (s_drv[i].msc == msc_class) s_drv[i].valid = false;

    msc_ev_t ev = { .type = EV_DETACH, .msc = msc_class };
    if (s_ev_q) xQueueSend(s_ev_q, &ev, 0);
}

/* ============================================================
 *  init
 * ============================================================ */

int usb_msc_hardened_init(void)
{
    memset(s_drv, 0, sizeof(s_drv));
    usb_msc_stats_reset();

    s_io_mtx = xSemaphoreCreateMutex();
    s_ev_q   = xQueueCreate(8, sizeof(msc_ev_t));
    s_sup_q  = xQueueCreate(4, sizeof(sup_cmd_t));
    if (!s_io_mtx || !s_ev_q || !s_sup_q) return -1;

#if (USB_MSC_VBUS_EN_GPIO >= 0)
    gpio_config_t io = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << USB_MSC_VBUS_EN_GPIO,
    };
    gpio_config(&io);
    vbus_set(true);
#endif

    /* stack 8 KB: FatFS ~2KB + vfprintf(%f) ~2KB + กันเหนียว
     * priority 6: สูงกว่า idle/wifi worker แต่ต่ำกว่า LVGL(12) */
    if (xTaskCreate(msc_worker, "msc_wrk", 8 * 1024, NULL, 6, NULL) != pdPASS) return -2;
    if (xTaskCreate(sup_task,   "usb_sup", 4 * 1024, NULL, 7, NULL) != pdPASS) return -3;

    ESP_LOGI(TAG, "hardened MSC ready (max %d sec/cmd, retry %d, vbus_gpio %d, "
                  "enum_wd %d, cache_sync %d)",
             USB_MSC_MAX_SEC_PER_CMD, USB_MSC_IO_RETRY, USB_MSC_VBUS_EN_GPIO,
             USB_MSC_ENUM_FAIL_BEFORE_RESTART, USB_MSC_DO_CACHE_SYNC);
    return 0;
}
