/*
 * app_fs_bridge.c — ตัวเชื่อม USB-MSC / SD / Flash → หน้า File Explorer ของ vd_menu
 *
 * ────────────────────────────────────────────────────────────────────────
 *  สิ่งที่ต้องรู้ก่อนอ่านโค้ด
 * ────────────────────────────────────────────────────────────────────────
 *
 * 1. get_drives() / list_dir() ถูก vd_menu เรียก "บนเธรด LVGL" แบบซิงโครนัส
 *    (ดู _render_files_view() ใน vd_menu.c:3436 และ :3561)
 *    -> โค้ดในนี้บล็อก UI ชั่วคราว ห้ามนานเกิน ~200 ms จึง
 *       - ไม่สแกนแบบ recursive
 *       - จำกัดที่ VD_FILE_MAX_DIR_ITEMS
 *       - ใช้ค่าขนาดจาก esp32_usbh_msc_get_info() แทน f_getfree ถ้ามี
 *
 * 2. หน่วยความจำที่ callback จอง vd_menu เป็นคนคืนเองผ่าน
 *    vd_file_items_free() / vd_drives_free()
 *    -> ต้องใช้ calloc/strdup เท่านั้น ห้ามคืน static array เด็ดขาด
 *
 * 3. drive_id ถูก vd_menu เอาไปต่อเป็น full_path = "/<drive_id><path>/<name>"
 *    (vd_menu.c:3417) เช่น "/usb0/รูป/a.png" -> ต้องมี app_fs_resolve() แปลงกลับ
 *
 * 4. หมายเลขไดรฟ์ FatFS ("0:", "1:") ถูกจ่ายแบบไดนามิกโดย
 *    diskio_register_driver() -> ห้าม hardcode ว่า USB = "1:"
 *    ต้องอ่านจาก esp32_usbh_msc_get_info().path เสมอ
 *
 * ⚠️ ไฟล์นี้ "ห้าม" include <dirent.h> เพราะ DIR ของ FatFS ชนกับ DIR ของ POSIX
 *    ส่วนที่ต้องใช้ POSIX อยู่ใน app_fs_vfs.c แยกต่างหาก
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ff.h"
#include "esp32_usb.h"
#include "esp_lvgl_port.h"      /* lvgl_port_lock / unlock */
#include "lvgl.h"

#include "app_fs_bridge.h"

static const char *TAG = "app_fs";

/* ตัวอักษรไดรฟ์ของ LVGL filesystem driver — ปรับให้ตรงกับที่ลงทะเบียนไว้ */
#ifndef APP_FS_LVGL_FATFS_LETTER
#define APP_FS_LVGL_FATFS_LETTER 'S'
#endif
#ifndef APP_FS_LVGL_POSIX_LETTER
#define APP_FS_LVGL_POSIX_LETTER 'P'
#endif

#define MAX_STATIC_DRIVES   4
#define MAX_TOTAL_DRIVES    (MAX_STATIC_DRIVES + ESP32_USBH_MSC_MAX_DRIVES)
#define ID_LEN              16
#define NAME_LEN            48
#define PREFIX_LEN          16
/* ขนาดบัฟเฟอร์พาธ — ไม่ต้องไล่เพิ่มอีกแล้ว ทุกจุดที่ต่อสตริงใช้ sfmt()
 * หรือคำนวณความยาวเองก่อนคัดลอก จึงตรวจการตัดได้ที่รันไทม์ */
#define FULLPATH_LEN        (VD_FILE_MAX_PATH_LEN + 32)

/* ======================================================================
 *  sfmt() — snprintf ที่ "ตรวจการตัด" ได้จริง
 *
 *  ทำไมต้องมี:
 *    -Wformat-truncation ของ GCC จะเตือนทุกครั้งที่มันพิสูจน์ได้ว่า
 *    ผลลัพธ์ "อาจ" ยาวเกินปลายทาง เช่น
 *        char a[N]; char b[N];  snprintf(b, N, "X:%s", a);
 *    กรณีนี้ b อาจต้องใช้ N+2 ไบต์ -> เตือนเสมอ ไม่ว่าจะขยาย N เป็นเท่าไร
 *    (ขยาย N ทำให้ทั้ง a และ b โตพร้อมกัน ส่วนต่าง 2 ไบต์ไม่เคยหายไป)
 *    การไล่เพิ่มขนาดบัฟเฟอร์จึงไม่มีวันจบ
 *
 *  ทางออกที่ถูกต้อง: ใช้ vsnprintf แล้ว "เช็กค่าที่คืนมา" เอง
 *  GCC ไม่วิเคราะห์ vsnprintf แบบ static จึงไม่เตือน และเราได้การตรวจจริง
 *  ที่รันไทม์แทน ซึ่งปลอดภัยกว่าการเดาขนาดด้วยซ้ำ
 *
 *  @return true = เขียนครบ, false = ถูกตัด (ปลายทางยังถูก NUL-terminate เสมอ)
 * ====================================================================== */
static bool sfmt(char *dst, size_t cap, const char *fmt, ...)
{
    if (!dst || cap == 0) return false;

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(dst, cap, fmt, ap);
    va_end(ap);

    if (n < 0) { dst[0] = '\0'; return false; }
    if ((size_t)n >= cap) {
        ESP_LOGW(TAG, "พาธยาวเกิน %u ไบต์ ถูกตัด: %s", (unsigned)cap, dst);
        return false;
    }
    return true;
}

/* ---- ฟังก์ชันฝั่ง POSIX ที่อยู่ใน app_fs_vfs.c ---- */
bool app_fs_vfs_list(const char *real_dir, vd_file_item_t *items,
                     uint32_t cap, uint32_t *out_n);
int  app_fs_vfs_delete(const char *real, bool is_dir);
int  app_fs_vfs_rename(const char *from, const char *to);
int  app_fs_vfs_mkdir(const char *real);

typedef struct {
    bool              used;
    char              id[ID_LEN];
    char              name[NAME_LEN];
    app_fs_backend_t  bk;
    char              prefix[PREFIX_LEN];
} static_drive_t;

static static_drive_t      s_static[MAX_STATIC_DRIVES];
static SemaphoreHandle_t   s_mtx;
static app_fs_refresh_cb_t s_refresh_cb;

/* ======================================================================
 *  ตารางไดรฟ์รวม (ถาวร + USB)
 * ====================================================================== */

typedef struct {
    char             id[ID_LEN];
    char             name[NAME_LEN];
    app_fs_backend_t bk;
    char             prefix[PREFIX_LEN];
    uint32_t         used_mb, total_mb;
} drv_entry_t;

/** อ่านพื้นที่ใช้งานของไดรฟ์ FatFS — ช้าบน FAT32 ก้อนใหญ่ ใช้เท่าที่จำเป็น */
static void fat_usage(const char *prefix, uint32_t *used_mb, uint32_t *total_mb)
{
    *used_mb = *total_mb = 0;

    FATFS *fs = NULL;
    DWORD  free_clust = 0;

    char path[PREFIX_LEN + 2];
    snprintf(path, sizeof(path), "%s", prefix);
    if (f_getfree(path, &free_clust, &fs) != FR_OK || !fs) return;

#if FF_MAX_SS == FF_MIN_SS
    uint32_t ssize = FF_MAX_SS;
#else
    uint32_t ssize = fs->ssize;
#endif

    uint64_t total_sec = (uint64_t)(fs->n_fatent - 2) * fs->csize;
    uint64_t free_sec  = (uint64_t)free_clust * fs->csize;

    *total_mb = (uint32_t)((total_sec * ssize) >> 20);
    *used_mb  = (uint32_t)(((total_sec - free_sec) * ssize) >> 20);
}

/* แคชขนาดของไดรฟ์ FatFS ถาวร (SD/flash) — f_getfree ช้ามาก */
typedef struct { char prefix[PREFIX_LEN]; uint32_t used_mb, total_mb; int64_t at_us; } usage_cache_t;
static usage_cache_t s_ucache[MAX_STATIC_DRIVES];
#define USAGE_CACHE_TTL_US  (30LL * 1000 * 1000)   /* 30 วินาที */

static void cache_usage_put(const char *prefix, uint32_t used_mb, uint32_t total_mb)
{
    int slot = -1;
    for (int i = 0; i < MAX_STATIC_DRIVES; i++) {
        if (!strcmp(s_ucache[i].prefix, prefix))       { slot = i; break; }
        if (slot < 0 && s_ucache[i].prefix[0] == '\0') { slot = i; }
    }
    if (slot < 0) return;
    snprintf(s_ucache[slot].prefix, PREFIX_LEN, "%s", prefix);
    s_ucache[slot].used_mb  = used_mb;
    s_ucache[slot].total_mb = total_mb;
    s_ucache[slot].at_us    = esp_timer_get_time();
}

/** อ่านจากแคชเท่านั้น — ไม่แตะดิสก์ คืน false ถ้ายังไม่มี/หมดอายุ
 *  (การคำนวณจริงทำโดย worker task เพื่อไม่ให้ UI ค้าง) */
static bool cache_usage_get(const char *prefix, uint32_t *used_mb, uint32_t *total_mb)
{
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < MAX_STATIC_DRIVES; i++) {
        if (strcmp(s_ucache[i].prefix, prefix)) continue;
        if (now - s_ucache[i].at_us > USAGE_CACHE_TTL_US) return false;
        *used_mb  = s_ucache[i].used_mb;
        *total_mb = s_ucache[i].total_mb;
        return true;
    }
    return false;
}

/*
 * want_usage = false  -> "โหมดเร็ว" ไม่แตะดิสก์เลย (ใช้กับ find_drive)
 * want_usage = true   -> เติมขนาดไดรฟ์ด้วย (ใช้เฉพาะตอนวาดการ์ดเลือกไดรฟ์)
 *
 * ⚠️ นี่คือจุดที่เคยทำให้ UI เหลือ <10 fps:
 *    find_drive() ถูกเรียกทุกครั้งที่ list_dir / resolve / delete
 *    ถ้ามันไปเรียก f_getfree() ด้วย = สแกน FAT ทั้งตารางทุกคลิก
 */
static uint32_t build_drive_table(drv_entry_t *out, uint32_t cap, bool want_usage)
{
    uint32_t n = 0;

    /* ---- 1) ไดรฟ์ถาวร ---- */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (int i = 0; i < MAX_STATIC_DRIVES && n < cap; i++) {
        if (!s_static[i].used) continue;
        drv_entry_t *d = &out[n];
        memset(d, 0, sizeof(*d));
        snprintf(d->id,     sizeof(d->id),     "%s", s_static[i].id);
        snprintf(d->name,   sizeof(d->name),   "%s", s_static[i].name);
        snprintf(d->prefix, sizeof(d->prefix), "%s", s_static[i].prefix);
        d->bk = s_static[i].bk;
        n++;
    }
    xSemaphoreGive(s_mtx);

    /* ---- 2) ไดรฟ์ USB ที่ mount อยู่ตอนนี้ ---- */
#if ESP32_USBH_ENABLE_MSC
    for (int i = 0; i < ESP32_USBH_MSC_MAX_DRIVES && n < cap; i++) {
        esp32_usbh_msc_info_t mi;
        if (esp32_usbh_msc_get_info(i, &mi) != ESP_OK) continue;
        if (!mi.present || !mi.mounted) continue;   /* ยังไม่พร้อม ไม่ต้องโชว์ */

        drv_entry_t *d = &out[n];
        memset(d, 0, sizeof(*d));
        snprintf(d->id, sizeof(d->id), "usb%d", i);

        if (mi.product[0])
            snprintf(d->name, sizeof(d->name), "USB: %s", mi.product);
        else
            snprintf(d->name, sizeof(d->name), "USB %04X:%04X", mi.vid, mi.pid);

        snprintf(d->prefix, sizeof(d->prefix), "%s", mi.path);   /* "1:" */
        d->bk = APP_FS_BK_FATFS;

        /* ใช้ค่าที่ไลบรารีคำนวณไว้แล้ว เร็วกว่าเรียก f_getfree ซ้ำ */
        d->total_mb = (uint32_t)(mi.capacity_bytes >> 20);
        if (mi.total_bytes > 0)
            d->used_mb = (uint32_t)((mi.total_bytes - mi.free_bytes) >> 20);
        n++;
    }
#endif

    /* ---- 3) เติมขนาด เฉพาะเมื่อต้องวาดการ์ดไดรฟ์จริง ๆ ---- */
    if (want_usage) {
        for (uint32_t i = 0; i < n; i++) {
            if (out[i].total_mb) continue;                /* USB มีค่ามาแล้ว */
            if (out[i].bk == APP_FS_BK_FATFS)
                cache_usage_get(out[i].prefix, &out[i].used_mb, &out[i].total_mb);
        }
    }
    return n;
}

static bool find_drive(const char *id, drv_entry_t *out)
{
    drv_entry_t tbl[MAX_TOTAL_DRIVES];
    uint32_t n = build_drive_table(tbl, MAX_TOTAL_DRIVES, false);  /* โหมดเร็ว */
    for (uint32_t i = 0; i < n; i++)
        if (strcmp(tbl[i].id, id) == 0) { *out = tbl[i]; return true; }
    return false;
}

/* ======================================================================
 *  ยูทิลิตี้
 * ====================================================================== */

vd_file_type_t app_fs_type_by_name(const char *name, bool is_dir)
{
    if (is_dir) return VD_FILE_TYPE_DIR;

    const char *dot = strrchr(name, '.');
    if (!dot) return VD_FILE_TYPE_FILE;
    dot++;

    char e[8] = {0};
    for (int i = 0; i < 7 && dot[i]; i++)
        e[i] = (dot[i] >= 'A' && dot[i] <= 'Z') ? (char)(dot[i] + 32) : dot[i];

    if (!strcmp(e,"png")||!strcmp(e,"jpg")||!strcmp(e,"jpeg")||
        !strcmp(e,"bmp")||!strcmp(e,"gif")||!strcmp(e,"sjpg")) return VD_FILE_TYPE_IMG;
    if (!strcmp(e,"avi")||!strcmp(e,"mp4")||!strcmp(e,"mkv")||
        !strcmp(e,"mov"))                                       return VD_FILE_TYPE_VID;
    if (!strcmp(e,"txt")||!strcmp(e,"json")||!strcmp(e,"log")||
        !strcmp(e,"csv")||!strcmp(e,"ini")||!strcmp(e,"cfg"))   return VD_FILE_TYPE_TXT;
    return VD_FILE_TYPE_FILE;
}

/** "1:" + "/a/b" -> "1:/a/b"  คืน false ถ้าพาธยาวเกิน */
static bool join_path(const drv_entry_t *d, const char *path, char *out, size_t len)
{
    if (!path || !path[0] || !strcmp(path, "/"))
        return sfmt(out, len, "%s/", d->prefix);
    if (path[0] == '/')
        return sfmt(out, len, "%s%s", d->prefix, path);
    return sfmt(out, len, "%s/%s", d->prefix, path);
}

/** โฟลเดอร์ขึ้นก่อน แล้วเรียงตามชื่อ */
int app_fs_item_cmp(const void *a, const void *b)
{
    const vd_file_item_t *x = (const vd_file_item_t *)a;
    const vd_file_item_t *y = (const vd_file_item_t *)b;
    bool dx = (x->type == VD_FILE_TYPE_DIR), dy = (y->type == VD_FILE_TYPE_DIR);
    if (dx != dy) return dx ? -1 : 1;
    return strcasecmp(x->name ? x->name : "", y->name ? y->name : "");
}

/* ======================================================================
 *  งานเบื้องหลัง: แคชรายการไฟล์ + worker task
 *
 *  ปัญหา: vd_menu เรียก list_dir() แบบซิงโครนัสบนเธรด LVGL
 *         การอ่าน FAT จาก USB กินเวลา 30-500 ms -> UI ค้างทุกครั้งที่เข้าโฟลเดอร์
 *
 *  วิธีแก้โดยไม่ต้องแก้ vd_menu.c เลย:
 *    1. list_dir() ดูแคชก่อน — ถ้ามี คืนทันที (ไม่กี่ไมโครวินาที)
 *    2. ถ้าไม่มี -> โยนงานให้ worker task แล้วคืนแถว "กำลังอ่านข้อมูล..." ทันที
 *    3. worker อ่านเสร็จ -> เก็บลงแคช -> สั่งรีเฟรชหน้า
 *    4. รอบที่สองเข้าแคช -> วาดเสร็จทันที
 *
 *  ผล: เธรด LVGL ไม่เคยแตะดิสก์เลย ปัดจอลื่นตลอด
 * ====================================================================== */

#define FS_CACHE_SLOTS   3
/* เพดานรวมของทุกช่องแคช — กันโฟลเดอร์ใหญ่ 3 โฟลเดอร์กินแรมพร้อมกัน
 * 1200 รายการ ~ (16 B struct + ชื่อเฉลี่ย 32 B) x 1200 = ~60 KB */
#define FS_CACHE_MAX_TOTAL_ITEMS 1200
#define FS_CACHE_TTL_US  (10LL * 1000 * 1000)
#define FS_LOADING_NAME  "\xE2\x8C\x9B  กำลังอ่านข้อมูล..."
#define KEY_LEN          (ID_LEN + VD_FILE_MAX_PATH_LEN + 2)

typedef struct {
    bool            valid;
    char            key[KEY_LEN];
    vd_file_item_t *items;
    uint32_t        count;
    int64_t         at_us;
} dir_cache_t;

typedef struct {
    bool usage_only;
    char drive_id[ID_LEN];
    char path[VD_FILE_MAX_PATH_LEN + 1];
} job_t;

static dir_cache_t       s_dcache[FS_CACHE_SLOTS];
static SemaphoreHandle_t s_cache_mtx;
static QueueHandle_t     s_jobq;
static TaskHandle_t      s_worker;
static char              s_pending_key[KEY_LEN];   /* กันโยนงานซ้ำ */

static void make_key(char *out, const char *drive_id, const char *path)
{
    sfmt(out, KEY_LEN, "%s|%s", drive_id, (path && path[0]) ? path : "/");
}

static void items_free(vd_file_item_t *items, uint32_t n)
{
    if (!items) return;
    for (uint32_t i = 0; i < n; i++) free(items[i].name);
    free(items);
}

static void cache_free_entry(dir_cache_t *c)
{
    items_free(c->items, c->count);
    c->items = NULL; c->count = 0; c->valid = false; c->key[0] = '\0';
}

static void cache_clear_all(void)
{
    if (!s_cache_mtx) return;
    xSemaphoreTake(s_cache_mtx, portMAX_DELAY);
    for (int i = 0; i < FS_CACHE_SLOTS; i++) cache_free_entry(&s_dcache[i]);
    xSemaphoreGive(s_cache_mtx);
}

/** คัดลอกจากแคชออกมาเป็นก้อนใหม่ เพราะ vd_menu จะ free ให้เอง */
static bool cache_lookup_clone(const char *key, vd_file_item_t **out, uint32_t *n)
{
    bool hit = false;
    xSemaphoreTake(s_cache_mtx, portMAX_DELAY);

    for (int i = 0; i < FS_CACHE_SLOTS; i++) {
        dir_cache_t *c = &s_dcache[i];
        if (!c->valid || strcmp(c->key, key)) continue;
        if (esp_timer_get_time() - c->at_us > FS_CACHE_TTL_US) { cache_free_entry(c); break; }

        vd_file_item_t *cp = NULL;
        if (c->count) {
            cp = (vd_file_item_t *)calloc(c->count, sizeof(vd_file_item_t));
            if (!cp) break;
            for (uint32_t k = 0; k < c->count; k++) {
                cp[k].type    = c->items[k].type;
                cp[k].size_kb = c->items[k].size_kb;
                cp[k].name    = strdup(c->items[k].name ? c->items[k].name : "");
            }
        }
        *out = cp;
        *n   = c->count;
        hit  = true;
        break;
    }

    xSemaphoreGive(s_cache_mtx);
    return hit;
}

static void cache_store(const char *key, vd_file_item_t *items, uint32_t n)
{
    xSemaphoreTake(s_cache_mtx, portMAX_DELAY);

    int slot = -1;
    int64_t oldest = INT64_MAX;
    for (int i = 0; i < FS_CACHE_SLOTS; i++) {
        if (!s_dcache[i].valid)            { slot = i; break; }
        if (!strcmp(s_dcache[i].key, key)) { slot = i; break; }
        if (s_dcache[i].at_us < oldest)    { oldest = s_dcache[i].at_us; slot = i; }
    }
    cache_free_entry(&s_dcache[slot]);

    sfmt(s_dcache[slot].key, KEY_LEN, "%s", key);
    s_dcache[slot].items = items;
    s_dcache[slot].count = n;
    s_dcache[slot].at_us = esp_timer_get_time();
    s_dcache[slot].valid = true;

    /* ถ้ารวมกันแล้วเกินเพดาน ให้ทิ้งช่องที่เก่าที่สุดทีละช่อง
     * (ยกเว้นช่องที่เพิ่งเก็บ — ไม่งั้น cb_list_dir จะ miss แล้วสั่งงานวนไม่จบ) */
    for (;;) {
        uint32_t total = 0;
        for (int i = 0; i < FS_CACHE_SLOTS; i++)
            if (s_dcache[i].valid) total += s_dcache[i].count;
        if (total <= FS_CACHE_MAX_TOTAL_ITEMS) break;

        int victim = -1;
        int64_t oldest_us = INT64_MAX;
        for (int i = 0; i < FS_CACHE_SLOTS; i++) {
            if (i == slot || !s_dcache[i].valid) continue;
            if (s_dcache[i].at_us < oldest_us) { oldest_us = s_dcache[i].at_us; victim = i; }
        }
        if (victim < 0) break;                 /* เหลือแต่ช่องปัจจุบัน ปล่อยไว้ */
        cache_free_entry(&s_dcache[victim]);
    }

    xSemaphoreGive(s_cache_mtx);
}

static void post_job(const job_t *j);
void app_fs_invalidate_cache(void);

/* ขอรีเฟรชหน้าไฟล์แบบ "ปลอดภัย"
 *
 * ⚠️ ห้ามเรียก s_refresh_cb() ตรง ๆ จากข้างใน callback ของ LVGL
 *    เพราะมันจะ lv_obj_clean() ลบ object ที่ LVGL กำลังวนส่ง event อยู่
 *    = use-after-free แบบสุ่ม  ต้องเลื่อนไปทำหลัง event loop จบเสมอ */
static void refresh_async_cb(void *p);
static void request_refresh(void)
{
    if (s_refresh_cb) lv_async_call(refresh_async_cb, NULL);
}

/** เรียกจาก task อื่นที่ไม่ใช่ LVGL — ต้องถือ lock ก่อนแตะคิว async ของ LVGL */
static void request_refresh_from_task(void)
{
    if (!s_refresh_cb) return;
    if (lvgl_port_lock(200)) {
        lv_async_call(refresh_async_cb, NULL);
        lvgl_port_unlock();
    }
}

/* ======================================================================
 *  callback: get_drives
 * ====================================================================== */

static bool cb_get_drives(vd_drive_info_t **out_drives, uint32_t *out_count, void *ud)
{
    (void)ud;
    drv_entry_t tbl[MAX_TOTAL_DRIVES];
    uint32_t n = build_drive_table(tbl, MAX_TOTAL_DRIVES, true);

    *out_drives = NULL;
    *out_count  = 0;
    if (n == 0) return true;                     /* ไม่มีไดรฟ์ ไม่ใช่ error */

    vd_drive_info_t *arr = (vd_drive_info_t *)calloc(n, sizeof(vd_drive_info_t));
    if (!arr) return false;

    for (uint32_t i = 0; i < n; i++) {
        arr[i].id       = strdup(tbl[i].id);
        arr[i].name     = strdup(tbl[i].name);
        arr[i].used_mb  = tbl[i].used_mb;
        arr[i].total_mb = tbl[i].total_mb;
    }

    *out_drives = arr;
    *out_count  = n;

    /* ถ้ายังไม่รู้ขนาดไดรฟ์ไหน ให้ worker ไปคำนวณแล้วค่อยรีเฟรชหน้าให้เอง
     * (f_getfree สแกน FAT ทั้งตาราง ห้ามทำบนเธรด LVGL) */
    for (uint32_t i = 0; i < n; i++) {
        if (tbl[i].total_mb == 0) {
            job_t j = { .usage_only = true };
            post_job(&j);
            break;
        }
    }

    ESP_LOGI(TAG, "พบไดรฟ์ %lu ตัว", (unsigned long)n);
    return true;
}

/* ======================================================================
 *  callback: list_dir
 * ====================================================================== */

/** ลดขนาด array ลงให้พอดีกับจำนวนจริง
 *  สำคัญมากเมื่อ VD_FILE_MAX_DIR_ITEMS ใหญ่ (2000 ช่อง = 32 KB ต่อการ list หนึ่งครั้ง)
 *  แคช 3 ช่องจะกินฟรี ๆ ~96 KB ถ้าไม่หดก่อนเก็บ */
static vd_file_item_t *shrink_items(vd_file_item_t *items, uint32_t n)
{
    if (!items) return NULL;
    if (n == 0) { free(items); return NULL; }
    vd_file_item_t *p = (vd_file_item_t *)realloc(items, n * sizeof(vd_file_item_t));
    return p ? p : items;                  /* realloc ย่อแทบไม่พลาด แต่กันไว้ */
}

static bool list_fatfs(const drv_entry_t *d, const char *path,
                       vd_file_item_t **out_items, uint32_t *out_count)
{
    char real[FULLPATH_LEN];
    if (!join_path(d, path, real, sizeof(real))) return false;

    DIR dir;                                   /* DIR ของ FatFS */
    FRESULT fr = f_opendir(&dir, real);
    if (fr != FR_OK) {
        ESP_LOGW(TAG, "f_opendir(\"%s\") = %d", real, fr);
        return false;
    }

    vd_file_item_t *items =
        (vd_file_item_t *)calloc(VD_FILE_MAX_DIR_ITEMS, sizeof(vd_file_item_t));
    if (!items) { f_closedir(&dir); return false; }

    uint32_t n = 0;
    FILINFO fno;
    while (n < VD_FILE_MAX_DIR_ITEMS) {
        if (f_readdir(&dir, &fno) != FR_OK) break;
        if (fno.fname[0] == 0) break;                     /* จบไดเรกทอรี */
        if (fno.fattrib & AM_HID) continue;
        if (fno.fname[0] == '.') continue;
        if (!strcasecmp(fno.fname, "System Volume Information")) continue;
        if (!strncmp(fno.fname, "._", 2)) continue;       /* ขยะจาก macOS */

        bool is_dir = (fno.fattrib & AM_DIR) != 0;
        items[n].name = strdup(fno.fname);
        if (!items[n].name) break;
        items[n].type    = app_fs_type_by_name(fno.fname, is_dir);
        items[n].size_kb = is_dir ? 0 : (uint32_t)((fno.fsize + 1023) / 1024);
        n++;
    }
    f_closedir(&dir);

    if (n >= VD_FILE_MAX_DIR_ITEMS)
        ESP_LOGW(TAG, "โฟลเดอร์นี้เกิน %d รายการ — แสดงได้ไม่ครบ", VD_FILE_MAX_DIR_ITEMS);

    qsort(items, n, sizeof(vd_file_item_t), app_fs_item_cmp);
    *out_items = shrink_items(items, n);
    *out_count = n;
    return true;
}

static bool list_vfs(const drv_entry_t *d, const char *path,
                     vd_file_item_t **out_items, uint32_t *out_count)
{
    char real[FULLPATH_LEN];
    if (!join_path(d, path, real, sizeof(real))) return false;

    vd_file_item_t *items =
        (vd_file_item_t *)calloc(VD_FILE_MAX_DIR_ITEMS, sizeof(vd_file_item_t));
    if (!items) return false;

    uint32_t n = 0;
    if (!app_fs_vfs_list(real, items, VD_FILE_MAX_DIR_ITEMS, &n)) {
        free(items);
        return false;
    }

    if (n >= VD_FILE_MAX_DIR_ITEMS)
        ESP_LOGW(TAG, "โฟลเดอร์นี้เกิน %d รายการ — แสดงได้ไม่ครบ", VD_FILE_MAX_DIR_ITEMS);

    qsort(items, n, sizeof(vd_file_item_t), app_fs_item_cmp);
    *out_items = shrink_items(items, n);
    *out_count = n;
    return true;
}

static bool make_placeholder(vd_file_item_t **out, uint32_t *n)
{
    vd_file_item_t *it = (vd_file_item_t *)calloc(1, sizeof(vd_file_item_t));
    if (!it) { *out = NULL; *n = 0; return true; }
    it->name    = strdup(FS_LOADING_NAME);
    it->type    = VD_FILE_TYPE_FILE;
    it->size_kb = 0;
    *out = it; *n = 1;
    return true;
}

/** งานจริงที่อ่านดิสก์ — รันบน worker task เท่านั้น ห้ามเรียกจากเธรด LVGL */
static void do_list_job(const job_t *j)
{
    drv_entry_t d;
    if (!find_drive(j->drive_id, &d)) {
        ESP_LOGW(TAG, "ไดรฟ์ \"%s\" หายไปแล้ว", j->drive_id);
        cache_clear_all();
        request_refresh_from_task();
        return;
    }

    vd_file_item_t *items = NULL;
    uint32_t        n     = 0;

    int64_t t0 = esp_timer_get_time();
    bool ok = (d.bk == APP_FS_BK_FATFS)
            ? list_fatfs(&d, j->path, &items, &n)
            : list_vfs(&d, j->path, &items, &n);
    uint32_t dt = (uint32_t)((esp_timer_get_time() - t0) / 1000);

    if (!ok) { items = NULL; n = 0; }

    ESP_LOGI(TAG, "[worker] list %s%s -> %lu รายการ (%lu ms) — UI ไม่ค้าง",
             j->drive_id, j->path, (unsigned long)n, (unsigned long)dt);

    char key[KEY_LEN];
    make_key(key, j->drive_id, j->path);
    cache_store(key, items, n);

    request_refresh_from_task();
}

static void fs_worker_task(void *arg)
{
    (void)arg;
    job_t j;
    for (;;) {
        if (xQueueReceive(s_jobq, &j, portMAX_DELAY) != pdTRUE) continue;

        if (j.usage_only) {
#if ESP32_USBH_ENABLE_MSC
            for (int i = 0; i < ESP32_USBH_MSC_MAX_DRIVES; i++)
                esp32_usbh_msc_refresh_usage(i);     /* ช้าได้ ไม่กระทบ UI */
#endif
            for (int i = 0; i < MAX_STATIC_DRIVES; i++) {
                if (!s_static[i].used || s_static[i].bk != APP_FS_BK_FATFS) continue;
                uint32_t u, t;
                fat_usage(s_static[i].prefix, &u, &t);
                cache_usage_put(s_static[i].prefix, u, t);
            }
            request_refresh_from_task();
        } else {
            do_list_job(&j);
        }

        s_pending_key[0] = '\0';
    }
}

static void post_job(const job_t *j)
{
    if (!s_jobq) return;
    if (xQueueSend(s_jobq, j, 0) != pdTRUE)
        ESP_LOGW(TAG, "คิวงานเต็ม — ข้ามคำขอนี้");
}

static bool cb_list_dir(const char *drive_id, const char *path,
                        vd_file_item_t **out_items, uint32_t *out_count, void *ud)
{
    (void)ud;
    *out_items = NULL;
    *out_count = 0;

    char key[KEY_LEN];
    make_key(key, drive_id, path);

    /* 1) แคชมี -> คืนทันที ไม่แตะดิสก์ */
    if (cache_lookup_clone(key, out_items, out_count)) return true;

    /* 2) ยังไม่มี -> สั่งงานเบื้องหลัง แล้วคืนแถว placeholder ทันที */
    if (strcmp(s_pending_key, key) != 0) {
        sfmt(s_pending_key, KEY_LEN, "%s", key);

        job_t j = { .usage_only = false };
        sfmt(j.drive_id, sizeof(j.drive_id), "%s", drive_id);
        sfmt(j.path, sizeof(j.path), "%s", (path && path[0]) ? path : "/");
        post_job(&j);
    }
    return make_placeholder(out_items, out_count);
}

/* ======================================================================
 *  แปลง full_path -> พาธจริง
 * ====================================================================== */

bool app_fs_resolve(const char *full_path, char *out, size_t out_len,
                    app_fs_backend_t *out_bk)
{
    if (!full_path || full_path[0] != '/') return false;

    const char *p = full_path + 1;                 /* ข้าม '/' ตัวแรก */
    const char *slash = strchr(p, '/');
    size_t idlen = slash ? (size_t)(slash - p) : strlen(p);
    if (idlen == 0 || idlen >= ID_LEN) return false;

    char id[ID_LEN];
    memcpy(id, p, idlen);
    id[idlen] = '\0';

    drv_entry_t d;
    if (!find_drive(id, &d)) return false;

    if (!sfmt(out, out_len, "%s%s", d.prefix, slash ? slash : "/")) return false;
    if (out_bk) *out_bk = d.bk;
    return true;
}

bool app_fs_resolve_lvgl(const char *full_path, char *out, size_t out_len)
{
    if (!out || out_len < 5) return false;

    /* เขียนพาธจริงลง out+2 โดยตรง ไม่ต้องมีบัฟเฟอร์กลาง
     * -> ไม่มีการ copy ซ้ำ และ -Wformat-truncation ไม่มีอะไรให้เตือน */
    app_fs_backend_t bk;
    if (!app_fs_resolve(full_path, out + 2, out_len - 2, &bk)) {
        out[0] = '\0';
        return false;
    }

    out[0] = (bk == APP_FS_BK_FATFS) ? APP_FS_LVGL_FATFS_LETTER
                                     : APP_FS_LVGL_POSIX_LETTER;
    out[1] = ':';
    return true;
}

/* ======================================================================
 *  การกระทำกับไฟล์
 * ====================================================================== */

esp_err_t app_fs_delete(const char *full_path, bool is_dir)
{
    char real[FULLPATH_LEN];
    app_fs_backend_t bk;
    if (!app_fs_resolve(full_path, real, sizeof(real), &bk)) return ESP_ERR_NOT_FOUND;

    if (bk == APP_FS_BK_FATFS) {
        FRESULT fr = f_unlink(real);          /* ใช้ได้ทั้งไฟล์และโฟลเดอร์ว่าง */
        if (fr != FR_OK) { ESP_LOGE(TAG, "f_unlink(%s)=%d", real, fr); return ESP_FAIL; }
        app_fs_invalidate_cache();
        return ESP_OK;
    }
    app_fs_invalidate_cache();
    return (app_fs_vfs_delete(real, is_dir) == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t app_fs_rename(const char *full_path, const char *new_name)
{
    char real[FULLPATH_LEN];
    app_fs_backend_t bk;
    if (!app_fs_resolve(full_path, real, sizeof(real), &bk)) return ESP_ERR_NOT_FOUND;

    char *last = strrchr(real, '/');
    if (!last) return ESP_ERR_INVALID_ARG;

    /* ตัดชื่อเดิมทิ้งแล้วต่อชื่อใหม่ "ในที่" — ไม่ต้องมีบัฟเฟอร์ที่สอง */
    size_t dirlen = (size_t)(last - real) + 1;          /* รวม '/' */
    if (dirlen + strlen(new_name) >= sizeof(real)) {
        ESP_LOGE(TAG, "ชื่อใหม่ยาวเกิน");
        return ESP_ERR_INVALID_SIZE;
    }
    char dst[FULLPATH_LEN];
    memcpy(dst, real, dirlen);
    strcpy(dst + dirlen, new_name);

    if (bk == APP_FS_BK_FATFS)
        return (f_rename(real, dst) == FR_OK) ? ESP_OK : ESP_FAIL;
    return (app_fs_vfs_rename(real, dst) == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t app_fs_mkdir(const char *drive_id, const char *cur_path, const char *name)
{
    drv_entry_t d;
    if (!find_drive(drive_id, &d)) return ESP_ERR_NOT_FOUND;

    /* ต่อชื่อโฟลเดอร์ "ในที่" บนบัฟเฟอร์เดียว ไม่ซ้อนสองใบ */
    char real[FULLPATH_LEN];
    if (!join_path(&d, cur_path, real, sizeof(real))) return ESP_ERR_INVALID_SIZE;

    size_t bl = strlen(real);
    if (bl == 0 || real[bl - 1] != '/') {
        if (bl + 1 >= sizeof(real)) return ESP_ERR_INVALID_SIZE;
        real[bl++] = '/';
        real[bl]   = '\0';
    }
    if (bl + strlen(name) >= sizeof(real)) return ESP_ERR_INVALID_SIZE;
    strcpy(real + bl, name);

    esp_err_t r = (d.bk == APP_FS_BK_FATFS)
                ? ((f_mkdir(real) == FR_OK) ? ESP_OK : ESP_FAIL)
                : ((app_fs_vfs_mkdir(real) == 0) ? ESP_OK : ESP_FAIL);
    app_fs_invalidate_cache();
    return r;
}

/* ======================================================================
 *  hot-plug
 * ====================================================================== */

static void refresh_async_cb(void *p)
{
    (void)p;
    if (s_refresh_cb) s_refresh_cb();
}

void app_fs_invalidate_cache(void)
{
    cache_clear_all();
    s_pending_key[0] = '\0';
}

void app_fs_bridge_set_refresh_cb(app_fs_refresh_cb_t cb) { s_refresh_cb = cb; }

void app_fs_notify_usb_changed(void)
{
    app_fs_invalidate_cache();               /* รายการเดิมใช้ไม่ได้แล้ว */
    if (!s_refresh_cb) return;

    /* on_usb() รันบน event task ของไลบรารี USB ไม่ใช่เธรด LVGL
     * แตะ object ของ LVGL ตรง ๆ จากที่นี่ = heap พัง
     * ต้องถือ lock แล้วฝากงานให้เธรด LVGL ทำเอง */
    if (lvgl_port_lock(200)) {
        lv_async_call(refresh_async_cb, NULL);
        lvgl_port_unlock();
    } else {
        ESP_LOGW(TAG, "lvgl_port_lock timeout — ข้ามการรีเฟรชรอบนี้");
    }
}

/* ======================================================================
 *  init / register
 * ====================================================================== */

esp_err_t app_fs_register_static_drive(const char *id, const char *name,
                                       app_fs_backend_t bk, const char *prefix)
{
    if (!id || !name || !prefix || !s_mtx) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < MAX_STATIC_DRIVES; i++) {
        if (s_static[i].used && !strcmp(s_static[i].id, id)) { slot = i; break; }
        if (slot < 0 && !s_static[i].used) slot = i;
    }
    if (slot < 0) { xSemaphoreGive(s_mtx); return ESP_ERR_NO_MEM; }

    snprintf(s_static[slot].id,     ID_LEN,     "%s", id);
    snprintf(s_static[slot].name,   NAME_LEN,   "%s", name);
    snprintf(s_static[slot].prefix, PREFIX_LEN, "%s", prefix);
    s_static[slot].bk   = bk;
    s_static[slot].used = true;
    xSemaphoreGive(s_mtx);

    ESP_LOGI(TAG, "ลงทะเบียนไดรฟ์ \"%s\" (%s) -> %s", id, name, prefix);
    return ESP_OK;
}

void app_fs_unregister_static_drive(const char *id)
{
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (int i = 0; i < MAX_STATIC_DRIVES; i++)
        if (s_static[i].used && !strcmp(s_static[i].id, id)) s_static[i].used = false;
    xSemaphoreGive(s_mtx);
}

/* ---- พฤติกรรมเริ่มต้นของ callback (override จาก main.c ได้) ---- */

static void cb_on_file_click(const char *drive_id, const char *full_path,
                             const vd_file_item_t *item, void *ud)
{
    (void)drive_id; (void)ud;
    if (item->name && !strcmp(item->name, FS_LOADING_NAME)) return;   /* แถว placeholder */
    char lv_path[FULLPATH_LEN];
    if (!app_fs_resolve_lvgl(full_path, lv_path, sizeof(lv_path))) return;

    ESP_LOGI(TAG, "เปิดไฟล์ %s -> LVGL src \"%s\" (type=%d, %lu KB)",
             full_path, lv_path, (int)item->type, (unsigned long)item->size_kb);
    /* ตัวอย่าง: lv_image_set_src(preview_img, lv_path); */
}

static void cb_on_new_folder(const char *drive_id, const char *cur_path, void *ud)
{
    (void)ud;
    esp_err_t e = app_fs_mkdir(drive_id, cur_path, "โฟลเดอร์ใหม่");
    ESP_LOGI(TAG, "สร้างโฟลเดอร์ที่ %s%s -> %s",
             drive_id, cur_path ? cur_path : "/", esp_err_to_name(e));
    request_refresh();
}

static vd_file_browser_cbs_t s_cbs;

const vd_file_browser_cbs_t *app_fs_get_browser_cbs(void) { return &s_cbs; }

esp_err_t app_fs_bridge_init(void)
{
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) return ESP_ERR_NO_MEM;

    if (!s_cache_mtx) s_cache_mtx = xSemaphoreCreateMutex();
    if (!s_jobq)      s_jobq      = xQueueCreate(6, sizeof(job_t));
    if (!s_cache_mtx || !s_jobq) return ESP_ERR_NO_MEM;

    if (!s_worker) {
        /* prio 4 — ต่ำกว่าเธรด LVGL (12) เสมอ เพื่อไม่ให้แย่ง CPU ตอนวาดจอ
         * stack 5 KB: FatFS LFN ใช้ heap อยู่แล้ว แต่ FILINFO + พาธกินพอควร */
        if (xTaskCreate(fs_worker_task, "fs_worker", 5120, NULL, 4, &s_worker) != pdPASS)
            return ESP_ERR_NO_MEM;
    }

    memset(&s_cbs, 0, sizeof(s_cbs));
    s_cbs.get_drives    = cb_get_drives;
    s_cbs.list_dir      = cb_list_dir;
    s_cbs.on_file_click = cb_on_file_click;
    s_cbs.on_new_folder = cb_on_new_folder;
    /* actions / action_count / on_action_click ให้ main.c เติมเอง
     * เพราะชุดเมนูกดค้างเป็นเรื่องของแอป ไม่ใช่ของ bridge */
    return ESP_OK;
}
