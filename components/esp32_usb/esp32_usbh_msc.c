/*
 * esp32_usbh_msc.c — USB Mass Storage + FatFS
 *
 * จุดสำคัญด้าน thread-safety
 *   - แต่ละไดรฟ์มี mutex ของตัวเอง (d->mtx) -> คนละไดรฟ์ทำงานขนานกันได้จริง
 *   - bounce buffer แยกต่อไดรฟ์ ไม่แชร์กัน จึงไม่ต้องมี global lock
 *   - ตารางไดรฟ์ป้องกันด้วย s_tbl_mtx (ถือสั้น ๆ เฉพาะตอนค้นหา/จอง slot)
 *   - ธง d->valid เป็น volatile: ตั้ง false ทันทีที่ถอด ทุกเส้นทาง I/O เช็กก่อนใช้
 *
 * จุดสำคัญด้าน hot plug
 *   - usbh_msc_run()/stop() ถูกเรียกจากเธรด hub (prio 24) -> แค่โยนงานเข้าคิว
 *   - usbh_msc_stop() ต้อง "รอ" จนไม่มี I/O ค้างก่อนคืน เพราะ CherryUSB จะ
 *     usbh_msc_class_free() ทันทีหลังจากนั้น (use-after-free ถ้าไม่รอ)
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

#include "usbh_core.h"
#include "usbh_msc.h"
#include "ff.h"
#include "diskio.h"

#include "esp32_usbh_msc.h"
#include "esp32_usb_private.h"

#if ESP32_USBH_ENABLE_MSC

static const char *TAG = PRIV_TAG_MSC;

#define NDRV          ESP32_USBH_MSC_MAX_DRIVES
#define BOUNCE_BYTES  (ESP32_USBH_MSC_BOUNCE_SECTORS * 512)


/* ========================================================================
 *  โครงสร้างไดรฟ์
 * ======================================================================== */

typedef struct {
    volatile bool     valid;      /* false = ถอดแล้ว ห้ามแตะ msc อีก */
    volatile bool     mounted;
    volatile uint32_t io_busy;    /* จำนวน I/O ที่กำลังทำงานอยู่ */

    struct usbh_msc  *msc;
    uint8_t           pdrv;
    char              path[4];
    char              devname[CONFIG_USBHOST_DEV_NAMELEN];

    /* ⚠️ จองแบบ lazy ทั้งคู่ — เมื่อก่อนจองไว้ล่วงหน้าทุกช่องตั้งแต่ init
     * ทำให้กินแรมภายใน (NDRV x BOUNCE_BYTES) + (NDRV x sizeof(FATFS))
     * ซึ่งที่ NDRV=4, BOUNCE=32 KB, FF_MAX_SS=4096 คือ 128 KB + ~19 KB
     * หายไปตั้งแต่บูต ทั้งที่ยังไม่มีไดรฟ์เสียบสักตัว */
    FATFS            *fs;         /* จองตอน mount   คืนตอน unmount (PSRAM ได้) */
    uint8_t          *bounce;     /* จองตอนเสียบ    คืนตอนถอด (ต้อง DMA internal) */
    size_t            bounce_sz;  /* ขนาดที่จองได้จริง อาจน้อยกว่า BOUNCE_BYTES */

    SemaphoreHandle_t mtx;        /* ล็อกต่อไดรฟ์ */

    uint16_t          vid, pid;
    char              product[ESP32_USBH_STR_LEN];

    esp32_usbh_msc_stats_t st;
    portMUX_TYPE           st_lock;

    uint32_t          consec_fail;
    /* ---- แคชพื้นที่ว่าง ----
     * f_getfree() สแกน FAT "ทั้งตาราง": FAT32 16 GB / cluster 4 KB
     * = 4M entry x 4 B = อ่าน 16 MB จาก USB ต่อการเรียก 1 ครั้ง
     * ถ้าปล่อยให้ get_info() เรียกทุกครั้ง UI จะกระตุกทันที
     * -> คำนวณครั้งเดียวตอน mount แล้วเก็บไว้ ใครอยากได้ค่าสด
     *    ต้องเรียก esp32_usbh_msc_refresh_usage() เอง */
    uint64_t          free_bytes, total_bytes;
    uint32_t          cluster_bytes;
    int64_t           usage_at_us;        /* เวลาที่คำนวณล่าสุด (0 = ยังไม่เคย) */
} drive_t;

static drive_t          s_d[NDRV];
static SemaphoreHandle_t s_tbl_mtx;
static QueueHandle_t     s_work_q;
static TaskHandle_t      s_work_task;
static volatile bool     s_running;
static volatile uint32_t s_max_sec = ESP32_USBH_MSC_MAX_SEC_PER_CMD;
static volatile bool     s_auto_mount = true;

typedef struct {
    enum { W_ATTACH, W_MOUNT, W_UNMOUNT } op;
    int slot;
} work_t;

/* ========================================================================
 *  ยูทิลิตี้
 * ======================================================================== */

static inline bool drv_ok(drive_t *d)
{
    return d && d->valid && d->msc && d->msc->hport && d->msc->hport->connected;
}

static void st_add_rd(drive_t *d, uint32_t sec, uint64_t us, bool bounce)
{
    portENTER_CRITICAL(&d->st_lock);
    d->st.rd_calls++; d->st.rd_sectors += sec; d->st.rd_us += us;
    if (bounce) d->st.rd_bounce++;
    portEXIT_CRITICAL(&d->st_lock);
}

static void st_add_wr(drive_t *d, uint32_t sec, uint64_t us, bool bounce)
{
    portENTER_CRITICAL(&d->st_lock);
    d->st.wr_calls++; d->st.wr_sectors += sec; d->st.wr_us += us;
    if (bounce) d->st.wr_bounce++;
    portEXIT_CRITICAL(&d->st_lock);
}

static void st_bump(drive_t *d, uint32_t *f)
{
    portENTER_CRITICAL(&d->st_lock);
    (*f)++;
    portEXIT_CRITICAL(&d->st_lock);
}

static void post_msc_event(drive_t *d, esp32_usbh_event_id_t id, int err)
{
    esp32_usbh_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.id = id;

    ev.dev.valid     = true;
    ev.dev.dev_class = ESP32_USBH_CLASS_MSC;
    ev.dev.vid       = d->vid;
    ev.dev.pid       = d->pid;
    snprintf(ev.dev.product, sizeof(ev.dev.product), "%s", d->product);
    snprintf(ev.dev.devname, sizeof(ev.dev.devname), "%s", d->devname);

    ev.msc.index      = (uint8_t)(d - s_d);
    ev.msc.pdrv       = d->pdrv;
    ev.msc.error_code = err;
    snprintf(ev.msc.path, sizeof(ev.msc.path), "%s", d->path);
    if (d->msc) {
        ev.msc.block_count    = d->msc->blocknum;
        ev.msc.block_size     = d->msc->blocksize ? d->msc->blocksize : PRIV_DEFAULT_BLOCK_SIZE;
        ev.msc.capacity_bytes = (uint64_t)ev.msc.block_count * ev.msc.block_size;
    }
    priv_event_post(&ev);
}

/* ========================================================================
 *  การกู้คืนระดับ BOT (Bulk-Only Transport)
 * ======================================================================== */

static int bot_clear_halt(struct usbh_msc *msc, uint8_t ep)
{
    struct usbh_hubport *hport = msc->hport;
    struct usb_setup_packet *setup = hport->setup;

    setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_STANDARD |
                           USB_REQUEST_RECIPIENT_ENDPOINT;
    setup->bRequest = USB_REQUEST_CLEAR_FEATURE;
    setup->wValue   = USB_FEATURE_ENDPOINT_HALT;
    setup->wIndex   = ep;
    setup->wLength  = 0;

    int r = usbh_control_transfer(hport, setup, NULL);
    if (r >= 0) {
        /* ต้องเคลียร์ data toggle เอง — CherryUSB ไม่ทำให้ */
        if (ep & 0x80) msc->bulkin_urb.data_toggle  = 0;
        else           msc->bulkout_urb.data_toggle = 0;
    }
    return r;
}

/** คืน 0 ถ้ากู้สำเร็จ, <0 ถ้า EP0 ตายด้วย (แปลว่าปัญหาอยู่ที่ลิงก์ ไม่ใช่ไดรฟ์) */
static int bot_recover(drive_t *d)
{
    struct usbh_msc *msc = d->msc;
    if (!drv_ok(d)) return -1;

    st_bump(d, &d->st.bot_resets);
    ESP_LOGW(TAG, "%s: BOT reset recovery", d->path);

    if (!priv_ep0_lock(PRIV_EP0_MUTEX_WAIT_MS)) return -1;

    struct usbh_hubport *hport = msc->hport;
    struct usb_setup_packet *setup = hport->setup;

    setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_CLASS |
                           USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = PRIV_BOT_RESET_REQUEST;
    setup->wValue   = 0;
    setup->wIndex   = msc->intf;
    setup->wLength  = 0;

    int r = usbh_control_transfer(hport, setup, NULL);
    if (r < 0) {
        ESP_LOGE(TAG, "%s: bulk-only reset ล้มเหลว: %d (EP0 ตายด้วย)", d->path, r);
        priv_ep0_unlock();
        return -1;
    }

    int a = bot_clear_halt(msc, msc->bulkin->bEndpointAddress);
    int b = bot_clear_halt(msc, msc->bulkout->bEndpointAddress);

    priv_ep0_unlock();
    return (a < 0 || b < 0) ? -1 : 0;
}

/* ========================================================================
 *  คำสั่ง SCSI หนึ่งคำสั่ง + ลองซ้ำ
 * ======================================================================== */

static int scsi_one(drive_t *d, bool wr, uint32_t lba, uint8_t *buf, uint32_t nsec)
{
    bool ep0_dead = false;

    for (int a = 0; a <= ESP32_USBH_MSC_IO_RETRY; a++) {
        if (!drv_ok(d)) return -1;

        int r = wr ? usbh_msc_scsi_write10(d->msc, lba, buf, nsec)
                   : usbh_msc_scsi_read10(d->msc, lba, buf, nsec);
        if (r == 0) {
            if (a > 0) d->consec_fail = 0;
            return 0;
        }

        st_bump(d, &d->st.retries);
        ESP_LOGW(TAG, "%s: %s LBA=%lu n=%lu ล้มเหลว err=%d (ครั้งที่ %d/%d)",
                 d->path, wr ? "WRITE10" : "READ10",
                 (unsigned long)lba, (unsigned long)nsec, r,
                 a + 1, ESP32_USBH_MSC_IO_RETRY + 1);

        /* ถอดสายระหว่างทาง -> เลิกทันที ไม่ต้องกู้ */
        if (r == -USB_ERR_SHUTDOWN || r == -USB_ERR_NOTCONN || !drv_ok(d)) return -1;
        if (a == ESP32_USBH_MSC_IO_RETRY) break;

        /* เลือกวิธีกู้ให้ตรงกับชนิดความผิดพลาด
         *
         * -USB_ERR_INVAL (-2) = CSW กลับมาครบ แต่ bStatus != 0
         *     => นี่คือ "Command Failed" ระดับ SCSI ไม่ใช่ปัญหาของสาย/EP
         *        มี sense data ค้างอยู่ในไดรฟ์ ต้องเคลียร์ด้วย REQUEST SENSE
         *        ถ้าไป BOT reset จะยิ่งแย่ เพราะ sense หายแต่สาเหตุยังอยู่
         *        usbh_msc_scsi_init() ทำ TEST UNIT READY + REQUEST SENSE ให้ครบ
         *
         * อย่างอื่น (STALL / IO / TIMEOUT / BABBLE / DT) = ปัญหาระดับ transport
         *     => BOT reset + CLEAR_FEATURE(HALT) ทั้งสอง endpoint
         */
        if (r == -USB_ERR_INVAL) {
            /* scsi_init ใช้ bulk endpoint ล้วน (CBW/CSW) ไม่แตะ EP0
             * จึงไม่ต้องถือ priv_ep0_lock — และไม่ควรถือ เพราะมันกินเวลานาน */
            int sr = usbh_msc_scsi_init(d->msc);
            ESP_LOGW(TAG, "%s: CSW Command Failed -> เคลียร์ sense (scsi_init=%d)",
                     d->path, sr);
            if (sr < 0 && bot_recover(d) < 0) { ep0_dead = true; break; }
            vTaskDelay(pdMS_TO_TICKS(PRIV_MOUNT_RETRY_DELAY_MS));
        } else {
            if (bot_recover(d) < 0) { ep0_dead = true; break; }
        }
    }

    st_bump(d, &d->st.io_errors);
    d->consec_fail++;
    post_msc_event(d, ESP32_USBH_EV_MSC_IO_ERROR, -1);

    /* ยกระดับเฉพาะเมื่อ EP0 ตายด้วย
     * ถ้า EP0 ยังตอบ = ปัญหาของไดรฟ์ตัวนี้ตัวเดียว
     * ไม่ควร restart host ทั้งบัสจนไดรฟ์ตัวอื่นที่ยังดีพังตามไปด้วย */
    if (ep0_dead && d->consec_fail >= ESP32_USBH_MSC_FAIL_BEFORE_RESTART) {
        priv_request_host_restart("MSC ล้มเหลวติดกัน + EP0 ตาย");
    } else if (!ep0_dead) {
        ESP_LOGW(TAG, "%s: I/O ล้มเหลวแต่ EP0 ยังตอบ -> ไม่ restart host", d->path);
    }
    return -1;
}

/** ซอยคำสั่งตามเพดานฮาร์ดแวร์ แล้วยิงทีละก้อน */
static int scsi_chunked(drive_t *d, bool wr, uint32_t lba, uint8_t *buf,
                        uint32_t nsec, uint16_t bs)
{
    uint32_t maxs = s_max_sec;
    if (maxs < 1) maxs = 1;
    if (maxs > PRIV_HARD_MAX_SEC_PER_CMD) maxs = PRIV_HARD_MAX_SEC_PER_CMD;

    while (nsec) {
        uint32_t n = (nsec > maxs) ? maxs : nsec;
        if (scsi_one(d, wr, lba, buf, n) != 0) return -1;
        lba  += n;
        buf  += (size_t)n * bs;
        nsec -= n;
    }
    return 0;
}

/* ========================================================================
 *  เส้นทาง I/O หลัก (ใช้ร่วมกันระหว่าง diskio และ API ตรง)
 * ======================================================================== */

static int io_run(drive_t *d, bool wr, uint32_t lba, void *buf, uint32_t nsec)
{
    if (!drv_ok(d)) return -1;

    uint16_t bs = d->msc->blocksize ? d->msc->blocksize : PRIV_DEFAULT_BLOCK_SIZE;
    int64_t  t0 = esp_timer_get_time();

    /* นับว่ามี I/O ค้างอยู่ เพื่อให้ usbh_msc_stop() รอได้ถูก */
    d->io_busy++;

    int ret;
    bool used_bounce = false;

    /* buffer ของผู้เรียก aligned พอหรือไม่
     * usbh_submit_urb() assert alignment ตาม CONFIG_USB_ALIGN_SIZE (64 บน P4) */
    if (((uintptr_t)buf % CONFIG_USB_ALIGN_SIZE) == 0) {
        ret = scsi_chunked(d, wr, lba, (uint8_t *)buf, nsec, bs);
    } else {
        used_bounce = true;
        uint32_t maxs = (uint32_t)(d->bounce_sz / bs);
        if (maxs == 0) { d->io_busy--; return -1; }     /* ไม่มี bounce = ทำไม่ได้ */
        uint8_t *p = (uint8_t *)buf;
        uint32_t left = nsec, cur = lba;
        ret = 0;

        /* bounce buffer เป็นของไดรฟ์นี้คนเดียว -> ไม่ต้องใช้ global lock
         * แต่ต้องกันสองเธรดใช้ไดรฟ์เดียวกันพร้อมกัน */
        xSemaphoreTakeRecursive(d->mtx, portMAX_DELAY);
        while (left) {
            uint32_t n = (left > maxs) ? maxs : left;
            if (wr) memcpy(d->bounce, p, (size_t)n * bs);
            if (scsi_chunked(d, wr, cur, d->bounce, n, bs) != 0) { ret = -1; break; }
            if (!wr) memcpy(p, d->bounce, (size_t)n * bs);
            cur  += n;
            p    += (size_t)n * bs;
            left -= n;
        }
        xSemaphoreGiveRecursive(d->mtx);
    }

    uint64_t us = (uint64_t)(esp_timer_get_time() - t0);
    if (wr) st_add_wr(d, nsec, us, used_bounce);
    else    st_add_rd(d, nsec, us, used_bounce);

    d->io_busy--;
    return ret;
}

/* ========================================================================
 *  diskio glue (FatFS)
 *  FatFS เรียกมาจาก task ของผู้ใช้ — ไดรฟ์ต่างกัน = ขนานกันได้
 * ======================================================================== */

static drive_t *by_ctx(void *ctx)
{
    for (int i = 0; i < NDRV; i++) {
        if (s_d[i].valid && s_d[i].msc == (struct usbh_msc *)ctx) return &s_d[i];
    }
    return NULL;
}

static DSTATUS dio_init(void *ctx)   { return by_ctx(ctx) ? 0 : STA_NOINIT; }
static DSTATUS dio_status(void *ctx)
{
    drive_t *d = by_ctx(ctx);
    return (d && drv_ok(d)) ? 0 : STA_NOINIT;
}

static DRESULT dio_read(void *ctx, BYTE *buff, LBA_t sector, UINT count)
{
    drive_t *d = by_ctx(ctx);
    if (!d || !drv_ok(d)) return RES_NOTRDY;
    return io_run(d, false, (uint32_t)sector, buff, count) == 0 ? RES_OK : RES_ERROR;
}

static DRESULT dio_write(void *ctx, const BYTE *buff, LBA_t sector, UINT count)
{
    drive_t *d = by_ctx(ctx);
    if (!d || !drv_ok(d)) return RES_NOTRDY;
    return io_run(d, true, (uint32_t)sector, (void *)buff, count) == 0 ? RES_OK : RES_ERROR;
}

static DRESULT dio_ioctl(void *ctx, BYTE cmd, void *buff)
{
    drive_t *d = by_ctx(ctx);
    if (!d || !drv_ok(d)) return RES_NOTRDY;

    uint16_t bs = d->msc->blocksize ? d->msc->blocksize : PRIV_DEFAULT_BLOCK_SIZE;

    switch (cmd) {
        case CTRL_SYNC:
            /* SCSI SYNCHRONIZE CACHE (0x35) ไม่ได้ถูก export โดย CherryUSB
             * แต่ BOT เขียนเสร็จแล้ว CSW ถึงจะกลับมา จึงถือว่า flush แล้ว */
            return RES_OK;
        case GET_SECTOR_COUNT: *(LBA_t *)buff = (LBA_t)d->msc->blocknum; return RES_OK;
        case GET_SECTOR_SIZE:  *(WORD  *)buff = (WORD)bs;                return RES_OK;
        case GET_BLOCK_SIZE:   *(DWORD *)buff = 1;                       return RES_OK;
        default: return RES_PARERR;
    }
}

static const diskio_ops_t s_ops = {
    .init   = dio_init,
    .status = dio_status,
    .read   = dio_read,
    .write  = dio_write,
    .ioctl  = dio_ioctl,
};

/* ========================================================================
 *  mount / unmount (ทำบน worker task เสมอ ไม่ใช่เธรด hub)
 * ======================================================================== */

static void free_fatfs(drive_t *d)
{
    if (d->fs) { priv_mem_free(d->fs); d->fs = NULL; }
    d->free_bytes = d->total_bytes = 0;
    d->cluster_bytes = 0;
}

/** คำนวณพื้นที่ว่าง — ช้ามาก ผู้เรียกต้องถือ d->mtx อยู่แล้ว */
static void calc_usage_locked(drive_t *d)
{
    d->free_bytes = d->total_bytes = 0;
    if (!d->mounted || !d->msc) return;

    uint16_t bs = d->msc->blocksize ? d->msc->blocksize : PRIV_DEFAULT_BLOCK_SIZE;
    d->cluster_bytes = d->fs ? (uint32_t)d->fs->csize * bs : 0;

    DWORD  fre = 0;
    FATFS *fsp = NULL;
    int64_t t0 = esp_timer_get_time();

    if (f_getfree(d->path, &fre, &fsp) == FR_OK && fsp) {
        d->free_bytes  = (uint64_t)fre * d->cluster_bytes;
        d->total_bytes = (uint64_t)(fsp->n_fatent - 2) * d->cluster_bytes;
    }
    d->usage_at_us = esp_timer_get_time();

    uint32_t ms = (uint32_t)((d->usage_at_us - t0) / 1000);
    if (ms > 200)
        ESP_LOGW(TAG, "%s: f_getfree ใช้ %lu ms (สแกน FAT ทั้งตาราง) "
                      "-> ค่าถูกแคชไว้ เรียก refresh_usage() เองถ้าต้องการค่าสด",
                 d->path, (unsigned long)ms);
}

static void do_mount(int slot)
{
    drive_t *d = &s_d[slot];
    if (!drv_ok(d) || d->mounted) return;

    /* จอง FATFS ตอนนี้ — ก้อนนี้ใหญ่ (มี win[FF_MAX_SS] อยู่ข้างใน
     * ที่ FF_MAX_SS=4096 + exFAT = ~4.8 KB ต่อไดรฟ์) */
    if (!d->fs) {
        d->fs = (FATFS *)priv_alloc(sizeof(FATFS), "FATFS");
        if (!d->fs) {
            ESP_LOGE(TAG, "%s: จอง FATFS (%u ไบต์) ไม่พอ", d->path, (unsigned)sizeof(FATFS));
            post_msc_event(d, ESP32_USBH_EV_MSC_IO_ERROR, -1);
            return;
        }
    }

    FRESULT fr = f_mount(d->fs, d->path, 1);
    if (fr != FR_OK) {
        ESP_LOGE(TAG, "%s: f_mount ล้มเหลว: %d", d->path, fr);
        free_fatfs(d);                    /* คืนแรมทันที อย่าค้างไว้เปล่า ๆ */
        post_msc_event(d, ESP32_USBH_EV_MSC_IO_ERROR, (int)fr);
        return;
    }
    d->mounted = true;

    uint32_t cl_bytes = (uint32_t)d->fs->csize *
                        (d->msc->blocksize ? d->msc->blocksize : PRIV_DEFAULT_BLOCK_SIZE);
    ESP_LOGI(TAG, "%s mount สำเร็จ | %s | %lu sectors x %u B | cluster %lu KB",
             d->path, d->product[0] ? d->product : d->devname,
             (unsigned long)d->msc->blocknum,
             d->msc->blocksize ? d->msc->blocksize : PRIV_DEFAULT_BLOCK_SIZE,
             (unsigned long)(cl_bytes / 1024));

    if (cl_bytes < 32768) {
        ESP_LOGW(TAG, "%s: cluster เล็ก (%lu KB) FatFS จะตัดคำสั่งที่ขอบ cluster "
                      "ทำให้ช้ามาก — ฟอร์แมตใหม่เป็น 64-128 KB จะเร็วขึ้นหลายเท่า",
                 d->path, (unsigned long)(cl_bytes / 1024));
    }

    calc_usage_locked(d);          /* คำนวณครั้งเดียวตรงนี้ */

    post_msc_event(d, ESP32_USBH_EV_MSC_MOUNTED, 0);
}

/* ทำตอนอุปกรณ์เพิ่งเสียบ — อยู่บน worker เพราะมี control transfer */
static void do_attach(int slot)
{
    drive_t *d = &s_d[slot];
    if (!drv_ok(d)) return;

    /* ⚠️ ขั้นตอนที่ขาดไม่ได้: usbh_msc_connect() ของ CherryUSB v1.6.1
     * ทำแค่ GET_MAX_LUN + ตั้ง endpoint + ตั้งชื่อ /dev/sdX แล้วเรียก usbh_msc_run()
     * มัน "ไม่ได้" เรียก usbh_msc_scsi_init() ให้เลย (ต่างจากหลาย stack อื่น)
     *
     * ถ้าไม่เรียกเอง จะเจอ 2 ปัญหาทันที:
     *   1) ไดรฟ์เพิ่งเสียบยังอยู่ในสถานะ Unit Attention / Not Ready
     *      คำสั่งแรกจะคืน CSW bStatus = 1 (Command Failed) ตลอด
     *      ต้อง TEST UNIT READY + REQUEST SENSE จนเคลียร์ก่อน
     *   2) msc->blocknum / blocksize ยังเป็น 0 เพราะไม่เคย READ CAPACITY
     *      -> FatFS จะเห็นไดรฟ์ขนาด 0 sector
     *
     * usbh_msc_scsi_init() ทำครบทั้ง TUR loop + REQUEST SENSE + INQUIRY +
     * READ CAPACITY(10) ให้ในตัวเดียว เรียกจาก worker ได้เพราะไม่ใช่เธรด hub
     */
    vTaskDelay(pdMS_TO_TICKS(PRIV_MSC_SPINUP_DELAY_MS));

    int si = -1;
    for (int t = 0; t < PRIV_SCSI_INIT_RETRY && drv_ok(d); t++) {
        si = usbh_msc_scsi_init(d->msc);
        if (si == 0) break;
        ESP_LOGW(TAG, "%s: scsi_init ยังไม่ผ่าน (%d) รอบ %d/%d — ไดรฟ์อาจกำลังสปินอัป",
                 d->path, si, t + 1, PRIV_SCSI_INIT_RETRY);
        vTaskDelay(pdMS_TO_TICKS(PRIV_SCSI_INIT_DELAY_MS));
    }

    if (si != 0 || !drv_ok(d)) {
        ESP_LOGE(TAG, "%s: เตรียมไดรฟ์ไม่สำเร็จ (scsi_init=%d) ไม่ mount", d->path, si);
        post_msc_event(d, ESP32_USBH_EV_MSC_IO_ERROR, si);
        return;
    }

    if (d->msc->blocksize == 0 || d->msc->blocknum == 0) {
        ESP_LOGE(TAG, "%s: READ CAPACITY คืนค่าไม่สมเหตุสมผล (%lu x %u) ไม่ mount",
                 d->path, (unsigned long)d->msc->blocknum, d->msc->blocksize);
        post_msc_event(d, ESP32_USBH_EV_MSC_IO_ERROR, -1);
        return;
    }

    ESP_LOGI(TAG, "%s: ไดรฟ์พร้อม | %lu sectors x %u B = %llu MB",
             d->path, (unsigned long)d->msc->blocknum, d->msc->blocksize,
             (unsigned long long)((uint64_t)d->msc->blocknum * d->msc->blocksize >> 20));

    /* priv_devtable_add() ดึงสตริงให้ และยิง ESP32_USBH_EV_DEVICE_ATTACHED เอง */
    esp32_usbh_devinfo_t info;
    priv_devtable_add(d->msc->hport, d->msc->intf, ESP32_USBH_CLASS_MSC,
                      d->devname, &info);
    snprintf(d->product, sizeof(d->product), "%s", info.product);

    if (s_auto_mount) do_mount(slot);
    else ESP_LOGI(TAG, "%s: auto-mount ปิดอยู่ — เรียก esp32_usbh_msc_mount() เองได้", d->path);
}

static void do_unmount(int slot)
{
    drive_t *d = &s_d[slot];
    if (!d->mounted) return;
    f_mount(NULL, d->path, 0);
    d->mounted = false;
    free_fatfs(d);
    post_msc_event(d, ESP32_USBH_EV_MSC_UNMOUNTED, 0);
}

static void work_task(void *arg)
{
    (void)arg;
    work_t w;
    while (s_running) {
        if (xQueueReceive(s_work_q, &w, pdMS_TO_TICKS(200)) != pdTRUE) continue;
        if (w.slot < 0 || w.slot >= NDRV) continue;
        switch (w.op) {
            case W_ATTACH:  do_attach(w.slot);  break;
            case W_MOUNT:   do_mount(w.slot);   break;
            default:        do_unmount(w.slot); break;
        }
    }
    vTaskDelete(NULL);
}

/* ========================================================================
 *  hook ของ CherryUSB — รันบนเธรด hub (prio 24) ห้ามบล็อกนาน
 * ======================================================================== */

void usbh_msc_run(struct usbh_msc *msc_class)
{
    if (!s_running) return;

    xSemaphoreTake(s_tbl_mtx, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < NDRV; i++) if (!s_d[i].valid) { slot = i; break; }
    if (slot < 0) { xSemaphoreGive(s_tbl_mtx); ESP_LOGE(TAG, "ไดรฟ์เต็ม"); return; }

    drive_t *d = &s_d[slot];
    d->msc         = msc_class;
    d->consec_fail = 0;
    d->io_busy     = 0;
    d->mounted     = false;
    snprintf(d->devname, sizeof(d->devname), "%s",
             msc_class->hport->config.intf[msc_class->intf].devname);

    BYTE pdrv = 0;
    if (diskio_register_driver(&s_ops, msc_class, &pdrv) != RES_OK) {
        xSemaphoreGive(s_tbl_mtx);
        ESP_LOGE(TAG, "diskio_register_driver ล้มเหลว");
        return;
    }
    d->pdrv = pdrv;

    /* FatFS รองรับเลขไดรฟ์ 0-9 เท่านั้น (FF_VOLUMES สูงสุด 10)
     * จึงประกอบสตริงเองทีละตัวอักษร — ไม่ใช้ snprintf เพราะ GCC มองว่า
     * "%u" ของ unsigned ยาวได้ถึง 10 หลัก แล้วเตือน -Wformat-truncation
     * ทั้งที่ค่าจริงเป็นหลักเดียวเสมอ */
    if (pdrv > 9) {
        xSemaphoreGive(s_tbl_mtx);
        ESP_LOGE(TAG, "pdrv=%u เกิน 9 — FatFS รองรับไม่ได้ (ลด FF_VOLUMES)",
                 (unsigned)pdrv);
        diskio_unregister_driver(pdrv);
        return;
    }
    d->path[0] = (char)('0' + pdrv);
    d->path[1] = ':';
    d->path[2] = '\0';

    d->vid = msc_class->hport->device_desc.idVendor;
    d->pid = msc_class->hport->device_desc.idProduct;

    /* จอง bounce buffer ตอนนี้ (ไม่ใช่ตอนบูต) — ถอดสายแล้วคืนทันที */
    if (!d->bounce) {
        d->bounce = priv_alloc_dma(BOUNCE_BYTES,
                        (size_t)ESP32_USBH_MSC_BOUNCE_MIN_SECTORS * 512,
                        &d->bounce_sz, "MSC bounce");
        if (!d->bounce) {
            diskio_unregister_driver(pdrv);
            xSemaphoreGive(s_tbl_mtx);
            ESP_LOGE(TAG, "แรมภายในไม่พอสำหรับ bounce buffer — ไม่รับไดรฟ์นี้");
            return;
        }
        ESP_LOGI(TAG, "%s: bounce %u ไบต์ (%u sector/ก้อน)",
                 d->path, (unsigned)d->bounce_sz, (unsigned)(d->bounce_sz / 512));
    }

    d->valid = true;                       /* หลังจากนี้ I/O เข้าได้แล้ว */
    xSemaphoreGive(s_tbl_mtx);

    /* ดึงสตริง + ลงตารางอุปกรณ์ + mount ทำบน worker ไม่ใช่ที่นี่
     * (ทั้งหมดมี control transfer / f_mount ซึ่งบล็อกนาน ห้ามทำบนเธรด hub) */
    work_t w = { .op = W_ATTACH, .slot = slot };
    xQueueSend(s_work_q, &w, 0);

    ESP_LOGI(TAG, "เสียบไดรฟ์ %s -> %s (slot %d)", d->devname, d->path, slot);
}

void usbh_msc_stop(struct usbh_msc *msc_class)
{
    int slot = -1;
    xSemaphoreTake(s_tbl_mtx, portMAX_DELAY);
    for (int i = 0; i < NDRV; i++) {
        if (s_d[i].valid && s_d[i].msc == msc_class) { slot = i; break; }
    }
    if (slot >= 0) s_d[slot].valid = false;     /* ตัด I/O ใหม่ทันที */
    xSemaphoreGive(s_tbl_mtx);

    if (slot < 0) return;
    drive_t *d = &s_d[slot];

    /* ต้องรอให้ I/O ที่ค้างอยู่จบก่อนคืน เพราะ usbh_msc_disconnect() จะ
     * usbh_msc_class_free(msc_class) ทันทีหลังจากนี้ */
    for (int t = 0; t < PRIV_STOP_JOIN_TIMEOUT_MS / PRIV_STOP_JOIN_STEP_MS; t++) {
        if (d->io_busy == 0) break;
        vTaskDelay(pdMS_TO_TICKS(PRIV_STOP_JOIN_STEP_MS));
    }
    if (d->io_busy) ESP_LOGE(TAG, "%s: ยังมี I/O ค้าง %lu รายการตอนถอด!",
                             d->path, (unsigned long)d->io_busy);

    if (d->mounted) {
        f_mount(NULL, d->path, 0);
        d->mounted = false;
        post_msc_event(d, ESP32_USBH_EV_MSC_UNMOUNTED, 0);
    }
    diskio_unregister_driver(d->pdrv);
    priv_devtable_remove(msc_class->hport, msc_class->intf);

    /* คืนแรมก้อนใหญ่ทั้งสองทันทีที่ถอดสาย */
    free_fatfs(d);
    if (d->bounce) { priv_mem_free(d->bounce); d->bounce = NULL; d->bounce_sz = 0; }

    ESP_LOGI(TAG, "ถอดไดรฟ์ %s (%s)", d->devname, d->path);
    d->msc = NULL;
}

/* ========================================================================
 *  lifecycle
 * ======================================================================== */

esp_err_t priv_msc_start(void)
{
    memset(s_d, 0, sizeof(s_d));
    s_tbl_mtx = xSemaphoreCreateMutex();
    s_work_q  = xQueueCreate(NDRV * 2, sizeof(work_t));
    if (!s_tbl_mtx || !s_work_q) return ESP_ERR_NO_MEM;

    for (int i = 0; i < NDRV; i++) {
        s_d[i].st_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
        s_d[i].mtx     = xSemaphoreCreateRecursiveMutex();
        /* ⚡ ไม่จอง bounce / FATFS ตรงนี้แล้ว — จองตอนเสียบไดรฟ์จริงเท่านั้น
         * ประหยัดแรมภายใน NDRV x (BOUNCE_BYTES + sizeof(FATFS)) ตอนไม่มีอุปกรณ์ */
        if (!s_d[i].mtx) {
            ESP_LOGE(TAG, "สร้าง mutex ของไดรฟ์ไม่สำเร็จ");
            return ESP_ERR_NO_MEM;
        }
    }

    s_max_sec    = ESP32_USBH_MSC_MAX_SEC_PER_CMD;
    s_auto_mount = priv_auto_mount_enabled();
    s_running    = true;

    if (priv_task_create(work_task, "usbh_msc", ESP32_USBH_MSC_TASK_STACK, NULL,
                         ESP32_USBH_MSC_TASK_PRIO, &s_work_task) != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "โมดูล MSC พร้อม | ไดรฟ์สูงสุด %d | bounce %d B/ไดรฟ์ | sec/cmd %lu",
             NDRV, BOUNCE_BYTES, (unsigned long)s_max_sec);
    ESP_LOGI(TAG, "แรมที่จองไว้ตอนนี้: 0 ไบต์ (จองตอนเสียบไดรฟ์) | "
                  "ต่อไดรฟ์จะใช้ %u + %u = %u ไบต์",
             (unsigned)BOUNCE_BYTES, (unsigned)sizeof(FATFS),
             (unsigned)(BOUNCE_BYTES + sizeof(FATFS)));
    return ESP_OK;
}

void priv_msc_force_unmount_all(void)
{
    for (int i = 0; i < NDRV; i++) {
        drive_t *d = &s_d[i];
        if (!d->valid) continue;
        d->valid = false;
        for (int t = 0; t < 40 && d->io_busy; t++) vTaskDelay(pdMS_TO_TICKS(5));
        if (d->mounted) { f_mount(NULL, d->path, 0); d->mounted = false; }
        diskio_unregister_driver(d->pdrv);
        d->msc = NULL;
    }
}

void priv_msc_stop(void)
{
    s_running = false;
    priv_msc_force_unmount_all();
    vTaskDelay(pdMS_TO_TICKS(250));          /* ให้ work_task ออกเอง */

    for (int i = 0; i < NDRV; i++) {
        if (s_d[i].bounce) { priv_mem_free(s_d[i].bounce); s_d[i].bounce = NULL; s_d[i].bounce_sz = 0; }
        free_fatfs(&s_d[i]);
        if (s_d[i].mtx)    { vSemaphoreDelete(s_d[i].mtx);  s_d[i].mtx = NULL; }
    }
    if (s_work_q)  { vQueueDelete(s_work_q);      s_work_q  = NULL; }
    if (s_tbl_mtx) { vSemaphoreDelete(s_tbl_mtx); s_tbl_mtx = NULL; }
}

/* ========================================================================
 *  API สาธารณะ
 * ======================================================================== */

int esp32_usbh_msc_get_count(void)
{
    int n = 0;
    for (int i = 0; i < NDRV; i++) if (s_d[i].valid) n++;
    return n;
}

int esp32_usbh_msc_get_mounted_count(void)
{
    int n = 0;
    for (int i = 0; i < NDRV; i++) if (s_d[i].valid && s_d[i].mounted) n++;
    return n;
}

esp_err_t esp32_usbh_msc_refresh_usage(int index)
{
    if (index < 0 || index >= NDRV) return ESP_ERR_INVALID_ARG;
    drive_t *d = &s_d[index];
    if (!d->valid) return ESP_ERR_NOT_FOUND;

    xSemaphoreTakeRecursive(d->mtx, portMAX_DELAY);
    calc_usage_locked(d);
    xSemaphoreGiveRecursive(d->mtx);
    return ESP_OK;
}

esp_err_t esp32_usbh_msc_get_info(int index, esp32_usbh_msc_info_t *out)
{
    if (index < 0 || index >= NDRV || !out) return ESP_ERR_INVALID_ARG;
    drive_t *d = &s_d[index];

    memset(out, 0, sizeof(*out));
    if (!d->valid) return ESP_ERR_NOT_FOUND;

    xSemaphoreTakeRecursive(d->mtx, portMAX_DELAY);
    out->present = true;
    out->mounted = d->mounted;
    out->pdrv    = d->pdrv;
    out->vid     = d->vid;
    out->pid     = d->pid;
    snprintf(out->path,    sizeof(out->path),    "%s", d->path);
    snprintf(out->devname, sizeof(out->devname), "%s", d->devname);
    snprintf(out->product, sizeof(out->product), "%s", d->product);

    if (d->msc) {
        out->block_count    = d->msc->blocknum;
        out->block_size     = d->msc->blocksize ? d->msc->blocksize : PRIV_DEFAULT_BLOCK_SIZE;
        out->capacity_bytes = (uint64_t)out->block_count * out->block_size;
    }
    if (d->mounted) {
        /* ⚡ ใช้ค่าที่แคชไว้ตอน mount — ฟังก์ชันนี้ต้อง "เร็วเสมอ"
         * เพราะ UI เรียกถี่มาก ถ้าอยากได้ค่าสดให้เรียก
         * esp32_usbh_msc_refresh_usage() ซึ่งช้าโดยตั้งใจ */
        out->cluster_bytes = d->cluster_bytes;
        out->free_bytes    = d->free_bytes;
        out->total_bytes   = d->total_bytes;
    }
    xSemaphoreGiveRecursive(d->mtx);
    return ESP_OK;
}

int esp32_usbh_msc_get_index_by_path(const char *path)
{
    if (!path) return -1;
    for (int i = 0; i < NDRV; i++) {
        if (s_d[i].valid && strncmp(s_d[i].path, path, sizeof(s_d[i].path)) == 0) return i;
    }
    return -1;
}

esp_err_t esp32_usbh_msc_get_stats(int index, esp32_usbh_msc_stats_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    if (index >= 0) {
        if (index >= NDRV) return ESP_ERR_INVALID_ARG;
        portENTER_CRITICAL(&s_d[index].st_lock);
        *out = s_d[index].st;
        portEXIT_CRITICAL(&s_d[index].st_lock);
        return ESP_OK;
    }
    for (int i = 0; i < NDRV; i++) {
        esp32_usbh_msc_stats_t t;
        portENTER_CRITICAL(&s_d[i].st_lock);
        t = s_d[i].st;
        portEXIT_CRITICAL(&s_d[i].st_lock);
        out->rd_calls += t.rd_calls;   out->wr_calls += t.wr_calls;
        out->rd_sectors += t.rd_sectors; out->wr_sectors += t.wr_sectors;
        out->rd_bounce += t.rd_bounce; out->wr_bounce += t.wr_bounce;
        out->rd_us += t.rd_us;         out->wr_us += t.wr_us;
        out->retries += t.retries;     out->bot_resets += t.bot_resets;
        out->io_errors += t.io_errors;
    }
    return ESP_OK;
}

esp_err_t esp32_usbh_msc_reset_stats(int index)
{
    if (index >= NDRV) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < NDRV; i++) {
        if (index >= 0 && i != index) continue;
        portENTER_CRITICAL(&s_d[i].st_lock);
        memset(&s_d[i].st, 0, sizeof(s_d[i].st));
        portEXIT_CRITICAL(&s_d[i].st_lock);
    }
    return ESP_OK;
}

uint32_t esp32_usbh_msc_get_max_sectors_per_cmd(void) { return s_max_sec; }

esp_err_t esp32_usbh_msc_set_max_sectors_per_cmd(uint32_t sectors)
{
    uint32_t hw = esp32_usbh_get_hw_max_sectors();
    uint32_t lim = (hw < PRIV_HARD_MAX_SEC_PER_CMD) ? hw : PRIV_HARD_MAX_SEC_PER_CMD;

    uint32_t v = sectors;
    if (v < 1)   v = 1;
    if (v > lim) v = lim;

    s_max_sec = v;
    if (v != sectors) {
        ESP_LOGW(TAG, "ขอ %lu sectors แต่ clamp เหลือ %lu (เพดานฮาร์ดแวร์)",
                 (unsigned long)sectors, (unsigned long)v);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "ตั้ง sec/cmd = %lu (%lu KB)", (unsigned long)v, (unsigned long)(v / 2));
    return ESP_OK;
}

esp_err_t esp32_usbh_msc_set_auto_mount(bool enable)
{
    s_auto_mount = enable;
    return ESP_OK;
}

esp_err_t esp32_usbh_msc_mount(int index)
{
    if (index < 0 || index >= NDRV) return ESP_ERR_INVALID_ARG;
    if (!s_d[index].valid)          return ESP_ERR_NOT_FOUND;
    work_t w = { .op = W_MOUNT, .slot = index };
    return xQueueSend(s_work_q, &w, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t esp32_usbh_msc_unmount(int index)
{
    if (index < 0 || index >= NDRV) return ESP_ERR_INVALID_ARG;
    work_t w = { .op = W_UNMOUNT, .slot = index };
    return xQueueSend(s_work_q, &w, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t esp32_usbh_msc_read(int index, uint32_t lba, void *buf, uint32_t nsec)
{
    if (index < 0 || index >= NDRV || !buf) return ESP_ERR_INVALID_ARG;
    drive_t *d = &s_d[index];
    if (!drv_ok(d)) return ESP_ERR_NOT_FOUND;
    return io_run(d, false, lba, buf, nsec) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t esp32_usbh_msc_write(int index, uint32_t lba, const void *buf, uint32_t nsec)
{
    if (index < 0 || index >= NDRV || !buf) return ESP_ERR_INVALID_ARG;
    drive_t *d = &s_d[index];
    if (!drv_ok(d)) return ESP_ERR_NOT_FOUND;
    return io_run(d, true, lba, (void *)buf, nsec) == 0 ? ESP_OK : ESP_FAIL;
}

bool esp32_usbh_msc_lock(int index, uint32_t timeout_ms)
{
    if (index < 0 || index >= NDRV || !s_d[index].mtx) return false;
    return xSemaphoreTakeRecursive(s_d[index].mtx, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void esp32_usbh_msc_unlock(int index)
{
    if (index < 0 || index >= NDRV || !s_d[index].mtx) return;
    xSemaphoreGiveRecursive(s_d[index].mtx);
}

#endif /* ESP32_USBH_ENABLE_MSC */
