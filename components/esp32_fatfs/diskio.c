#include "diskio.h"
#include <string.h>
#include <time.h>

// เก็บข้อมูล Driver แต่ละช่อง
typedef struct {
    diskio_ops_t ops;
    void *ctx;
    bool is_used;
} disk_slot_t;

// จำนวน Slot สูงสุดจะอิงตาม FF_VOLUMES ใน ffconf.h
static disk_slot_t s_slots[FF_VOLUMES];

DRESULT diskio_register_driver(const diskio_ops_t *ops, void *ctx, BYTE *out_pdrv) {
    if (!ops || !out_pdrv) return RES_PARERR;

    // วนหา Slot ว่างแรกที่ยังไม่ได้ใช้งาน
    for (BYTE i = 0; i < FF_VOLUMES; i++) {
        if (!s_slots[i].is_used) {
            s_slots[i].ops = *ops;
            s_slots[i].ctx = ctx;
            s_slots[i].is_used = true;
            *out_pdrv = i; // คืนค่าเลข pdrv ที่จัดสรรได้กลับไป
            return RES_OK;
        }
    }
    return RES_ERROR; // ไดรฟ์เต็มโควตาตาม FF_VOLUMES
}

void diskio_unregister_driver(BYTE pdrv) {
    if (pdrv < FF_VOLUMES) {
        memset(&s_slots[pdrv], 0, sizeof(disk_slot_t));
    }
}

/* -----------------------------------------------------------------
   ฟังก์ชันที่ FatFS เรียกใช้ -> วิ่งส่งต่อ (Dispatch) ไปยัง Driver ตัวจริง
   ----------------------------------------------------------------- */

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv >= FF_VOLUMES || !s_slots[pdrv].is_used || !s_slots[pdrv].ops.status) {
        return STA_NOINIT;
    }
    return s_slots[pdrv].ops.status(s_slots[pdrv].ctx);
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv >= FF_VOLUMES || !s_slots[pdrv].is_used || !s_slots[pdrv].ops.init) {
        return STA_NOINIT;
    }
    return s_slots[pdrv].ops.init(s_slots[pdrv].ctx);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv >= FF_VOLUMES || !s_slots[pdrv].is_used || !s_slots[pdrv].ops.read) {
        return RES_PARERR;
    }
    return s_slots[pdrv].ops.read(s_slots[pdrv].ctx, buff, sector, count);
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv >= FF_VOLUMES || !s_slots[pdrv].is_used || !s_slots[pdrv].ops.write) {
        return RES_PARERR;
    }
    return s_slots[pdrv].ops.write(s_slots[pdrv].ctx, buff, sector, count);
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv >= FF_VOLUMES || !s_slots[pdrv].is_used || !s_slots[pdrv].ops.ioctl) {
        return RES_PARERR;
    }
    return s_slots[pdrv].ops.ioctl(s_slots[pdrv].ctx, cmd, buff);
}

DWORD get_fattime(void) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < 80) {
        return ((DWORD)(2026 - 1980) << 25) | (1 << 21) | (1 << 16);
    }

    return ((DWORD)(timeinfo.tm_year - 80) << 25)
         | ((DWORD)(timeinfo.tm_mon + 1) << 21)
         | ((DWORD)timeinfo.tm_mday << 16)
         | ((DWORD)timeinfo.tm_hour << 11)
         | ((DWORD)timeinfo.tm_min << 5)
         | ((DWORD)timeinfo.tm_sec >> 1);
}